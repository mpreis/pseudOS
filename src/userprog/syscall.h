#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "lib/user/syscall.h"

void syscall_init (void);
bool is_valid_usr_ptr(const void * ptr, unsigned size);	/* pseudOS: Checks if the given pointer is a valid user-space pointer. */


#endif /* userprog/syscall.h */
