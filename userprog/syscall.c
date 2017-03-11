#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "lib/syscall-nr.h"
#include "user/syscall.h"
#include "devices/shutdown.h"
#include "userprog/process.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "devices/input.h"
#include "filesys/cache.h"

static void syscall_handler (struct intr_frame *);

static bool put_user (uint8_t *udst, uint8_t byte);
static int get_argc (int syscall_num);
static int get_user (const uint8_t *uaddr);
static bool usermem_read (uint8_t *udst, int size_byte);
static bool usermem_write (uint8_t *udst, uint8_t *src, int size_byte);

static void syscall_halt (void);
void syscall_exit (int status);
static pid_t syscall_exec (const char *file);
static int syscall_wait (pid_t pid);
static bool syscall_create (const char *file, unsigned initial_size);
static bool syscall_remove (const char *file);
static int syscall_open (const char *file);
static int syscall_filesize (int fd);
static int syscall_read (int fd, void *buffer, unsigned length);
static int syscall_write (int fd, const void *buffer, unsigned length);
static void syscall_seek (int fd, unsigned position);
static unsigned syscall_tell (int fd);
static void syscall_close (int fd);
static int syscall_practice (int i);

static bool syscall_chdir (const char *dir);
static bool syscall_mkdir (const char *dir);
static bool syscall_readdir (int fd, char *name);
static bool syscall_isdir (int fd);
static int syscall_inumber (int fd);

static unsigned long long syscall_get_block_read_cnt (void);
static unsigned long long syscall_get_block_write_cnt (void);
static void syscall_buffer_clear (void);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/* Reads a byte at user virtual address UADDR.
   UADDR must be below PHYS_BASE.
   Returns the byte value if successful, -1 if a segfault
   occurred. */
static int
get_user (const uint8_t *uaddr)
{
  int result;
  asm ("movl $1f, %0; movzbl %1, %0; 1:"
    : "=&a" (result) : "m" (*uaddr));
  return result;
}

/* Writes BYTE to user address UDST.
   UDST must be below PHYS_BASE.
   Returns true if successful, false if a segfault occurred. */
static bool
put_user (uint8_t *udst, uint8_t byte)
{
  int error_code;
  asm ("movl $1f, %0; movb %b2, %1; 1:"
    : "=&a" (error_code), "=m" (*udst) : "q" (byte));
  return error_code != -1;
}

/* Helper function read for all system calls. */
static bool
usermem_read (uint8_t *udst, int size_byte)
{
  int i = 0;
  if (!udst)
    return false;
  if (size_byte == -1) {
    int temp;
    do {
      if ((udst + i) >= (uint8_t*) PHYS_BASE) {
        return false;
      } else {
        if ((temp = get_user (udst + i)) == -1) {
          return false;
        }
      }
      i++;
    } while (temp);
    return true;
  } else {
    for (i = 0; i < size_byte; ++i) {
      if ((udst + i) >= (uint8_t*) PHYS_BASE) {
        return false;
      } else {
        if (get_user (udst + i) == -1) {
          return false;
        }
      }
    }
    return true;
  }
}

static bool 
usermem_write (uint8_t *udst, uint8_t *src, int size_byte) 
{
  int i = 0;
  if ((udst + size_byte) >= (uint8_t*) PHYS_BASE) {
    return false;
  }
  for (i = 0; i < size_byte; ++i) {
    if (put_user((udst + i), *(src + i)) == false) {
      return false;
    }
  }
  return true;
}

static int 
get_argc (int syscall_num)
{
  switch (syscall_num) {
    case SYS_HALT:  case SYS_GET_BLOCK_READ_CNT:
    case SYS_GET_BLOCK_WRITE_CNT:
    case SYS_BUFFER_CLEAR:         return 0;

    case SYS_EXIT: 	case SYS_WAIT: 
    case SYS_PRACTICE: case SYS_EXEC:
    case SYS_REMOVE: case SYS_OPEN: 
    case SYS_FILESIZE: case SYS_TELL:
    case SYS_CLOSE: case SYS_CHDIR:
    case SYS_MKDIR: case SYS_ISDIR:
    case SYS_INUMBER:                  return 1;

    case SYS_CREATE: case SYS_SEEK:
    case SYS_READDIR:                  return 2;
    case SYS_READ: case SYS_WRITE:     return 3;
    default:                           return -1;
  }
}

