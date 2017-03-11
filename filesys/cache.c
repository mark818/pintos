#include "cache.h"
#include "lib/kernel/list.h"

/* Overall number of cache_blocks created */
static int cache_block_count;    

/* Max number of cache_blocks */
const int MAX_CACHE_BLOCKS = 64;     

/* LRU list containing all cache_block */
static struct list LRU;         

/* Lock enforcing consistency of LRU list */
struct lock LRU_lock; 

/* Initializes buffer cache LRU list */
void buffer_init (void) {
	cache_block_count = 0;
	list_init (&LRU);
	lock_init (&LRU_lock);
}

/* Check if sector_index requested is valid */
static inline bool buffer_check_sector_index (struct block *fs_device, block_sector_t id) {
	return id < block_size (fs_device);
}

/* Write sector back to block device */
static inline void buffer_flush (struct block *fs_device, cache_block *blk, block_sector_t id) {
	block_write (fs_device, id, blk->data);
}

/* Decide a cache entry to evict in LRU list */
static cache_block *buffer_find_evict (void) {
	struct list_elem *el;
	cache_block *blk = NULL;
	lock_acquire (&LRU_lock);
	for (el = list_back (&LRU); el != list_head (&LRU); el = list_prev (el)) {
		blk = list_entry (el, cache_block, elem);
		if (!(blk->exclude_active + blk->exclude_wait))
			goto found;
	}
	blk = list_entry (list_back (&LRU), cache_block, elem);
found:
	lock_acquire (&blk->lock_cache);
	blk->exclude_wait++;
	lock_release (&blk->lock_cache);
	lock_release (&LRU_lock);
	return blk;
}

/* Create a new cache_block from buf and replace an old 
   entry in LRU. Evict if possible */
static cache_block *buffer_update_old (struct block *fs_device, block_sector_t id, uint8_t *buf) {
	cache_block *blk = buffer_find_evict ();
	lock_acquire (&blk->lock_cache);
	while (blk->share_active + blk->exclude_active) {
		cond_wait (&blk->exclude_cond, &blk->lock_cache);
	}
	block_sector_t old_id = blk->sector_index;
	blk->exclude_wait--;
	blk->exclude_active++;
	blk->sector_index = id;
	lock_release (&blk->lock_cache);

	if (blk->dirty) {
		buffer_flush (fs_device, blk, old_id);
	}
	if (buf) {
		memcpy (blk->data, buf, BLOCK_SECTOR_SIZE);
	}
	
	free (buf);	
	blk->dirty = false;
	
	if (cache_block_count > 1) {
		lock_acquire (&LRU_lock);
		list_remove (&blk->elem);
		list_push_front (&LRU, &blk->elem);
		lock_release (&LRU_lock);
	}

	lock_acquire (&blk->lock_cache);
    blk->exclude_active--;
	if (blk->exclude_wait){ 
		cond_signal (&blk->exclude_cond, &blk->lock_cache);
	} else if (blk->share_wait) {
		cond_broadcast (&blk->share_cond, &blk->lock_cache);
	}
	blk->share_wait = blk->share_active = blk->exclude_wait = blk->exclude_active = 0;
	lock_release (&blk->lock_cache);
	return blk;
}

/* Create a new cache_block from buf and insert to LRU */
static cache_block *buffer_create_block (struct block *fs_device, block_sector_t id, uint8_t *buf) {
	if (cache_block_count >= MAX_CACHE_BLOCKS) 
		return buffer_update_old (fs_device, id, 0);
	cache_block *blk = calloc (1, sizeof (cache_block));
	if (buf) {
		memcpy (blk->data, buf, BLOCK_SECTOR_SIZE);
		free (buf);
	}
	blk->sector_index = id;
	blk->dirty = false;
	blk->share_wait = blk->share_active = blk->exclude_wait = blk->exclude_active = 0;
	cond_init (&blk->share_cond);
	cond_init (&blk->exclude_cond);
	lock_init (&blk->lock_cache);

	bool duplicate = false;
	lock_acquire (&LRU_lock);
	
	struct list_elem *el;
	for (el = list_begin (&LRU); el != list_end (&LRU); el = list_next (el)) {
		cache_block *block = list_entry (el, cache_block, elem);
		if (block->sector_index == id) {
            duplicate = true;
            break;
		}
	}

	if (!duplicate) {
		list_push_front (&LRU, &blk->elem);
	}
	
	lock_release (&LRU_lock);
	cache_block_count++;
	return blk;
}

/* Import block from block device */
static cache_block *buffer_import_block (struct block *fs_device, block_sector_t id) {
	if (!buffer_check_sector_index (fs_device, id)) {
		return NULL;
	}
	uint8_t *buf = malloc (BLOCK_SECTOR_SIZE);
	if (!buf) {
		return NULL;
	}
	block_read (fs_device, id, (void*) buf);
	if (cache_block_count < MAX_CACHE_BLOCKS) {
		return buffer_create_block (fs_device, id, buf);
	} else {
		return buffer_update_old (fs_device, id, buf);
	}
}

