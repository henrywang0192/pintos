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
bool inode_create (block_sector_t, off_t, bool);
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
bool inode_isdir (const struct inode *);
bool inode_isopen(const struct inode *);
void grow_file(const struct inode *, off_t);
void inode_acquire_dirlock(const struct inode *);
void inode_release_dirlock(const struct inode *);
#endif /* filesys/inode.h */
