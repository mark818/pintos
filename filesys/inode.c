#include "filesys/inode.h"

#include <debug.h>
#include <round.h>
#include <string.h>
#include <stdio.h>
#include "filesys.h"
#include "free-map.h"
#include "cache.h"
#include "threads/malloc.h"



/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline uint32_t num_of_direct_block (uint32_t size)
{
  if (size < DIRECT_BOUND) {
    return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
  } else {
    return DIRECT_BLOCK_NUMBER;
  }
}
/* Returns the number of singly indirect block number for a file 
   SIZE bytes long. */
static inline uint32_t num_of_single_indirect_block (uint32_t size)
{
  if (size <= DIRECT_BOUND) {
    return 0;
  } else if (size < SINGLE_INDIRECT_BOUND) {
    return DIV_ROUND_UP (size - DIRECT_BOUND, BLOCK_SECTOR_SIZE*126);
  } else {
    return SINGLE_INDIRECT_NUMBER;
  }
}
/* Returns the number of doubly indirect block number for a file 
   SIZE bytes long. */
static inline uint32_t num_of_double_indirect_block (uint32_t size)
{ 
  if (size <= SINGLE_INDIRECT_BOUND) {
    return 0;
  } else {
    return DIV_ROUND_UP (size - SINGLE_INDIRECT_BOUND, BLOCK_SECTOR_SIZE*126*126);
  } 
}

/* Returns true if the INODE is a directory and false if the INODe 
   is a file. */
inline bool 
inode_get_type (const struct inode *inode)
{
  ASSERT (inode != NULL);
  return inode->is_dir;
}

/* Returns BASE to the power of EXPO. */
static uint32_t 
power(uint32_t base, uint32_t expo) 
{
  uint32_t i;
  uint32_t rtn = 1;
  for (i = 0; i < expo; i += 1) {
    rtn = rtn * base;
  }
  return rtn;
}

/* Return the block device sector that contains byte offset 
   BYTE_NUMBER with IBLOCK.*/
static block_sector_t sector_of_byte(struct indirect_block* iblock, uint32_t byte_number) {
  if (iblock->indirection_level > 0) {
    uint32_t index = byte_number / ((power(126,iblock->indirection_level)) * BLOCK_SECTOR_SIZE);
    struct indirect_block* next_iblock = malloc(BLOCK_SECTOR_SIZE);
    buffer_read(fs_device, iblock->block_pointers[index], next_iblock);
    block_sector_t return_value = sector_of_byte(next_iblock, byte_number % (power(126,iblock->indirection_level) * BLOCK_SECTOR_SIZE));
    free(next_iblock);
    return return_value;
  }
  return iblock->block_pointers[byte_number >> 9];
}

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, uint32_t pos) 
{
  ASSERT (inode != NULL);

  struct inode_disk* disk_inode = malloc(BLOCK_SECTOR_SIZE);
  buffer_read(fs_device, inode->sector, disk_inode);
  block_sector_t return_value;
 
  
  if (pos < DIRECT_BOUND) {
    uint32_t index = pos / BLOCK_SECTOR_SIZE;
    return_value = disk_inode->direct[index];
    free(disk_inode);
    return return_value;
  } else if (pos < SINGLE_INDIRECT_BOUND) {
    uint32_t index = (pos - DIRECT_BOUND) / (BLOCK_SECTOR_SIZE * 126);
    struct indirect_block* next_iblock = malloc(BLOCK_SECTOR_SIZE);
    buffer_read(fs_device, disk_inode->single_indirect[index], next_iblock);
    return_value = sector_of_byte(next_iblock, (pos - DIRECT_BOUND) % (BLOCK_SECTOR_SIZE * 126));
    free(next_iblock);
    free(disk_inode);
    return return_value;
  } else {
    uint32_t index = (pos - SINGLE_INDIRECT_BOUND) / (BLOCK_SECTOR_SIZE * 126 * 126);
    struct indirect_block* next_iblock = malloc(BLOCK_SECTOR_SIZE);
    buffer_read(fs_device, disk_inode->double_indirect[index], next_iblock);
    return_value = sector_of_byte(next_iblock, (pos - SINGLE_INDIRECT_BOUND) % (BLOCK_SECTOR_SIZE * 126 * 126));
    free(next_iblock);
    free(disk_inode);
    return return_value;
  }
  
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Lock for accessing the open_inodes list. */
static struct lock inode_list_lock;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
  lock_init (&inode_list_lock);
  buffer_init ();
}
/* Initialize or assign more sectors to the disk_inode. */
void
inode_extend (struct inode_disk* disk_inode, uint32_t direct_block_num, 
                    uint32_t single_indirect_block_num, uint32_t double_indirect_block_num,
                    uint32_t direct_block_num_old, uint32_t single_indirect_block_num_old, 
                    uint32_t double_indirect_block_num_old) 
{
  static char zeros[BLOCK_SECTOR_SIZE];
  uint32_t i;
  for (i = direct_block_num_old; i < direct_block_num; i++) {
    free_map_allocate(1, &(disk_inode->direct[i]));
    buffer_write (fs_device, disk_inode->direct[i], zeros);
  }
  struct indirect_block* indir_block = malloc(BLOCK_SECTOR_SIZE);
  for (i = single_indirect_block_num_old; i < single_indirect_block_num; i++) {
    free_map_allocate(1, &(disk_inode->single_indirect[i]));
    indir_block->block = disk_inode->single_indirect[i];
    indir_block->indirection_level = 0;
    int j;
    for (j = 0; j < 126; j++) {
      free_map_allocate(1, &(indir_block->block_pointers[j]));
      buffer_write (fs_device, indir_block->block_pointers[j], zeros);
    }
    buffer_write (fs_device, disk_inode->single_indirect[i], indir_block);
  }
  struct indirect_block* double_indir_block = malloc(BLOCK_SECTOR_SIZE);
  for (i = double_indirect_block_num_old; i < double_indirect_block_num; i++) {
    free_map_allocate(1, &(disk_inode->double_indirect[i]));
    double_indir_block->block = disk_inode->double_indirect[i];
    double_indir_block->indirection_level = 1;
    int k;
    for (k = 0; k < 126; k++) {
      free_map_allocate(1, &(double_indir_block->block_pointers[k]));
      indir_block->block = double_indir_block->block_pointers[k];
      indir_block->indirection_level = 0;
      int j;
      for (j = 0; j < 126; j++) {
        free_map_allocate(1, &(indir_block->block_pointers[j]));
        buffer_write (fs_device, indir_block->block_pointers[j], zeros);
      }
      buffer_write (fs_device, double_indir_block->block_pointers[k], indir_block);
    }
    buffer_write (fs_device, disk_inode->double_indirect[i], double_indir_block);
  }

  free(indir_block);
  free(double_indir_block);

}