/* Return cache_block requested, fetch from disk
   if necessary. Upon accessing, the cache_block
   is placed to the top of the LRU list */
static cache_block *buffer_get_block (struct block *fs_device, block_sector_t id) {
	struct list_elem *el;
	lock_acquire(&LRU_lock);
	for (el = list_begin (&LRU); el != list_end (&LRU); el = list_next (el)) {
		cache_block *block = list_entry (el, cache_block, elem);
		if (block->sector_index == id) {
			if (cache_block_count > 1) {
				list_remove (&block->elem);
				list_push_front (&LRU, &block->elem);
			}
            lock_release (&LRU_lock);
			return block;
		}
	}
	lock_release(&LRU_lock);
	return buffer_import_block (fs_device, id);
}

/* Helper function to perform read, double checking sector number */
static bool _read (cache_block *blk, block_sector_t id, void *buf) {
	bool status = true;
	lock_acquire (&blk->lock_cache);
	while (blk->exclude_wait + blk->exclude_active) {
		blk->share_wait++;
		cond_wait (&blk->exclude_cond, &blk->lock_cache);
		blk->share_wait--;
	}
	blk->share_active++;
	lock_release (&blk->lock_cache);

	if (blk->sector_index != id)
		status = false;
	memcpy (buf, blk->data, BLOCK_SECTOR_SIZE);
	
	if (cache_block_count > 1) {
		lock_acquire (&LRU_lock);
		list_remove (&blk->elem);
		list_push_front (&LRU, &blk->elem);
		lock_release (&LRU_lock);
	}

	lock_acquire (&blk->lock_cache);
    blk->share_active--;
	if (blk->share_active == 0 && blk->exclude_wait > 0)
		cond_signal (&blk->exclude_cond, &blk->lock_cache);
	lock_release (&blk->lock_cache);
	return status;
}

/* Generic interface to read a block */
bool buffer_read (struct block *fs_device, block_sector_t id, void *buf) {
	cache_block *blk = buffer_get_block (fs_device, id);
	if (!blk)
		return false;
	while (!_read (blk, id, buf)) {
		blk = buffer_get_block (fs_device, id);
	}
	return true;
}

/* Helper function to perform write, double checking sector number */
static bool _write (cache_block* blk, block_sector_t id, void *buf) {
	bool status = true;
	lock_acquire (&blk->lock_cache);
	while (blk->exclude_wait + blk->exclude_active) {
		blk->share_wait++;
		cond_wait (&blk->exclude_cond, &blk->lock_cache);
		blk->share_wait--;
	}
	blk->share_active++;
	lock_release (&blk->lock_cache);

	if (blk->sector_index != id)
		status = false;
	memcpy (blk->data, buf, BLOCK_SECTOR_SIZE);
	blk->dirty = true;

	if (cache_block_count > 1) {
		lock_acquire (&LRU_lock);
		list_remove (&blk->elem);
		list_push_front (&LRU, &blk->elem);
		lock_release (&LRU_lock);
	}

	lock_acquire (&blk->lock_cache);
    blk->share_active--;
	if (blk->share_active == 0 && blk->exclude_wait > 0)
		cond_signal (&blk->exclude_cond, &blk->lock_cache);
	lock_release (&blk->lock_cache);
	return status;
}

/* Generic interface to read a block */
bool buffer_write (struct block *fs_device, block_sector_t id, void *buf) {
	cache_block *blk = buffer_get_block (fs_device, id);
	if (!blk)
		return false;
	while (!_write (blk, id, buf)) {
		blk = buffer_get_block (fs_device, id);
	}
	return true;
}

/* Evict all cache entries */
void buffer_clear (void) {
	struct list_elem *el;
	lock_acquire (&LRU_lock);
	cache_block *blk;
	for (el = list_begin (&LRU); el != list_end (&LRU); el = list_next (el)) {
		blk = list_entry (el, cache_block, elem);
		lock_acquire (&blk->lock_cache);
		while (blk->exclude_wait + blk->exclude_active) {
			blk->share_wait++;
			cond_wait (&blk->exclude_cond, &blk->lock_cache);
			blk->share_wait--;
		}
		blk->share_active++;
		lock_release (&blk->lock_cache);

		if (blk->dirty) {
			buffer_flush (fs_device, blk, blk->sector_index);
			blk->dirty = false;
		}
		memset (blk->data, 0, BLOCK_SECTOR_SIZE);
		blk->sector_index = -1;

		lock_acquire (&blk->lock_cache);
	    blk->share_active--;
		if (blk->share_active == 0 && blk->exclude_wait > 0)
			cond_signal (&blk->exclude_cond, &blk->lock_cache);
		lock_release (&blk->lock_cache);
	}
	lock_release (&LRU_lock); 
}