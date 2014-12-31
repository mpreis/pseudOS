#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <list.h>
#include "lib/user/syscall.h"

#define MUNMAP_ALL -2	/* pseudOS: Has to be smaller than -1(!) because -1 is a ERROR case. */
#define SYSCALL_ERROR -1

void syscall_init (void);
bool is_valid_usr_ptr(const void * ptr, unsigned size);	/* pseudOS: Checks if the given pointer is a valid user-space pointer. */

/* pseudOS: Project 3 - memory mapped file */
struct mapped_file_t 
{
  struct list_elem elem;
  struct file *file;
  int mapid;
  void *addr;
  struct list spt_entries;
};

#endif /* userprog/syscall.h */
