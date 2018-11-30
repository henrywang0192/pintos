#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "devices/block.h"
#include <list.h>

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* Amount of direct data blocks in an inode */
#define DIRECT_BLOCKS 10

/* Amount of sectors an indirection block contains */
#define SECTORS_PER_INDIRECTION_BLOCK BLOCK_SECTOR_SIZE / sizeof(block_sector_t)

#define NOT_PRESENT SECTORS_PER_INDIRECTION_BLOCK + 1

struct bitmap;

void inode_init (void);
bool inode_create (block_sector_t, off_t);
struct inode *inode_open (block_sector_t);
struct inode *inode_reopen (struct inode *);
block_sector_t inode_get_inumber (const struct inode *);
void inode_close (struct inode *);
void inode_remove (struct inode *);
off_t inode_read_at (struct inode *, void *, off_t size, off_t offset);
off_t inode_write_at (struct inode *, const void *, off_t size, off_t offset);
void inode_deny_write (struct inode *);
void inode_allow_write (struct inode *);
off_t inode_length (const struct inode *);
void grow_file(const struct inode *, off_t);

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
    //struct inode_details details;

    uint32_t unused[114];                 /* Not used. */
  };  

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    bool is_directory;                  /* Denote if this inode is a directory*/
    struct inode_disk data;             /* Inode content. */
  };

#endif /* filesys/inode.h */
