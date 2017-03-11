#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "filesys/cache.h"
#include "threads/synch.h"
#include "threads/thread.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  free_map_init ();

  if (format) 
    do_format ();

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  free_map_close ();
  //flush all data 
  buffer_clear();
}


/* Returns read_cnt of fs_device */
unsigned long long
filesys_get_read_cnt (void) {
  return block_get_read_cnt (fs_device);
}

/* Returns write_cnt of fs_device */
unsigned long long
filesys_get_write_cnt (void) {
  return block_get_read_cnt (fs_device);
}


/* Extracts a file name part from *SRCP into PART, and updates *SRCP so that the
next call will return the next file name part. Returns 1 if successful, 0 at
end of string, -1 for a too-long file name part. */
static int
get_next_part (char part[NAME_MAX + 1], const char **srcp) {
  const char *src = *srcp;
  char *dst = part;
  
  /* Skip leading slashes. If it's all slashes, we're done. */
  while (*src == '/')
    src++;
  if (*src == '\0')
    return 0;

  /* Copy up to NAME_MAX character from SRC to DST. Add null terminator. */
  while (*src != '/' && *src != '\0') {
    if (dst < part + NAME_MAX)
      *dst++ = *src;
    else
      return -1;
    src++;
  }
  *dst = '\0';


  /* Advance source pointer. */
  *srcp = src;
  return 1;
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size) 
{
  block_sector_t inode_sector = 0;

  if (!name || !strlen(name))
    return false;

  struct inode *inode = NULL;
  struct dir *dir = NULL;
  char part[NAME_MAX + 1];
  memset (part, 0, NAME_MAX + 1);

  if (name[0] != '/') {
    struct inode *cwd = inode_open (inode_get_inumber (thread_current ()->cwd->inode));
    dir = dir_open (cwd);
  } else {
    dir = dir_open_root ();
  }    

  int ret;
  while ((ret = get_next_part (part, &name)) > 0) {
    if (dir_lookup (dir, part, &inode)) {
      if (inode_get_type (inode)) {
        dir_close (dir);
        dir = dir_open (inode);
        if (!dir) 
          return false;
        continue;
      } else {
        // Found the same name file
        dir_close (dir);
        return false;
      }
    } else {
      if (get_next_part (part, &name) != 0) {
        dir_close (dir);
        return false;
      } else {
        bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size, false)
                  && dir_add (dir, part, inode_sector, false));
        if (!success && inode_sector != 0) 
          free_map_release (inode_sector, 1);
        dir_close (dir);
        return success;
      }
    }
  }
  dir_close (dir);
  return false;
}

bool 
filesys_mkdir (const char *name) {
  block_sector_t inode_sector = 0;

  if (!name || !strlen (name))
    return false;

  if (name[0] == '/' && name[1] == 0)
    return false;

  struct inode *inode = NULL;
  struct dir *dir = NULL;
  char part[NAME_MAX + 1];
  char *next;
  char temp[NAME_MAX + 1];

  if (name[0] != '/') {
    struct inode *cwd = inode_open (inode_get_inumber (thread_current ()->cwd->inode));
    dir = dir_open (cwd);
  } else {
    dir = dir_open_root ();
  }    

  while ((get_next_part (part, &name)) > 0) {
    next = name;
    int ret = get_next_part (temp, &next);
    if (dir_lookup (dir, part, &inode)) {
      if (!ret)
        return false;
      if (inode_get_type (inode)) {
        dir_close (dir);
        dir = dir_open (inode);
        if (!dir) 
          return false;
        continue;
      } else {
        // Found the same name file
        dir_close (dir);
        return false;
      }
    } else {
      if (ret != 0) {
        dir_close (dir);
        return false;
      } else {
        bool success = (dir != NULL
                        && free_map_allocate (1, &inode_sector)
                        && dir_create (inode_sector, 2)
                        && dir_add (dir, part, inode_sector, true));
        if (!success && inode_sector != 0) 
          free_map_release (inode_sector, 1);
        dir_close (dir);
        return success;
      }
    }
  }
  dir_close(dir);
  return false;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  if (!name || !strlen(name))
    return NULL;

  if (name[0] == '/' && name[1] == 0)
    return NULL;

  struct inode *inode = NULL;
  struct dir *dir = NULL;
  char part[NAME_MAX + 1];
  if (name[0] != '/') {
    struct inode *cwd = inode_open (inode_get_inumber (thread_current ()->cwd->inode));
    dir = dir_open (cwd);
  } else {
    dir = dir_open_root ();
  }

  while ((get_next_part (part, &name)) > 0) {
    if (dir_lookup (dir, part, &inode)) {
      if (inode_get_type (inode)) {
        dir_close (dir);
        dir = dir_open (inode);
        if (!dir) 
          return false;
        continue;
      } else {
        char temp[NAME_MAX + 1];
        char *next = name;
        if (get_next_part (temp, &next) == 1) {
          dir_close (dir);
          return false;
        }
        dir_close (dir);
        return file_open (inode);
      }
    } else {
      dir_close (dir);
      return NULL;
    }
  }
  dir_close (dir);
  return NULL;
}

