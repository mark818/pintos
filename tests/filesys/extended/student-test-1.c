/* Write a 66560 byte file one byte at a time sequentially and makes sure the 
number of disk writes is on the order of 128. */

#include <syscall.h>
#include <random.h>
#include "tests/filesys/seq-test.h"
#include "tests/lib.h"
#include "tests/main.h"

static char buf[1];

void
test_main (void) 
{ 
   char file_name[] = "testfile";
   size_t size = 66560;
   size_t block_size = 1;
   size_t initial_size = 66560;
   size_t file_size;
   size_t ofs = 0;
   size_t read_size;
   int fd;
   random_bytes (buf, block_size);
   
   CHECK (create (file_name, initial_size), "create \"%s\"", file_name);
   CHECK ((fd = open (file_name)) > 1, "open \"%s\"", file_name);
  
   msg ("writing \"%s\"", file_name);
   size_t write_cnt_before = get_block_write_cnt();

   while (ofs < size) 
    {
      if (write (fd, buf, block_size) != (int) block_size)
        fail ("write %zu bytes at offset %zu in \"%s\" failed",
              block_size, ofs, file_name);

      ofs += block_size;
    }
    file_size = filesize (fd);
    close (fd);

    size_t write_cnt_after = get_block_write_cnt();

    /* Read the file block-by-block for verification. */
    CHECK ((fd = open (file_name)) > 1, "open \"%s\" for verification",
    file_name);

    block_size = 512;
    ofs = 0;
    while (ofs < file_size)
    {
      char block[512];
      size_t ret_val;
      read_size = file_size - ofs;
      if (read_size > sizeof block)
        read_size = sizeof block;

      ret_val = read (fd, block, read_size);
      if (ret_val != block_size)
        fail ("read of %zu bytes at offset %zu in \"%s\" returned %zu",
              read_size, ofs, file_name, ret_val);
      ofs += read_size;
    }

    close (fd);

    int diff = write_cnt_after - write_cnt_before;

    //CHECK (1, "total number of disk writes is \"%d\"", diff);
    CHECK(diff < 135, "disk writes upper range correct");
    CHECK(diff > 120, "disk writes lower range correct");
}