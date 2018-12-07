#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include <stdbool.h>

size_t get_first_indirect_from_second(off_t);
size_t get_direct_level(off_t);
size_t get_first_indirect(off_t);
size_t get_second_indirect(off_t);

block_sector_t install_direct_block(void);
block_sector_t install_first_indirect(long int);
block_sector_t install_second_indirect(long int);

//Chris is driving
/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
{
  block_sector_t direct[DIRECT_BLOCKS]; /* Direct data sectors. */
  block_sector_t first_indirect;        /* First level indirection block */
  block_sector_t second_indirect;       /* Second level indirection block */
  off_t length;                         /* File size in bytes. */
  bool is_directory;                    /* Is this inode a directory? */
  unsigned magic;                       /* Magic number. */

  uint32_t unused[113]; /* Not used. */
};

//Tarun is driving
/* In-memory inode. */
struct inode
{
  struct list_elem elem;   /* Element in inode list. */
  block_sector_t sector;   /* Sector number of disk location. */
  int open_cnt;            /* Number of openers. */
  bool removed;            /* True if deleted, false otherwise. */
  int deny_write_cnt;      /* 0: writes ok, >0: deny writes. */
  struct lock growth_lock; /* Synchronize file growth */
  struct lock dir_lock;    /* Synchronize operations in a dir */
  struct inode_disk data;  /* Inode content. */
  bool is_growing;         /* Flag to see if growth is occuring */
};

//Aaron is driving
/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors(off_t size)
{
  return DIV_ROUND_UP(size, BLOCK_SECTOR_SIZE);
}

/* Returns if inode represents directory or not */
bool inode_isdir(const struct inode *inode)
{
  return inode->data.is_directory;
}

//Henry is driving
/* Grows file depending on where position is */
void grow_file(struct inode *inode, off_t pos)
{
  //Ensure that no other writers are present
  size_t length = inode_length(inode);
  lock_acquire(&inode->growth_lock);
  inode->is_growing = true;
  if (length != inode_length(inode))
  {
    return;
  }

  struct inode_disk *disk_inode = &inode->data;

  //Obtain number of sectors we have to write
  long int sectors = bytes_to_sectors(pos) - bytes_to_sectors(length);
  size_t start_direct_idx = get_direct_level(length);
  //Run through direct sectors, and install them as long as
  //we are in bounds
  if (sectors > 0 && start_direct_idx != NOT_PRESENT)
  {
    int i;
    i = length == 0 ? 0 : start_direct_idx + 1;
    while (sectors > 0 && i < DIRECT_BLOCKS)
    {
      block_sector_t sector = install_direct_block();
      if (sector == -1)
      {
        return;
      }
      disk_inode->direct[i] = sector;
      sectors--;
      i++;
    }
  }
  //Chris is driving
  //Go through first indirect block and allocate while we still have
  //sectors remaining
  size_t start_second_indirect = get_second_indirect(length);
  size_t start_first_indirect = get_first_indirect(length);
  if (sectors > 0 && start_second_indirect == NOT_PRESENT)
  {
    int i;
    i = start_first_indirect != NOT_PRESENT ? start_first_indirect + 1 : 0;
    //If at the  beginning of the first indirect, else we go to else block
    if (i == 0)
    {
      block_sector_t sector = install_first_indirect(sectors);
      if (sector == -1)
        PANIC("FIRST");
      disk_inode->first_indirect = sector;
      sectors = sectors - SECTORS_PER_INDIRECTION_BLOCK;
    }
    else
    {
      block_sector_t block[SECTORS_PER_INDIRECTION_BLOCK];
      block_read(fs_device, disk_inode->first_indirect, &block);
      while (sectors > 0 && i < SECTORS_PER_INDIRECTION_BLOCK)
      {
        block_sector_t sector = install_direct_block();
        if (sector == -1)
          PANIC("SECOND");
        block[i] = sector;
        sectors--;
        i++;
      }
      //Write changes to block
      block_write(fs_device, disk_inode->first_indirect, &block);
    }
  }
  //Go through first indirect block of doubly indirect block
  //and allocate while we still have sectors remaining, and move
  //to second indirect block after first indirect block failed
  if (sectors > 0)
  {
    int i;
    i = start_second_indirect != NOT_PRESENT ? start_second_indirect : 0;
    //If at the beginning of the second indirect block
    if (i == 0 && start_second_indirect == NOT_PRESENT)
    {
      disk_inode->second_indirect = install_second_indirect(sectors);
    }
    else
    {
      block_sector_t block[SECTORS_PER_INDIRECTION_BLOCK];
      block_read(fs_device, disk_inode->second_indirect, &block);
      size_t first_index = get_first_indirect_from_second(length);
      int j;
      j = first_index + 1;
      //Install second indirect block to store location of first
      //indirect block, which in turn holds sector of data
      while (sectors > 0 && i < SECTORS_PER_INDIRECTION_BLOCK)
      {
        block_sector_t first_block[SECTORS_PER_INDIRECTION_BLOCK];
        block_read(fs_device, block[i], &first_block);

        while (sectors > 0 && j < SECTORS_PER_INDIRECTION_BLOCK)
        {
          first_block[j] = install_direct_block();
          sectors--;
          j++;
        }
        j = 0;
        //Traverse to next second second-indirect block
        block_sector_t new_sector;
        free_map_allocate(1, &new_sector);
        block_write(fs_device, block[i], &first_block);
        block[++i] = new_sector;
        block_write(fs_device, disk_inode->second_indirect, &block);
      }
    }
  }
  //Change length to new position
  disk_inode->length = pos;
  block_write(fs_device, inode->sector, disk_inode);
}

