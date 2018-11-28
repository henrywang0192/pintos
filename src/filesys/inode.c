#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* Amount of direct data blocks in an inode */
#define DIRECT_BLOCKS 10

/* Amount of sectors an indirection block contains */
#define SECTORS_PER_INDIRECTION_BLOCK BLOCK_SECTOR_SIZE / sizeof(block_sector_t)

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    //block_sector_t start;
    block_sector_t direct[DIRECT_BLOCKS]; /* Direct data sectors. */
    block_sector_t first_indirect;        /* First level indirection block */
    block_sector_t second_indirect;       /* Second level indirection block */
    off_t length;                         /* File size in bytes. */
    unsigned magic;                       /* Magic number. */
    struct inode_details details;

    uint32_t unused[104];                 /* Not used. */
  };  

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct inode_disk data;             /* Inode content. */
  };

struct inode_details{
   uint16_t level;  //1 , 2 , 3      
   uint16_t first_level_block; //last allocated data block in first level if lvl1
   uint16_t second_level_block; //idx of first level indirect block if lvl2
   uint16_t third_level_idx; //idx of second level indirection block if lvl3
   uint16_t index_block;   // idx of first level IB of third_level_idx if lvl 3
};
block_sector_t grow_file(const struct inode *inode, off_t pos){
  static char zeros[BLOCK_SECTOR_SIZE];
  struct inode_disk *disk_inode = &inode ->data;
  uint16_t orig_lvl = disk_inode->details.level;
  uint16_t new_lvl;

  if(pos < BLOCK_SECTOR_SIZE * DIRECT_BLOCKS) 
      new_lvl = 1;
  else if(pos < BLOCK_SECTOR_SIZE * DIRECT_BLOCKS + SECTORS_PER_INDIRECTION_BLOCK
                          * BLOCK_SECTOR_SIZE)
      new_lvl = 2;
  else
      new_lvl = 3;
  
  long int sectors = (long int) bytes_to_sectors (pos - inode->data.length);
  //file originally in 1st level
  bool in_direct_level = inode->data.length < DIRECT_BLOCKS * BLOCK_SECTOR_SIZE;
  if(orig_lvl == 1){
    block_sector_t end_block;
    if(new_lvl == 1)
      end_block = pos / BLOCK_SECTOR_SIZE;
    else
      end_block = DIRECT_BLOCKS - 1;

    block_sector_t start_block = disk_inode->details.first_level_block + 1;
    sectors = sectors - (end_block - start_block);

    while(start_block <= end_block){
       bool direct_allocated = free_map_allocate(1, &disk_inode->direct[start_block]);
        if(!direct_allocated)
              return false;
       block_write (fs_device, disk_inode->direct[start_block++], zeros);
    }
  
  }
 


  //Sector is in first level indirection block
  if(sectors > 0 && (orig_lvl == 1 || orig_lvl == 2)) {
    block_sector_t start_block2;

  
    block_sector_t indirect_block[SECTORS_PER_INDIRECTION_BLOCK];

    //first time in 1st indirection level
    if(orig_lvl == 1){
       bool first_indirect_allocated = 
              free_map_allocate(1, &disk_inode->first_indirect);
            if(!first_indirect_allocated)
              return false;
            start_block2 = 0;

    }
    else{
      start_block2 = disk_inode->details.second_level_block;
      block_read(fs_device, disk_inode->first_indirect, 
                &indirect_block);
    }
    
    int i;
    for(i = start_block2 ; i < sectors
              && i < SECTORS_PER_INDIRECTION_BLOCK; i++){
            
              bool direct_allocated = free_map_allocate(1,
                &indirect_block[i]);

              if(!direct_allocated)
                return false;
              
              block_write(fs_device, indirect_block[i], zeros);
     }
    block_write(fs_device, disk_inode->first_indirect, &indirect_block);
    sectors -= i - start_block2;
  }

  //Sector is in second level indirection block
  if(sectors >0){
    block_sector_t start_block3;
    block_sector_t index_block_start;



    bool first_time_in_second;
    if (inode->data.length < DIRECT_BLOCKS * BLOCK_SECTOR_SIZE +
                             SECTORS_PER_INDIRECTION_BLOCK * BLOCK_SECTOR_SIZE)
    {
      first_time_in_second = true;
      start_block3= 0;
      index_block_start = 0;
    }
    if (first_time_in_second)
    {
      bool second_indirect_allocated =
          free_map_allocate(1, &disk_inode->second_indirect);
      if (!second_indirect_allocated)
        return false;
      block_write(fs_device,&disk_inode->second_indirect, zeros);

    }
    else{
      start_block3 = (pos - DIRECT_BLOCKS * BLOCK_SECTOR_SIZE + 
                          SECTORS_PER_INDIRECTION_BLOCK * BLOCK_SECTOR_SIZE)/ 
                          (BLOCK_SECTOR_SIZE * SECTORS_PER_INDIRECTION_BLOCK);
      int second_level_byte_offset=  pos - (DIRECT_BLOCKS * BLOCK_SECTOR_SIZE) 
        - (SECTORS_PER_INDIRECTION_BLOCK * BLOCK_SECTOR_SIZE);
      index_block_start = (second_level_byte_offset - (start_block3 * 
                      SECTORS_PER_INDIRECTION_BLOCK * BLOCK_SECTOR_SIZE))
                      / BLOCK_SECTOR_SIZE;
    }

    
    int i;
    int j;
    block_sector_t 
            second_level_indirect[SECTORS_PER_INDIRECTION_BLOCK];  
    block_read(fs_device, start_block3, 
                &second_level_indirect);
    block_sector_t indirect_block[SECTORS_PER_INDIRECTION_BLOCK];
    block_read(fs_device, index_block_start, 
                &indirect_block);
    for (i = start_block3; i < sectors; i += SECTORS_PER_INDIRECTION_BLOCK)
    {
      block_read(fs_device, i, 
                &second_level_indirect);

      
      for (j = index_block_start; j < (sectors - i) && j < SECTORS_PER_INDIRECTION_BLOCK; j++)
      {

        block_read(fs_device, j, 
                &indirect_block);

        block_write(fs_device, indirect_block[j], zeros);
      }
      block_write(fs_device, second_level_indirect[i], &indirect_block);
    }
    block_write(fs_device, disk_inode->second_indirect,
                &second_level_indirect);
  }
}
/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);
  if(pos > inode->data.length){
    return grow_file(inode, pos);
  }
  //Sector is in one of the direct blocks
  if((pos / BLOCK_SECTOR_SIZE) < DIRECT_BLOCKS){
    return inode->data.direct[0] + pos / BLOCK_SECTOR_SIZE;
  }
  //Sector is in first level indirection block
  else if(pos - (DIRECT_BLOCKS * BLOCK_SECTOR_SIZE) 
    < SECTORS_PER_INDIRECTION_BLOCK * BLOCK_SECTOR_SIZE){
    block_sector_t first_indirect[SECTORS_PER_INDIRECTION_BLOCK];
    block_read(fs_device, inode->data.first_indirect, &first_indirect);

    return first_indirect[(pos - (DIRECT_BLOCKS * BLOCK_SECTOR_SIZE)) 
      / BLOCK_SECTOR_SIZE];
  }

  //Sector is in second level indirection block
  else if(pos < SECTORS_PER_INDIRECTION_BLOCK * SECTORS_PER_INDIRECTION_BLOCK 
    * BLOCK_SECTOR_SIZE){
    block_sector_t second_indirect[SECTORS_PER_INDIRECTION_BLOCK];
    block_read(fs_device, inode->data.second_indirect, &second_indirect);

    off_t second_indirect_start = pos - (DIRECT_BLOCKS * BLOCK_SECTOR_SIZE) 
        - (SECTORS_PER_INDIRECTION_BLOCK * BLOCK_SECTOR_SIZE);

    unsigned sector_idx = second_indirect_start 
      / (BLOCK_SECTOR_SIZE * SECTORS_PER_INDIRECTION_BLOCK);
    
    block_sector_t first_indirect_pointer = 
      second_indirect[sector_idx];
    block_sector_t first_indirect[SECTORS_PER_INDIRECTION_BLOCK];
    block_read(fs_device, first_indirect_pointer, &first_indirect);

    return first_indirect[(second_indirect_start 
      - (sector_idx * SECTORS_PER_INDIRECTION_BLOCK * BLOCK_SECTOR_SIZE))
      / BLOCK_SECTOR_SIZE];
  }
  else{
    PANIC("SHOULDNT BE HERE");
    return -1;
  }
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;
 
  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  disk_inode->details.level = 1;
  disk_inode->details.first_level_block = 0;
  disk_inode->details.second_level_block = 0;
  disk_inode->details.third_level_idx = 0;
  disk_inode->details.index_block = 0;

  if (disk_inode != NULL)
    {
      long int sectors = (long int) bytes_to_sectors (length);
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;

      if (sectors > 0) 
        {
          static char zeros[BLOCK_SECTOR_SIZE];
          size_t i;
          
          for (i = 0; i < sectors && i < DIRECT_BLOCKS; i++){ 
            bool direct_allocated = 
              free_map_allocate(1, &disk_inode->direct[i]);
            if(!direct_allocated)
              return false;
              
            block_write (fs_device, disk_inode->direct[i], zeros);

          disk_inode->details.first_level_block = i;

          }
          sectors -= DIRECT_BLOCKS;

          if(sectors > 0){
          disk_inode->details.level = 2;

            bool first_indirect_allocated = 
              free_map_allocate(1, &disk_inode->first_indirect);
            if(!first_indirect_allocated)
              return false;

            block_sector_t indirect_block[SECTORS_PER_INDIRECTION_BLOCK];
            // block_read(fs_device, disk_inode->first_indirect, 
            //     &indirect_block);

            for(i = 0; i < sectors
              && i < SECTORS_PER_INDIRECTION_BLOCK; i++){
            
              bool direct_allocated = free_map_allocate(1,
                &indirect_block[i]);

              if(!direct_allocated)
                return false;
              
              block_write(fs_device, indirect_block[i], zeros);
              disk_inode->details.second_level_block = i;

            }
            block_write(fs_device, disk_inode->first_indirect, &indirect_block);
          }
          
          sectors -= SECTORS_PER_INDIRECTION_BLOCK;
          if(sectors > 0){
            disk_inode->details.level = 3;

            bool second_indirect_allocated = 
              free_map_allocate(1, &disk_inode->second_indirect);
            if(!second_indirect_allocated)
              return false;
            
            block_sector_t 
            second_level_indirect[SECTORS_PER_INDIRECTION_BLOCK];
            // block_read(fs_device, disk_inode->second_indirect, 
            //   &second_level_indirect);

            for(i = 0; i < sectors; i+= SECTORS_PER_INDIRECTION_BLOCK){
              disk_inode->details.third_level_idx = i;

              bool first_indirect_allocated =
                free_map_allocate(1, &second_level_indirect[i]);
              
              if(!first_indirect_allocated)
                return false;

              block_sector_t indirect_block[SECTORS_PER_INDIRECTION_BLOCK];
              block_read(fs_device, second_level_indirect[i], 
                &indirect_block);
              
              size_t j;
              for(j = 0; j < (sectors - i)
                && j < SECTORS_PER_INDIRECTION_BLOCK; j++){
                disk_inode->details.index_block = j;

                bool direct_allocated = free_map_allocate(1,
                &indirect_block[j]);
                
                if(!direct_allocated){
                  return false;
                }

                block_write(fs_device, indirect_block[j], zeros);
              }
              block_write(fs_device, second_level_indirect[i], &indirect_block);
            }
            block_write(fs_device, disk_inode->second_indirect, 
              &second_level_indirect);
          }
        }
      block_write (fs_device, sector, disk_inode);
      success = true; 
      free (disk_inode);
    }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  block_read (fs_device, inode->sector, &inode->data);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk. (Does it?  Check code.)
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
          
          off_t length = inode->data.length;
          block_sector_t sector;
          for(sector = 0; sector * BLOCK_SECTOR_SIZE < length; sector++){
            free_map_release(byte_to_sector(inode, sector * BLOCK_SECTOR_SIZE)
              ,1);
          }

          free_map_release (inode->sector, 1);
        }

      free (inode); 
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Read full sector directly into caller's buffer. */
          block_read (fs_device, sector_idx, buffer + bytes_read);
        }
      else 
        {
          /* Read sector into bounce buffer, then partially copy
             into caller's buffer. */
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }
          block_read (fs_device, sector_idx, bounce);
          memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
        }
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  free (bounce);
  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Write full sector directly to disk. */
          block_write (fs_device, sector_idx, buffer + bytes_written);
        }
      else 
        {
          /* We need a bounce buffer. */
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }

          /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all zeros. */
          if (sector_ofs > 0 || chunk_size < sector_left) 
            block_read (fs_device, sector_idx, bounce);
          else
            memset (bounce, 0, BLOCK_SECTOR_SIZE);
          memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
          block_write (fs_device, sector_idx, bounce);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  free (bounce);

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}
