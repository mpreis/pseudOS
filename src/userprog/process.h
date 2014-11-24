#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "userprog/syscall.h"
#include "filesys/file.h"

/* pseudOS */
struct child_process
  {
    struct list_elem childelem;
    pid_t pid;
    int exit_status;
    struct thread *parent;
    struct semaphore alive;            /* pseudOS: This semaphore shows that this thread is alive. */
    struct semaphore init;
    bool load_success;
    bool parent_is_waiting;
  };

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

#endif /* userprog/process.h */