//Tarun is driving
/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector(const struct inode *inode, off_t pos)
{
  ASSERT(inode != NULL);

  //Sector is in one of the direct blocks
  size_t direct_idx = get_direct_level(pos);
  if (direct_idx != NOT_PRESENT)
  {
    return inode->data.direct[direct_idx];
  }

  size_t first_indirect_idx = get_first_indirect(pos);
  //Sector is in first level indirection block
  if (first_indirect_idx != NOT_PRESENT)
  {
    block_sector_t first_indirect[SECTORS_PER_INDIRECTION_BLOCK];
    block_read(fs_device, inode->data.first_indirect, &first_indirect);
    return first_indirect[first_indirect_idx];
  }

  size_t second_indirect_idx = get_second_indirect(pos);
  //Sector is in second level indirection block
  if (second_indirect_idx != NOT_PRESENT)
  {
    block_sector_t second_indirect[SECTORS_PER_INDIRECTION_BLOCK];
    block_read(fs_device, inode->data.second_indirect, &second_indirect);

    block_sector_t first_indirect_pointer =
        second_indirect[second_indirect_idx];

    block_sector_t first_indirect[SECTORS_PER_INDIRECTION_BLOCK];
    block_read(fs_device, first_indirect_pointer, &first_indirect);
    return first_indirect[get_first_indirect_from_second(pos)];
  }
  else
  {
    PANIC("-1");
    return -1;
  }
}

//Aaron is driving
/*
    Obtains a free sector and fills it with zeros.
*/
block_sector_t
install_direct_block()
{
  block_sector_t sector;
  //Obtain sector to allocate to
  bool success = free_map_allocate(1, &sector);

  if (!success)
    return -1;

  //Write data to sector
  static char zeros[BLOCK_SECTOR_SIZE];
  block_write(fs_device, sector, zeros);

  return sector;
}

/*
    Create an indirect index block by filling an empty sector with the
    indexes of direct blocks.
*/
block_sector_t
install_first_indirect(long int sectors)
{
  block_sector_t indirect_sector;
  //Obtains indirect sector to write to
  bool success = free_map_allocate(1, &indirect_sector);

  if (!success)
    return -1;

  block_sector_t indirect_block[SECTORS_PER_INDIRECTION_BLOCK];

  int i;
  i = 0;
  //Runs through indirection block to install block
  while (sectors > 0 && i < SECTORS_PER_INDIRECTION_BLOCK)
  {
    block_sector_t sector = install_direct_block();
    if (sector == -1)
      return -1;
    indirect_block[i] = sector;
    sectors--;
    i++;
  }
  //Write data to sector
  block_write(fs_device, indirect_sector, &indirect_block);
  return indirect_sector;
}

//Henry is driving
/*
    Create a second level index block by filling an empty sector with
    the indexes of first level indirect index blocks.
*/
block_sector_t
install_second_indirect(long int sectors)
{
  block_sector_t second_indirect_sector;
  //Finds sector to allocate to
  bool success = free_map_allocate(1, &second_indirect_sector);

  if (!success)
    return -1;

  block_sector_t indirect_block[SECTORS_PER_INDIRECTION_BLOCK];

  int i;
  i = 0;
  //Goes through second indirect block to install first indirect
  //block location
  while (sectors > 0 && i < SECTORS_PER_INDIRECTION_BLOCK)
  {
    block_sector_t sector = install_first_indirect(sectors);
    if (sector == -1)
      return -1;
    indirect_block[i] = sector;
    sectors -= SECTORS_PER_INDIRECTION_BLOCK;
    i++;
  }
  //Updates block with new metadata
  block_write(fs_device, second_indirect_sector, &indirect_block);
  return second_indirect_sector;
}

//Gets offset of position in direct level
size_t
get_direct_level(off_t pos)
{
  if (pos < DIRECT_BLOCKS * BLOCK_SECTOR_SIZE)
  {
    return pos / BLOCK_SECTOR_SIZE;
  }
  return NOT_PRESENT;
}

