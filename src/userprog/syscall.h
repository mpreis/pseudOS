#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <list.h>
#include "lib/user/syscall.h"

#define SYSCALL_ERROR -1

void syscall_init (void);
struct lock syscall_lock;	/* pseudOS: Lock variable to ensure a secure execution of a system-call. */

#endif /* userprog/syscall.h */
