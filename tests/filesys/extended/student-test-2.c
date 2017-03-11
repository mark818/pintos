/* Test the buffer cache’s effectiveness by measuring its cache hit rate. 
   First, Open a file and read it sequentially, to determine the cache hit 
   rate for a cold cache. Then, close it, re-open it, and read it sequentially 
   again, to make sure that the cache hit rate improves. */

#include <syscall.h>
#include <random.h>
#include "tests/filesys/seq-test.h"
#include "tests/lib.h"
#include "tests/main.h"

static char buf[512];

void
test_main (void) 
{ 
  char file_name[] = "testfile";
  size_t size = 25600;
  size_t block_size = 512;
  size_t read_size = 512;
  size_t initial_size = 0;
  size_t file_size;
  size_t ofs = 0;
  int fd;
  random_bytes (buf, block_size);
   
  CHECK (create (file_name, initial_size), "create \"%s\"", file_name);
  CHECK ((fd = open (file_name)) > 1, "open \"%s\"", file_name);
  
  msg ("writing \"%s\"", file_name);

  while (ofs < size) {
    if (block_size > size - ofs) {
      block_size = size - ofs;
    }

    if (write (fd, buf, block_size) != (int) block_size) {
      fail ("write %zu bytes at offset %zu in \"%s\" failed",
            block_size, ofs, file_name);
    }
    ofs += block_size;
  }
  file_size = filesize (fd);

  msg ("close \"%s\"", file_name);
  close (fd);

  buffer_clear ();
  msg ("reset cache");

  CHECK ((fd = open (file_name)) > 1, "open \"%s\"", file_name);

  msg ("reading \"%s\"", file_name);

  size_t read_cnt_before = get_block_read_cnt ();
  /* Read the file sequentially the first time. */
  ofs = 0;
  while (ofs < file_size) {
    char block[512];
    size_t ret_val;
    read_size = file_size - ofs;
    if (read_size > sizeof block) {
      read_size = sizeof block;
    }

    ret_val = read (fd, block, read_size);
    if (ret_val != block_size) {
      fail ("read of %zu bytes at offset %zu in \"%s\" returned %zu",
              read_size, ofs, file_name, ret_val);
    }

    compare_bytes (block, buf, block_size, ofs, file_name);
    ofs += read_size;
  }

  msg ("close \"%s\"", file_name);
  close (fd);

  size_t read_cnt_middle = get_block_read_cnt ();

  CHECK ((fd = open (file_name)) > 1, "open \"%s\"", file_name);

  msg ("reading \"%s\" 2nd time", file_name);

  /* Read the file sequentially the second time. */
  ofs = 0;
  while (ofs < file_size) {
    char block[512];
    size_t ret_val;
    read_size = file_size - ofs;
    if (read_size > sizeof block) {
      read_size = sizeof block;
    }

    ret_val = read (fd, block, read_size);
    if (ret_val != block_size) {
      fail ("read of %zu bytes at offset %zu in \"%s\" returned %zu",
              read_size, ofs, file_name, ret_val);
    }

    compare_bytes (block, buf, block_size, ofs, file_name);
    ofs += read_size;
  }

  msg ("close \"%s\"", file_name);
  close (fd);

  size_t read_cnt_after = get_block_read_cnt();

  size_t read_cnt_1 = read_cnt_middle - read_cnt_before;
  size_t read_cnt_2 = read_cnt_after - read_cnt_middle;
 
  CHECK(read_cnt_1 == 51, "first time read caused 51 disk reads");
  CHECK(read_cnt_2 == 0, "second time read caused 0 disk reads");
  CHECK((read_cnt_1 - read_cnt_2) > 0, "cache hit rate improved in the second read");
}