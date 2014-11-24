#include "userprog/syscall.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "devices/shutdown.h"
#include "devices/input.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "pagedir.h"
#include "process.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <list.h>
#include <string.h>

#define OFFSET_ARG 4

struct lock file_ops_lock;

static void syscall_handler (struct intr_frame *);
static bool is_valid_usr_ptr(const void * ptr);
static bool is_valid_fd(int fd);

void
syscall_init (void) 
{
	lock_init (&file_ops_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

void
syscall_handler (struct intr_frame *f) 
{
	if(!is_valid_usr_ptr(f->esp)) 
		exit(-1);

	switch(*(uint32_t *) (f->esp))
	{
		case SYS_HALT: 
			halt();
			break;

		case SYS_EXIT: 
			if( is_user_vaddr((int *)(f->esp + OFFSET_ARG)) )
				exit ( *(int *)(f->esp + OFFSET_ARG) );
			else exit(-1);
			break;

		case SYS_EXEC: 
			f->eax = exec ( *(char **)(f->esp + OFFSET_ARG) );
			break; 

		case SYS_WAIT: 
			f->eax = wait ( *(pid_t *)(f->esp + OFFSET_ARG) );
			break;

		case SYS_CREATE: 
			f->eax = create ( 
				*(char **)(f->esp + OFFSET_ARG), 
				*(unsigned int *)(f->esp + (OFFSET_ARG * 2)) );
			break;

		case SYS_REMOVE: 
			f->eax = remove ( *(char **)(f->esp + OFFSET_ARG) );
			break;

		case SYS_OPEN: 
			f->eax = open ( *(char **)(f->esp + OFFSET_ARG) );
			break;

		case SYS_FILESIZE: 
			f->eax = filesize ( *(int *)(f->esp + OFFSET_ARG) );
			break;

		case SYS_READ:
			f->eax = read (
				*(int *)(f->esp + (OFFSET_ARG * 1)), 
				*(char **)(f->esp + (OFFSET_ARG * 2)), 
				*(unsigned int *)(f->esp + (OFFSET_ARG * 3)) );	
			break;
		
		case SYS_WRITE:
			f->eax = write ( 
				*(int *)(f->esp + (OFFSET_ARG * 1)), 
				*(char **)(f->esp + (OFFSET_ARG * 2)), 
				*(unsigned int *)(f->esp + (OFFSET_ARG * 3)) );
			break;

		case SYS_SEEK: 
			seek( 
				*(int *)(f->esp + (OFFSET_ARG * 1)), 
				*(unsigned int *)(f->esp + (OFFSET_ARG * 2)) );
			break;

		case SYS_TELL: 
			f->eax = tell ( *(int *)(f->esp + (OFFSET_ARG * 1)) );
			break;

		case SYS_CLOSE: 
			close ( *(int *)(f->esp + (OFFSET_ARG * 1)) );
			break;

		default:
			exit (-1);         
	}
}

/*
 * pseudOS: Terminates Pintos by calling shutdown_power_off() (declared in devices/shutdown.h). 
 */
void
halt (void)
{
	shutdown_power_off();
}

/*
 * pseudOS: Terminates the current user program, returning status to the kernel. 
 */
void 
exit (int status)
{
	thread_current ()->child_info->exit_status = status;
	printf("%s: exit(%d)\n", thread_current ()->name, status);
	thread_exit ();
}

/*
 * pseudOS: Runs the executable whose name is given in cmd_line, passing any given arguments, 
 * and returns the new process's program id (pid). Must return pid -1, 
 * which otherwise should not be a valid pid, if the program cannot load or run for any reason. 
 */
pid_t 
exec (const char *cmd_line)
{
	if( ! is_valid_usr_ptr (cmd_line) ) 
 		exit(-1);
 	
 	pid_t pid = process_execute (cmd_line);
 	struct child_process *cp = thread_get_child (pid);
  
  sema_down (&cp->init);
  
  if(cp->load_success) 
  	return pid;
  return -1;
}

/*
 * pseudOS: Waits for a child process pid and retrieves the child's exit status.
 */
int 
wait (pid_t pid)
{
	struct child_process *cp = thread_get_child (pid);
	if(cp != NULL && !cp->parent_is_waiting)
	{
		cp->parent_is_waiting = true;
		sema_down (&cp->alive);
		return cp->exit_status;
	}
  return -1;
}

/*
 * pseudOS: Creates a new file called file initially initial_size bytes in size. 
 * Returns true if successful, false otherwise. 
 */
bool 
create (const char *file, unsigned initial_size)
{
 	if( ! is_valid_usr_ptr (file) ) 
 		exit(-1);
	
	lock_acquire (&file_ops_lock);
	bool b = filesys_create (file, initial_size); 
	lock_release (&file_ops_lock);
	return b;
}

/*
 * pseudOS: Deletes the file called file. Returns true if successful, false otherwise. 
 */
bool 
remove (const char *file)
{
 	if( ! is_valid_usr_ptr (file) ) 
 		exit(-1);

	lock_acquire (&file_ops_lock);
	bool b = filesys_remove (file);
	lock_release (&file_ops_lock);
  return b;
}

/*
 * pseudOS: Opens the file called file. Returns a nonnegative integer handle called a 
 * "file descriptor" (fd), or -1 if the file could not be opened.
 */
int 
open (const char *file)
{
 	if( ! is_valid_usr_ptr (file) ) 
 	{
 		printf(" --- invalid ptr \n");
 		exit(-1);
	}
 	lock_acquire (&file_ops_lock);

	struct file *f = filesys_open (file);

	if(f)
	{
		struct thread *t = thread_current ();
		int fds_size = sizeof (t->fds) / sizeof (t->fds[0]);

		int i;
		for(i = 0; i < fds_size; i++)
			if(t->fds[i] == NULL)	break;

		if(i == fds_size) 
		{
			printf(" --- no space \n");
			lock_release (&file_ops_lock);
			return -1; // fds array to small
		}
		t->fds[i] = f;
		int fd = i + FD_INIT;

		lock_release (&file_ops_lock);
		return fd;
	}

	lock_release (&file_ops_lock);
	printf(" --- komisch \n");
  return -1;
}

/*
 * pseudOS: Returns the size, in bytes, of the file open as fd.
 */
int 
filesize (int fd)
{
	if( ! is_valid_fd(fd) || thread_current ()->fds[fd - FD_INIT] == NULL )
		exit(-1);

  return file_length ( (struct file *) thread_current ()->fds[fd - FD_INIT] );
}

/*
 * pseudOS: Reads size bytes from the file open as fd into buffer. 
 * Returns the number of bytes actually read (0 at end of file), or -1 if the file could not be read 
 * (due to a condition other than end of file). Fd 0 reads from the keyboard using input_getc().
 */
int 
read (int fd, void *buffer, unsigned size)
{
	if( !is_valid_usr_ptr (buffer) || !is_valid_fd(fd) ) 
 		exit(-1);

 	lock_acquire (&file_ops_lock);

	if(fd == STDIN_FILENO)
	{
		uint8_t *buf = buffer;
		unsigned i;
		for(i = 0; i < size; i++)
			buf[i] = input_getc();

		lock_release (&file_ops_lock);
		return size;
	} 
	else if ( fd >= FD_INIT && thread_current ()->fds[fd - FD_INIT] != NULL )
	{
		int r = file_read (
			(struct file *) thread_current ()->fds[fd - FD_INIT],
			buffer,
			size );
		lock_release (&file_ops_lock);
		return r;
	}
	lock_release (&file_ops_lock);
	return -1;
}

/*
 * pseudOS: Writes size bytes from buffer to the open file fd. 
 * Returns the number of bytes actually written, which may be less than size if some bytes 
 * could not be written.
 */
int 
write (int fd, const void *buffer, unsigned size)
{ 
	if( !is_valid_usr_ptr (buffer) || !is_valid_fd(fd) ) 
 		exit(-1);

 	lock_acquire (&file_ops_lock);

  if(fd == STDOUT_FILENO)
  {
  	putbuf(buffer, size);
  	lock_release(&file_ops_lock);
  	return size;
  }
  else if (fd >= FD_INIT && thread_current ()->fds[fd - FD_INIT] != NULL )
  {
  	int r = file_write (
			(struct file *) thread_current ()->fds[fd - FD_INIT],
			buffer,
			size );
  	lock_release (&file_ops_lock);
  	return r;
  }
  lock_release (&file_ops_lock);
  return -1;
}

/*
 * pseudOS: Changes the next byte to be read or written in open file fd to position, 
 * expressed in bytes from the beginning of the file. (Thus, a position of 0 is the file's start.)
 */
void 
seek (int fd, unsigned position)
{
	if( ! is_valid_fd(fd) || thread_current ()->fds[fd - FD_INIT] == NULL )
		exit(-1);

	lock_acquire (&file_ops_lock);
	file_seek (
		(struct file *) thread_current ()->fds[fd - FD_INIT], 
		position );
	lock_release (&file_ops_lock);
}

/* pseudOS: Returns the position of the next byte to be read or written in open file fd, 
 * expressed in bytes from the beginning of the file.
 */
unsigned 
tell (int fd)
{
	if( ! is_valid_fd(fd) || thread_current ()->fds[fd - FD_INIT] == NULL )
		exit(-1);
	return file_tell( (struct file *) thread_current ()->fds[fd - FD_INIT] ); 
}

/*
 * pseudOS: Closes file descriptor fd. Exiting or terminating a process implicitly closes all its 
 * open file descriptors, as if by calling this function for each one.
 */
void 
close (int fd)
{
	if( ! is_valid_fd(fd) || thread_current ()->fds[fd - FD_INIT] == NULL)
		return;

	lock_acquire (&file_ops_lock);
	file_close ( thread_current ()->fds[fd - FD_INIT] );
	thread_current ()->fds[fd - FD_INIT] = NULL;
	lock_release (&file_ops_lock);
}

/* pseudOS: As part of a system call, the kernel must often access 
 * memory through pointers provided by a user program. The kernel 
 * must be very careful about doing so, because the user can pass 
 * a null pointer, a pointer to unmapped virtual memory, or a pointer 
 * to kernel virtual address space (above PHYS_BASE). All of these types 
 * of invalid pointers must be rejected without harm to the kernel or other 
 * running processes, by terminating the offending process and freeing its 
 * resources.
 */
static bool
is_valid_usr_ptr(const void * ptr)
{
	if( (ptr != NULL)
				&& is_user_vaddr(ptr)
				&& pagedir_get_page(thread_current()->pagedir, ptr) != 0) 
	{
		return true;
	}
	return false;	
}

/*
 * pseudOS: Checks if the give fd has a valid value.
 */
static bool
is_valid_fd(int fd)
{
	 return (0 <= fd && fd < FD_ARR_DEFAULT_LENGTH);
}