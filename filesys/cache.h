#ifndef CACHE_H
#define CACHE_H

#include <stdbool.h>
#include "devices/block.h"
#include "directory.h"
#include "devices/block.h"
#include "filesys.h"
#include "inode.h"
#include "free-map.h"
#include "threads/malloc.h"
#include "lib/string.h"

typedef struct {
  uint8_t data[BLOCK_SECTOR_SIZE];  /* Storage for actual block in disk */
  block_sector_t file_start;   		/* Disk inode position on disk. */
  block_sector_t sector_index;  	/* Identify block’s location on disk */
  bool dirty;                  		/* For write-back check */
  struct list_elem elem;       		/* List_elem in LRU list */
  short share_wait;					/* Number of shared accesses */
  short share_active;				/* Number of shared accesse waiters */
  short exclude_wait;				/* Number of exlusive accesses */
  short exclude_active;				/* Number of exlusive accesse waiters */
  struct condition share_cond;		/* Condition varibale for shared access */  
  struct condition exclude_cond;	/* Condition varibale for exclusive access */
  struct lock lock_cache;      		/* To synchronize dirty and ref_cnt */
} cache_block;

void buffer_init (void);

/* buffer read and wrote */
bool buffer_read (struct block *fs_device, block_sector_t id, void *buffer);
bool buffer_write (struct block *fs_device, block_sector_t id, void *buffer);

/* Reset buffer */
void buffer_clear (void);

#endif