static void
syscall_handler (struct intr_frame *f) 
{
  uint32_t* args = ((uint32_t*) f->esp);
  int syscall_num;
  if (!usermem_read ((uint8_t*) args, 4)) {
    syscall_exit (-1);
  }
  syscall_num = *args;
  int argc = get_argc (syscall_num);
  if (argc == -1) {
    syscall_exit (-1);
  } else {
    int i;
    for (i = 0; i < argc; ++i) {
      if (!usermem_read ((uint8_t*) args + 4 * (i + 1), 4)) {
        syscall_exit (-1);
      }
    }
    switch(syscall_num) {
      case SYS_HALT:
        syscall_halt ();
        break;
      case SYS_EXIT:
        syscall_exit ((int) *(args + 1));
        break;
      case SYS_WAIT:
        f->eax = 
          (uint32_t) syscall_wait ((pid_t) *(args + 1));
        break;
      case SYS_PRACTICE:
        f->eax =
          (uint32_t) syscall_practice ((int) *(args + 1));
        break; 
      case SYS_EXEC:
        f->eax = (uint32_t) syscall_exec ((const char *) *(args + 1));
        break;
      case SYS_REMOVE:
        f->eax = (uint32_t) syscall_remove ((const char *) *(args + 1));
        break;
      case SYS_OPEN:
        f->eax = (uint32_t) syscall_open ((const char *) *(args + 1));
        break;
      case SYS_FILESIZE:
        f->eax = (uint32_t) syscall_filesize ((int) *(args + 1));
        break; 
      case SYS_TELL:
        f->eax = (uint32_t) syscall_tell ((int) *(args + 1));
        break;
      case SYS_CLOSE:
        syscall_close ((int) *(args + 1));
        break;
      case SYS_CREATE:
        f->eax = (uint32_t) syscall_create ((const char *) *(args + 1),
          (unsigned) *(args + 2));
        break;
      case SYS_SEEK:
        syscall_seek ((int) *(args + 1), (unsigned) *(args + 2));
        break;
      case SYS_READ:
        f->eax = (uint32_t) syscall_read ((int) *(args + 1),
          (void*) *(args + 2), (unsigned) *(args + 3));
        break;
      case SYS_WRITE:
        f->eax = (uint32_t) syscall_write ((int) *(args + 1),
          (const void *) *(args + 2), (unsigned) *(args + 3));
        break;
      case SYS_CHDIR:
        f->eax = (uint32_t) syscall_chdir ((const char *) *(args + 1));
        break;
      case SYS_MKDIR:
        f->eax = (uint32_t) syscall_mkdir ((const char *) *(args + 1));
        break;
      case SYS_READDIR:
        f->eax = (uint32_t) syscall_readdir ((int) *(args + 1), (const char *) *(args + 2));
        break;
      case SYS_ISDIR:
        f->eax = (uint32_t) syscall_isdir ((int) *(args + 1));
        break;
      case SYS_INUMBER:
        f->eax = (uint32_t) syscall_inumber ((int) *(args + 1));
        break;
      case SYS_GET_BLOCK_READ_CNT:
        f->eax = (uint32_t) syscall_get_block_read_cnt ();
        break;
      case SYS_GET_BLOCK_WRITE_CNT:
        f->eax = (uint32_t) syscall_get_block_write_cnt ();
        break;
      case SYS_BUFFER_CLEAR:
        syscall_buffer_clear ();
        break;
      default: syscall_exit (-1);
    }
  }
  return;
}

static void 
syscall_halt (void) 
{
  shutdown_power_off ();
}