/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, uint32_t length, bool is_dir)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);
  ASSERT (length <= MAX_FILE_SIZE);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL) {
    uint32_t direct_block_num = num_of_direct_block (length);
    uint32_t single_indirect_block_num = num_of_single_indirect_block (length);
    uint32_t double_indirect_block_num = num_of_double_indirect_block (length);
    disk_inode->length = length;
    disk_inode->magic = INODE_MAGIC;
    disk_inode->is_dir = is_dir;

    inode_extend (disk_inode, direct_block_num, single_indirect_block_num, 
                  double_indirect_block_num, 0, 0, 0);

    buffer_write(fs_device, sector, disk_inode);

    free (disk_inode);
    success = true;
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
  lock_acquire (&inode_list_lock);
  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          if (inode->removed) {
            lock_release (&inode_list_lock);
            return NULL;
          }
          inode_reopen (inode);
          lock_release (&inode_list_lock);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL) {
    lock_release (&inode_list_lock);
    return NULL;
  }

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  lock_init (&(inode->size_lock));
  lock_init (&(inode->dir_lock));
  lock_init (&(inode->inode_lock));
  struct inode_disk* disk_inode = malloc(BLOCK_SECTOR_SIZE);
  buffer_read(fs_device, sector, disk_inode);
  inode->is_dir = disk_inode->is_dir;
  free(disk_inode);
  lock_release (&inode_list_lock);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL) {
    lock_acquire (&inode->inode_lock);
    inode->open_cnt++;
    lock_release (&inode->inode_lock);
  }
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  lock_acquire(&inode_list_lock);
  lock_acquire(&inode->inode_lock);
  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
      lock_release (&inode_list_lock);
      /* Deallocate blocks if removed. */
      if (inode->removed) {
        struct inode_disk* disk_inode = malloc(BLOCK_SECTOR_SIZE);
        buffer_read(fs_device, inode->sector, disk_inode);
        uint32_t length = disk_inode->length;
        
        uint32_t direct_block_num = num_of_direct_block (length);
        uint32_t single_indirect_block_num = num_of_single_indirect_block (length);
        uint32_t double_indirect_block_num = num_of_double_indirect_block (length);

        uint32_t i;
        for (i = 0; i < direct_block_num; i++) {
          free_map_release(disk_inode->direct[i], 1);
        }
        struct indirect_block *indir_block = malloc(BLOCK_SECTOR_SIZE);
        for (i = 0; i < single_indirect_block_num; i++) {
          buffer_read (fs_device, disk_inode->single_indirect[i], indir_block);
          int j;
          for (j = 0; j < 126; j++) {
            free_map_release(indir_block->block_pointers[j], 1);
          }
          free_map_release(disk_inode->single_indirect[i], 1);
        }
        struct indirect_block *double_indir_block = malloc(BLOCK_SECTOR_SIZE);
        for (i = 0; i < double_indirect_block_num; i++) {
          buffer_read (fs_device, disk_inode->double_indirect[i], double_indir_block);
          int k;
          for (k = 0; k < 126; k++) {
            buffer_read (fs_device, double_indir_block->block_pointers[k], indir_block);
            int j;
            for (j = 0; j < 126; j++) {
              free_map_release(indir_block->block_pointers[j], 1);
            }
            free_map_release(double_indir_block->block_pointers[k], 1);
          }
          free_map_release(disk_inode->double_indirect[i], 1);
        }
        free_map_release(inode->sector, 1);
        free(disk_inode);
        free(indir_block);
        free(double_indir_block);
      }
      lock_release (&inode->inode_lock);
      free (inode); 
    } else {
      lock_release (&inode_list_lock);
      lock_release (&inode->inode_lock);
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  lock_acquire (&inode->inode_lock);
  inode->removed = true;
  lock_release (&inode->inode_lock);
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
uint32_t
inode_read_at (struct inode *inode, void *buffer_, uint32_t size, uint32_t offset) 
{
  uint8_t *buffer = buffer_;
  uint32_t bytes_read = 0;
  uint8_t *bounce = NULL;

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      uint32_t sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      uint32_t inode_left = inode_length (inode) - offset;
      uint32_t sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      uint32_t min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      uint32_t chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Read full sector directly into caller's buffer. */
          buffer_read (fs_device, sector_idx, buffer + bytes_read);
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
          buffer_read (fs_device, sector_idx, bounce);
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
uint32_t
inode_write_at (struct inode *inode, const void *buffer_, uint32_t size,
                uint32_t offset) 
{
  const uint8_t *buffer = buffer_;
  uint32_t bytes_written = 0;
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;


  uint32_t length_inode = inode_length (inode);
  uint32_t old_length = length_inode;
  struct inode_disk* disk_inode = malloc (BLOCK_SECTOR_SIZE);
  buffer_read (fs_device, inode->sector, disk_inode);
  if ((size + offset) > length_inode) {
    lock_acquire (&inode->size_lock);
    length_inode = inode_length (inode);
    old_length = length_inode;
    if ((size + offset) <= length_inode) {
      lock_release(&inode->size_lock);
    } else {
      uint32_t direct_block_num = num_of_direct_block (size + offset);
      uint32_t single_indirect_block_num = num_of_single_indirect_block (size + offset);
      uint32_t double_indirect_block_num = num_of_double_indirect_block (size + offset);
      uint32_t direct_block_num_old = num_of_direct_block (length_inode);
      uint32_t single_indirect_block_num_old = num_of_single_indirect_block (length_inode);
      uint32_t double_indirect_block_num_old = num_of_double_indirect_block (length_inode);

      inode_extend (disk_inode, direct_block_num, single_indirect_block_num, 
                    double_indirect_block_num, direct_block_num_old, 
                    single_indirect_block_num_old, double_indirect_block_num_old);
      buffer_write (fs_device, inode->sector, disk_inode);
      length_inode = size + offset;
    }
  }


  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      uint32_t sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      uint32_t inode_left = length_inode - offset;
      uint32_t sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      uint32_t min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      uint32_t chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Write full sector directly to disk. */
          buffer_write (fs_device, sector_idx, buffer + bytes_written);
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
            buffer_read (fs_device, sector_idx, bounce);
          else
            memset (bounce, 0, BLOCK_SECTOR_SIZE);
          memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
          buffer_write (fs_device, sector_idx, bounce);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  disk_inode->length = length_inode;
  if (old_length < length_inode) {
    buffer_write (fs_device, inode->sector, disk_inode);
    lock_release (&inode->size_lock);
  }
  free (disk_inode);
  free (bounce);
  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  lock_acquire (&inode->inode_lock);
  inode->deny_write_cnt++;
  lock_release (&inode->inode_lock);
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
  lock_acquire (&inode->inode_lock);
  inode->deny_write_cnt--;
  lock_release (&inode->inode_lock);

}

/* Returns the length, in bytes, of INODE's data. */
uint32_t
inode_length (const struct inode *inode)
{
  struct inode_disk* disk_inode = malloc (BLOCK_SECTOR_SIZE);
  buffer_read (fs_device, inode->sector, disk_inode);
  uint32_t length = disk_inode->length;
  free (disk_inode);
  return length;
}