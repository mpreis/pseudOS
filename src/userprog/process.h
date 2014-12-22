#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "userprog/syscall.h"
#include "filesys/file.h"

/* pseudOS: Project 2 */
#define DEFAULT_EXIT_STATUS -1 /* pseudOS: The default exit status.*/

struct child_process
  {
    struct list_elem childelem; /* pseudOS: List element. */
    pid_t pid;                  /* pseudOS: ID of the process. */
    int exit_status;            /* pseudOS: Status which is passed to the system-call exit. */
    struct thread *parent;      /* pseudOS: Reference to the process parent. */
    struct semaphore alive;     /* pseudOS: This semaphore is down till the thread dies. */
    struct semaphore init;      /* pseudOS: This semaphore goes up if the initialization is done. */
    bool load_success;          /* pseudOS: Indicates if loading the executable was sucessful. */
    bool parent_is_waiting;     /* pseudOS: Indicates if the parent is already waiting for this child. */
  };

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

bool install_page (void *upage, void *kpage, bool writable);

#endif /* userprog/process.h */
