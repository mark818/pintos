#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include <list.h>
#include "filesys/off_t.h"
#include "devices/block.h"
#include "filesys/inode.h"
#include "threads/synch.h"

#define INODE_MAGIC 0x494e4f44
#define DIRECT_BLOCK_NUMBER 50
#define SINGLE_INDIRECT_NUMBER 74
#define DOUBLE_INDIRECT_NUMBER 1

static const uint32_t MAX_FILE_SIZE = DIRECT_BLOCK_NUMBER * BLOCK_SECTOR_SIZE 
                             + SINGLE_INDIRECT_NUMBER * BLOCK_SECTOR_SIZE * 126
                             + DIRECT_BLOCK_NUMBER * BLOCK_SECTOR_SIZE * 126 * 126; 
static const uint32_t DIRECT_BOUND = DIRECT_BLOCK_NUMBER * BLOCK_SECTOR_SIZE;
static const uint32_t SINGLE_INDIRECT_BOUND = DIRECT_BLOCK_NUMBER * BLOCK_SECTOR_SIZE 
                                         + SINGLE_INDIRECT_NUMBER * BLOCK_SECTOR_SIZE * 126;

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk {
    uint32_t length;                                            /* File size in bytes. */
    unsigned magic;                                             /* Magic number. */
    bool is_dir;
    block_sector_t direct[DIRECT_BLOCK_NUMBER];                 /* Direct blocks. */
    block_sector_t single_indirect[SINGLE_INDIRECT_NUMBER];     /* Singly indirect blocks. */ 
    block_sector_t double_indirect[DOUBLE_INDIRECT_NUMBER];     /* Doubly indirect blocks. */
};

struct indirect_block {
  block_sector_t block;                  /* block_sector_t of this indirect block. */
  uint32_t indirection_level;            /* 0 for singly indirect, 1 for doubly indirect, etc. */
  block_sector_t block_pointers[126];    /* sectors numbers of the blocks this indirect_block points to. */

};

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;     /* Element in inode list. */
    block_sector_t sector;     /* Sector number of disk location. */
    int open_cnt;              /* Number of openers. */
    bool removed;              /* True if deleted, false otherwise. */
    int deny_write_cnt;        /* 0: writes ok, >0: deny writes. */
    struct lock size_lock;     /* This lock is for file extension. */
    bool is_dir;               /* True if this inode represents a directory*/
    struct lock dir_lock;      /* Lock to control directory access*/ 
    struct lock inode_lock;    /* Lock to protect open_cnt, removed, deny_write_cnt. */        
  };

struct bitmap;

void inode_init (void);
bool inode_create (block_sector_t, uint32_t, bool);
struct inode *inode_open (block_sector_t);
struct inode *inode_reopen (struct inode *);
block_sector_t inode_get_inumber (const struct inode *);
void inode_close (struct inode *);
void inode_remove (struct inode *);
uint32_t inode_read_at (struct inode *, void *, uint32_t size, uint32_t offset);
uint32_t inode_write_at (struct inode *, const void *, uint32_t size, uint32_t offset);
void inode_deny_write (struct inode *);
void inode_allow_write (struct inode *);
uint32_t inode_length (const struct inode *);

#endif /* filesys/inode.h */