//Tarun is driving
//Gets offset of position in first indirection level
size_t
get_first_indirect(off_t pos)
{
  if (pos >= DIRECT_BLOCKS * BLOCK_SECTOR_SIZE &&
      pos < (SECTORS_PER_INDIRECTION_BLOCK * BLOCK_SECTOR_SIZE) +
                (DIRECT_BLOCKS * BLOCK_SECTOR_SIZE))
  {
    off_t first_indirect_index = pos - (DIRECT_BLOCKS * BLOCK_SECTOR_SIZE);
    return first_indirect_index / BLOCK_SECTOR_SIZE;
  }
  return NOT_PRESENT;
}

//Gets offset of position in second indirection level
size_t
get_second_indirect(off_t pos)
{
  if (get_direct_level(pos) == NOT_PRESENT &&
      get_first_indirect(pos) == NOT_PRESENT)
  {
    off_t second_indirect_off = pos - (DIRECT_BLOCKS * BLOCK_SECTOR_SIZE) -
                                (SECTORS_PER_INDIRECTION_BLOCK *
                                   BLOCK_SECTOR_SIZE);

    return second_indirect_off / (BLOCK_SECTOR_SIZE *
                                  SECTORS_PER_INDIRECTION_BLOCK);
  }
  return NOT_PRESENT;
}