/* Opens the directory with the given NAME.
   Returns the new directory if successful or a null pointer
   otherwise.
   Fails if no directory named NAME exists,
   or if an internal memory allocation fails. */
struct dir *
filesys_open_dir (const char *name)
{
  if (!name || !strlen(name))
    return NULL;

  if (name[0] == '/' && name[1] == 0)
    return dir_open_root ();

  struct inode *inode = NULL;
  struct dir *dir = NULL;
  char part[NAME_MAX + 1];

  if (name[0] != '/') {
    struct inode *cwd = inode_open (inode_get_inumber (thread_current ()->cwd->inode));
    dir = dir_open (cwd);
  } else {
    dir = dir_open_root ();
  }
    
  char *next;
  char temp[NAME_MAX + 1];
  while ((get_next_part (part, &name)) > 0) {
    next = name;
    int ret = get_next_part (temp, &next);
    if (dir_lookup (dir, part, &inode)) {
      if (inode_get_type (inode)) {
        dir_close (dir);
        dir = dir_open (inode);
        if (!dir) return NULL;
        if (!ret)
          return dir;
        else
          continue;
      } else {
        // Found the same name file
        dir_close (dir);
        return NULL;
      }
    } else {
      dir_close (dir);
      return NULL;
    }
  }
  dir_close (dir);
  return NULL;
}


/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
  if (!name || !strlen(name))
    return false;

  if (name[0] == '/' && name[1] == 0)
    return false;

  struct inode *inode = NULL;
  struct dir *dir = NULL;
  char part[NAME_MAX + 1];
  if (name[0] != '/') {
    struct inode *cwd = inode_open (inode_get_inumber (thread_current ()->cwd->inode));
    dir = dir_open (cwd);
  } else {
    dir = dir_open_root ();
  }

  while (get_next_part (part, &name) > 0) {
    if (dir_lookup (dir, part, &inode)) {
      if (inode_get_type (inode)) {
        char temp[NAME_MAX + 1];
        char *next = name;
        int ret = get_next_part (temp, &next);
        if (ret == 0) {
          
          char t[NAME_MAX + 1];
          struct dir *current = dir_open(inode);
          if (inode->sector == ROOT_DIR_SECTOR) {
            dir_close (current);
            dir_close (dir);
            return false;
          } 
          if (dir_readdir (current, t)) {
            dir_close (current);
            dir_close (dir);
            return false;
          } else {
            bool success = dir != NULL && dir_remove (dir, part);
            dir_close (current);
            dir_close (dir); 
            return success;
          }
        } else if (ret == 1) {
          dir_close (dir);
          dir = dir_open (inode);
          if (!dir) 
            return false;
          continue;
        } else {
          dir_close (dir);
          return false;
        }
      } else {
        char temp[NAME_MAX + 1];
        char *next = name;
        if (get_next_part (temp, &next) == 0) {
          bool success = dir != NULL && dir_remove (dir, part);
          dir_close (dir); 
          return success;
        } else {
          dir_close (dir);
          return false;
        }
      }
    } else {
      dir_close (dir);
      return false;
    }
  }
  dir_close (dir);
  return false;

}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}
