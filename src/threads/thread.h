#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include <hash.h>
#include "synch.h"

/* States in a thread's life cycle. */
enum thread_status
  {
    THREAD_RUNNING,     /* Running thread. */
    THREAD_READY,       /* Not running but ready to run. */
    THREAD_BLOCKED,     /* Waiting for an event to trigger. */
    THREAD_DYING        /* About to be destroyed. */
  };

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */

/* pseudOS: Nice range */
#define NICE_MIN -20			/* Lowest niceness. */
#define NICE_DEFAULT 0			/* Default niceness. */
#define NICE_MAX 20			/* Highest niceness. */

/* pseudOS: Project 2 */
#define FD_INIT 2                    /* pseudOS: The smallest possible value of a file-descriptor. */
#define FD_ARR_DEFAULT_LENGTH 128    /* pseudOS: Default size of the file-descriptors array. */

/* pseudOS: Project 3*/
#define INIT_MAPID 0                 /* pseudOS: First id of a memory mapped file. */
#define MUNMAP_ALL -2                /* pseudOS: Has to be smaller than -1(!) because -1 is a ERROR case. */

/* A kernel thread or user process.

   Each thread structure is stored in its own 4 kB page.  The
   thread structure itself sits at the very bottom of the page
   (at offset 0).  The rest of the page is reserved for the
   thread's kernel stack, which grows downward from the top of
   the page (at offset 4 kB).  Here's an illustration:

        4 kB +---------------------------------+
             |          kernel stack           |
             |                |                |
             |                |                |
             |                V                |
             |         grows downward          |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             +---------------------------------+
             |              magic              |
             |                :                |
             |                :                |
             |               name              |
             |              status             |
        0 kB +---------------------------------+

   The upshot of this is twofold:

      1. First, `struct thread' must not be allowed to grow too
         big.  If it does, then there will not be enough room for
         the kernel stack.  Our base `struct thread' is only a
         few bytes in size.  It probably should stay well under 1
         kB.

      2. Second, kernel stacks must not be allowed to grow too
         large.  If a stack overflows, it will corrupt the thread
         state.  Thus, kernel functions should not allocate large
         structures or arrays as non-static local variables.  Use
         dynamic allocation with malloc() or palloc_get_page()
         instead.

   The first symptom of either of these problems will probably be
   an assertion failure in thread_current(), which checks that
   the `magic' member of the running thread's `struct thread' is
   set to THREAD_MAGIC.  Stack overflow will normally change this
   value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
   the run queue (thread.c), or it can be an element in a
   semaphore wait list (synch.c).  It can be used these two ways
   only because they are mutually exclusive: only a thread in the
   ready state is on the run queue, whereas only a thread in the
   blocked state is on a semaphore wait list. */
struct thread
  {
    /* Owned by thread.c. */
    tid_t tid;                          /* Thread identifier. */
    enum thread_status status;          /* Thread state. */
    char name[16];                      /* Name (for debugging purposes). */
    uint8_t *stack;                     /* Saved stack pointer. */
    int priority;                       /* Priority. */
    int init_priority;			            /* pseudOS: Initial priorirty */
    struct list_elem allelem;           /* List element for all threads list. */

    /* Shared between thread.c and synch.c. */
    struct list_elem elem;              /* List element. */

#ifdef USERPROG
    /* Owned by userprog/process.c. */
    uint32_t *pagedir;                  /* Page directory. */

    /* pseudOS: Project 2 */
    struct list childs;                     /* pseudOS: List of children of this thread. */
    struct child_process* child_info;       /* pseudOS: Holds information of this thread. */ 
    struct file* executable;                /* pseudOS: Represents the executable which is executed by this thread.*/

    /* pseudOS: Project 3 */
    struct hash* spt;                       /* pseudOS: Supplemental page table. */
    struct list mapped_files;               /* pseudOS: This list holds pointers of all memory mapped files. */ 
    int next_mapid;
#endif

    /* Owned by thread.c. */
    unsigned magic;              /* Detects stack overflow. */

    /* pseudOS: Project 1 */
    uint64_t ticks_to_sleep;		 /* pseudOS: Number of ticks that the thread has to sleep */
    struct list donations;		   /* pseudOS: List of donations */
    struct list_elem donelem;		 /* pseudOS: Element of the donation list */
    struct lock *wanted_lock;		 /* pseudOS: Lock needed by this thread */
    int niceness; 			         /* pseudOS: Nice value of a thread. */
    int recent_cpu; 			       /* pseudOS: How much time recieved this thread recently. */

    /* pseudOS: Project 2 */
    struct file* fds[FD_ARR_DEFAULT_LENGTH]; /* pseudOS: This array holds pointers of all open files. */
  };

/* pseudOS: Project 3 - memory mapped file */
struct mapped_file_t 
{
  struct list_elem elem;
  int fd;
  int mapid;
  void *addr;
  struct list spt_entries;
};

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

/* Performs some operation on thread t, given auxiliary data AUX. */
typedef void thread_action_func (struct thread *t, void *aux);
void thread_foreach (thread_action_func *, void *);
void thread_foreach_ready (thread_action_func *, void *);

int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

/* 
 * pseudOS: public functions 
 */
// priority donation
bool thread_priority_leq (const struct list_elem *a_, const struct list_elem *b_, void *aux UNUSED);
void thread_donate_priority(void);
void thread_remove_donation (struct lock *lock);
void thread_update_priority (void);
void thread_priority_check (void);
// mlfqs
void thread_mlfqs_calc_priority (struct thread *t, void *aux UNUSED);
void thread_mlfqs_calc_recent_cpu (struct thread *t, void *aux UNUSED);
void thread_mlfqs_calc_load_avg (void);
void thread_mlfqs_update_properties (void);

/* pseudOS */
struct child_process *thread_get_child (int pid);

#endif /* threads/thread.h */