void 
syscall_exit (int status)
{
  struct thread *cur = thread_current ();
  struct wait_status *ws = cur->wait_status;
  ws->exit_status = status;
  lock_acquire (&(ws->ref_lock));
  ws->ref_count--;
  if (ws->ref_count == 0) {
    lock_release (&(ws->ref_lock));
    free (ws);
  } else {
    lock_release (&(ws->ref_lock));
  }

  struct list_elem *e = list_begin (&(cur->wait_status_list));
  while (e != list_end (&(cur->wait_status_list))) {
    struct wait_status *child_status = list_entry (e, struct wait_status, child);
    e = list_next (e);
    lock_acquire (&(child_status->ref_lock));
    child_status->ref_count--;
    if (child_status->ref_count == 0) {
      lock_release (&(child_status->ref_lock));
      free (child_status);
    } else {
      lock_release (&(child_status->ref_lock));
    }
  }

  e = list_begin (&(cur->open_files));
  while (e != list_end (&(cur->open_files))) {
    struct fd *f = list_entry (e, struct fd, elem);
    e = list_next (e);
    if (f->is_dir) {
      dir_close (f->dir);
    } else {
      file_close (f->file);
    }
    free (f);
  }

  sema_up(&(ws->parent_wait));
  char name[16];
  strlcpy(name, thread_current ()->name, 16);
  char *token, *save_ptr;
  token = strtok_r (name, " ", &save_ptr);
  if (thread_current ()->exe) {
    file_allow_write (thread_current ()->exe);
    file_close(thread_current ()->exe);
  }
  printf ("%s: exit(%d)\n", token, status);
  thread_exit();
}

static pid_t 
syscall_exec (const char *file)
{
  if (!usermem_read ((uint8_t *)file, -1)) {
    syscall_exit (-1);
  }
  return process_execute (file);
}

static int 
syscall_wait (pid_t pid)
{
  return process_wait ((tid_t) pid);
}

static bool 
syscall_create (const char *file, unsigned initial_size)
{
  if (!usermem_read ((uint8_t *)file, -1)) {
    syscall_exit (-1);
  }
  bool result = filesys_create (file, initial_size);
  return result;
}

static bool 
syscall_remove (const char *file)
{
  if (!usermem_read ((uint8_t *)file, -1)) {
    syscall_exit (-1);
  }
  bool result = filesys_remove (file);
  return result;
}

static int 
syscall_open (const char *file) 
{
  if (!usermem_read ((uint8_t *)file, -1)) {
    syscall_exit (-1);
  }
  struct file *f = filesys_open (file);
  struct dir *d = filesys_open_dir (file);
  struct fd *fd_struct;
  if (f == NULL && d == NULL) {
    return -1;
  } else if (f != NULL) {
    fd_struct = malloc (sizeof(struct fd));
    fd_struct->file = f;
    fd_struct->is_dir = false;
  } else if (d != NULL) {
    fd_struct = malloc (sizeof(struct fd));
    fd_struct->dir = d;
    fd_struct->is_dir = true;
  }
  fd_struct->fd = thread_current ()->next_fd;
  (thread_current ()->next_fd)++;
  list_push_back (&(thread_current ()->open_files), &(fd_struct->elem));
  return fd_struct->fd;
}

static struct file *
get_file (int fd) 
{
  struct list_elem *e;
  for (e = list_begin (&(thread_current ()->open_files)); 
       e != list_end (&(thread_current ()->open_files)); e = list_next (e)) 
    { 
      struct fd *current = list_entry (e, struct fd, elem);
      if (current->fd == fd && current->is_dir == false) {
        return current->file;
    }
  }
  return NULL;
}

static int 
syscall_filesize (int fd)
{
  struct file *f;
  if ((f = get_file (fd)) == NULL) {
    return -1;
  }
  return (int) file_length (f);
}

static int 
syscall_read (int fd, void *buffer, unsigned length)
{
  char *buf = malloc (length);
  struct file *f = get_file (fd);
  if (fd == 0) {
    int i = 0;
    for (i = 0; i < length; i++) {
      buf[i] = input_getc ();
    }
    bool success = usermem_write (buffer, buf, length);
    if (success) {
      free (buf);
      return length;
    } else {
      free (buf);
      syscall_exit (-1);
    }
  } else if (f == NULL) {
    free (buf);
    return -1;
  } else {
    off_t len = file_read (f, buf, (off_t)length);
    bool success = usermem_write (buffer, buf, len);
    if (success) {
      free (buf);
      return len;
    } else {
      free (buf);
      syscall_exit (-1);
    }
  }
}

static int 
syscall_write (int fd, const void *buffer, unsigned length)
{
  if (!usermem_read (buffer, length))
    syscall_exit (-1);
  struct file *f = get_file (fd);
  if (fd == 1) {
    putbuf (buffer, length);
    return length;
  } else if (f == NULL) {
    return -1;
  } else {
    off_t len = file_write (f, buffer, (off_t)length);
    return len;
  }
}