//Aaron is driving
//Gets location of first indirect block based on second
//indirect block location
size_t
get_first_indirect_from_second(off_t pos)
{
  ASSERT(get_second_indirect(pos) != NOT_PRESENT);
  size_t idx = get_second_indirect(pos);
  //Beginning offset of second indirection block
  off_t second_indirect_off = pos - (DIRECT_BLOCKS * BLOCK_SECTOR_SIZE) -
                              (SECTORS_PER_INDIRECTION_BLOCK * 
                                BLOCK_SECTOR_SIZE);

  off_t first_indirect_off = second_indirect_off - idx *
          SECTORS_PER_INDIRECTION_BLOCK * BLOCK_SECTOR_SIZE;

  return first_indirect_off / BLOCK_SECTOR_SIZE;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void inode_init(void)
{
  list_init(&open_inodes);
}

//Aaron is driving
/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool inode_create(block_sector_t sector, off_t length, bool is_directory)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT(length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT(sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc(1, sizeof *disk_inode);

  if (disk_inode != NULL)
  {
    long int sectors = (long int)bytes_to_sectors(length);
    disk_inode->length = length;
    disk_inode->is_directory = is_directory;
    disk_inode->magic = INODE_MAGIC;

    //While there are sectors left, set direct block to sector,
    //and update location sector of indirection block
    if (sectors > 0)
    {
      size_t i;
      //Set sector of direct blocks
      for (i = 0; i < sectors && i < DIRECT_BLOCKS; i++)
      {
        block_sector_t sector = install_direct_block();
        if (sector == -1)
          return false;
        disk_inode->direct[i] = sector;
      }

      sectors -= DIRECT_BLOCKS;

      //Sets sector associated with first indirect block
      if (sectors > 0)
      {
        block_sector_t sector = install_first_indirect(sectors);
        if (sector == -1)
          return false;

        disk_inode->first_indirect = sector;
      }

      //Sets sector associated with second indirect block
      sectors -= SECTORS_PER_INDIRECTION_BLOCK;
      if (sectors > 0)
      {
        block_sector_t sector = install_second_indirect(sectors);
        if (sector == -1)
          return false;
        disk_inode->second_indirect = sector;
      }
    }

    //Writes inode to sector in file system
    block_write(fs_device, sector, disk_inode);
    success = true;
    free(disk_inode);
  }
  return success;
}

//Henry is driving
/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open(block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin(&open_inodes); e != list_end(&open_inodes);
       e = list_next(e))
  {
    inode = list_entry(e, struct inode, elem);
    if (inode->sector == sector)
    {
      inode_reopen(inode);
      return inode;
    }
  }

  /* Allocate memory. */
  inode = malloc(sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front(&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  //Initialize growth and directory lock
  lock_init(&inode->growth_lock);
  lock_init(&inode->dir_lock);
  block_read(fs_device, inode->sector, &inode->data);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen(struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

//Chris is driving
/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber(const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk. (Does it?  Check code.)
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void inode_close(struct inode *inode)
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
  {
    /* Remove from inode list and release lock. */
    list_remove(&inode->elem);

    /* Deallocate blocks if removed. */
    if (inode->removed)
    {

      off_t length = inode->data.length;
      block_sector_t sector;
      for (sector = 0; sector * BLOCK_SECTOR_SIZE < length; sector++)
      {
        free_map_release(byte_to_sector(inode, sector * BLOCK_SECTOR_SIZE), 1);
      }

      free_map_release(inode->sector, 1);
    }

    free(inode);
  }
}

//Chris is driving
/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void inode_remove(struct inode *inode)
{
  ASSERT(inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t inode_read_at(struct inode *inode, void *buffer_, off_t size
  , off_t offset)
{
  //Acquires growth lock, and if write is currently occuring,
  //returns length back to original, so we don't read while
  //growing from write
  lock_acquire(&inode->growth_lock);
  if (offset + size > inode_length(inode))
    size = inode_length(inode) - offset;
  lock_release(&inode->growth_lock);

  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;

  while (size > 0)
  {
    /* Disk sector to read, starting byte offset within sector. */
    block_sector_t sector_idx = byte_to_sector(inode, offset);
    int sector_ofs = offset % BLOCK_SECTOR_SIZE;

    /* Bytes left in inode, bytes left in sector, lesser of the two. */
    off_t inode_left = inode_length(inode) - offset;
    int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
    int min_left = inode_left < sector_left ? inode_left : sector_left;

    /* Number of bytes to actually copy out of this sector. */
    int chunk_size = size < min_left ? size : min_left;
    if (chunk_size <= 0)
      break;

    //Henry is driving
    if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
    {
      /* Read full sector directly into caller's buffer. */
      block_read(fs_device, sector_idx, buffer + bytes_read);
    }
    else
    {
      /* Read sector into bounce buffer, then partially copy
             into caller's buffer. */
      if (bounce == NULL)
      {
        bounce = malloc(BLOCK_SECTOR_SIZE);
        if (bounce == NULL)
          break;
      }
      block_read(fs_device, sector_idx, bounce);
      memcpy(buffer + bytes_read, bounce + sector_ofs, chunk_size);
    }

    /* Advance. */
    size -= chunk_size;
    offset += chunk_size;
    bytes_read += chunk_size;
  }
  free(bounce);

  return bytes_read;
}

//Tarun is driving
/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t inode_write_at(struct inode *inode, const void *buffer_, off_t size,
                     off_t offset)
{
  const uint8_t *buffer = buffer_;
  bool growth_occured = false;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
  {
    return 0;
  }

  //If offset + size is past the current inode length, then
  //grow file, and set flag to true
  if (size + offset > inode_length(inode))
  {
    grow_file(inode, size + offset);
    growth_occured = true;
  }
  while (size > 0)
  {
    /* Sector to write, starting byte offset within sector. */
    block_sector_t sector_idx = byte_to_sector(inode, offset);
    int sector_ofs = offset % BLOCK_SECTOR_SIZE;

    /* Bytes left in inode, bytes left in sector, lesser of the two. */
    off_t inode_left = inode_length(inode) - offset;
    int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
    int min_left = inode_left < sector_left ? inode_left : sector_left;

    /* Number of bytes to actually write into this sector. */
    int chunk_size = size < min_left ? size : min_left;
    if (chunk_size <= 0)
      break;

    if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
    {
      /* Write full sector directly to disk. */
      block_write(fs_device, sector_idx, buffer + bytes_written);
    }
    else
    {
      /* We need a bounce buffer. */
      if (bounce == NULL)
      {
        bounce = malloc(BLOCK_SECTOR_SIZE);
        if (bounce == NULL)
          break;
      }
      //Aaron is driving
      /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all zeros. */
      if (sector_ofs > 0 || chunk_size < sector_left)
        block_read(fs_device, sector_idx, bounce);
      else
        memset(bounce, 0, BLOCK_SECTOR_SIZE);
      memcpy(bounce + sector_ofs, buffer + bytes_written, chunk_size);
      block_write(fs_device, sector_idx, bounce);
    }

    /* Advance. */
    size -= chunk_size;
    offset += chunk_size;
    bytes_written += chunk_size;
  }
  free(bounce);
  //Growth has finished, set flag to false, and release lock
  if (growth_occured)
  {
    inode->is_growing = false;
    lock_release(&inode->growth_lock);
  }
  return bytes_written;
}

//Henry is driving
/* Disables writes to INODE.
   May be called at most once per inode opener. */
void inode_deny_write(struct inode *inode)
{
  inode->deny_write_cnt++;
  ASSERT(inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void inode_allow_write(struct inode *inode)
{
  ASSERT(inode->deny_write_cnt > 0);
  ASSERT(inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t inode_length(const struct inode *inode)
{
  return inode->data.length;
}

/* Return if the inode is being used by process */
bool inode_isopen(const struct inode *inode)
{
  return inode->open_cnt > 2;
}

/* Acquires directory lock */
void inode_acquire_dirlock(const struct inode *inode)
{
  lock_acquire(&inode->dir_lock);
}

/* Releases directory lock */
void inode_release_dirlock(const struct inode *inode)
{
  lock_release(&inode->dir_lock);
}