static void 
syscall_seek (int fd, unsigned position)
{
  struct file *f;
  if ((f = get_file (fd)))  
    file_seek (f, (off_t)position);
}

static unsigned 
syscall_tell (int fd)
{
  struct file *f;
  if (f = get_file (fd)) {
    return (unsigned)file_tell (f);
  } else {
    return -1;
  }
}

static void 
syscall_close (int fd)
{
  struct list_elem *e;
  for (e = list_begin (&(thread_current ()->open_files)); 
       e != list_end (&(thread_current ()->open_files)); e = list_next (e)) 
    { 
      struct fd *current = list_entry (e, struct fd, elem);
      if (current->fd == fd) {
        list_remove(&(current->elem));
        if (current->is_dir) {
          dir_close (current->dir);
        } else {
          file_close (current->file);
        }
        free (current);
        return;
      }
    }
}

static int 
syscall_practice (int i)
{
  i++;
  return i;
}

/* Changes the current working directory of the process
   to dir, which may be relative or absolute. Returns true
   if successful, false on failure. */
static bool 
syscall_chdir (const char *name)  {
  if (!usermem_read ((uint8_t *)name, -1)) {
    syscall_exit (-1);
  }
  struct dir *old_cwd = thread_current ()->cwd;
  thread_current ()->cwd = filesys_open_dir (name);
  dir_close (old_cwd);
  return thread_current ()->cwd != NULL;
}

/* Creates the directory named dir, which may be relative
   or absolute. Returns true if successful, false on failure. 
   Fails if dir already exists or if any directory name 
   in dir, besides the last, does not already exist. 
   That is, mkdir(“/a/b/c”) succeeds only if /a/b already 
   exists and /a/b/c does not. */
static bool
syscall_mkdir (const char *dir) {  
  if (!usermem_read ((uint8_t *)dir, -1)) {
    syscall_exit (-1);
  }
  return filesys_mkdir (dir);
}


/* Reads a directory entry from file descriptor fd,
   which must represent a directory. If successful, stores 
   the null-terminated file name in name, which must have 
   room for READDIR MAX LEN + 1 bytes, and returns true. 
   If no entries are left in the directory, returns false.
   . and .. should not be returned by readdir */
static bool
syscall_readdir (int fd, char *name) {
  if (!usermem_read ((uint8_t *)name, -1)) {
    syscall_exit (-1);
  }
  struct thread *cur = thread_current ();
  struct list_elem *el;
  struct fd *pfd;
  for (el = list_begin (&cur->open_files); el != list_end (&cur->open_files); 
       el = list_next (el)) {
    pfd = list_entry (el, struct fd, elem);
    if (pfd->fd == fd) {
      if (pfd->is_dir == false) {
        return false;
      }
      return dir_readdir (pfd->dir, name);
    }
  }
  return false;
}

/*  Returns true if fd represents a directory, false if 
    it represents an ordinary file. */
static bool
syscall_isdir (int fd) {
  struct thread *cur = thread_current ();
  struct list_elem *el;
  struct fd *pfd;
  for (el = list_begin (&cur->open_files); el != list_end (&cur->open_files); 
       el = list_next (el)) {
    pfd = list_entry (el, struct fd, elem);
    if (pfd->fd == fd)
      return pfd->is_dir;
  }
  return false;
}

/* Returns the inode number of the inode associated with fd, 
   which may represent an ordinary file or a directory */
static int
syscall_inumber (int fd) {
  struct thread *cur = thread_current ();
  struct list_elem *el;
  struct fd *pfd;
  for (el = list_begin (&cur->open_files); el != list_end (&cur->open_files); 
       el = list_next (el)) {
    pfd = list_entry (el, struct fd, elem);
    if (pfd->fd == fd) {
      if (pfd->is_dir) {
        return inode_get_inumber (dir_get_inode (pfd->dir));
      } else {
        return inode_get_inumber (file_get_inode (pfd->file));
      }
    }
  }
  return -1; 
}

static unsigned long long 
syscall_get_block_read_cnt (void) {
	return filesys_get_read_cnt ();
}

static unsigned long long 
syscall_get_block_write_cnt (void) {
	return filesys_get_write_cnt ();
}

static void
syscall_buffer_clear (void) {
  buffer_clear ();
}
