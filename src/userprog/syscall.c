#include "userprog/syscall.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "devices/shutdown.h"
#include "devices/input.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "pagedir.h"
#include "process.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <list.h>

#define OFFSET_ARG 4

static void syscall_handler (struct intr_frame *);
static bool is_valid_usr_ptr(const void * ptr);

/* TODO:
 *  - remove printfs -> tests won't work within prints
 */

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

void
syscall_handler (struct intr_frame *f) 
{
	if(!is_valid_usr_ptr(f->esp)) 
 		thread_exit();

  uint32_t syscall_nr = *(uint32_t *) (f->esp);
	
	printf (" --- system call! (%u)\n", syscall_nr);	
	switch(syscall_nr)
	{
		case SYS_HALT: 
			halt();
			break;

		case SYS_EXIT: 
			exit ( *(int *)(f->esp + OFFSET_ARG) );
			f->esp += OFFSET_ARG * 2;
			break;

		case SYS_EXEC: 
			f->eax = exec ( *(char **)(f->esp + OFFSET_ARG) );
			f->esp += OFFSET_ARG * 2;
			break; 

		case SYS_WAIT: 
			f->eax = wait ( *(pid_t *)(f->esp + OFFSET_ARG) );
			f->esp += OFFSET_ARG * 2;
			break;

		case SYS_CREATE: 
			f->eax = create ( 
				*(char **)(f->esp + OFFSET_ARG), 
				*(unsigned int *)(f->esp + (OFFSET_ARG * 2)) );
			f->esp += OFFSET_ARG * 3;
			break;

		case SYS_REMOVE: 
			f->eax = remove ( *(char **)(f->esp + OFFSET_ARG) );
			f->esp += OFFSET_ARG * 2;
			break;

		case SYS_OPEN: 
			f->eax = open ( *(char **)(f->esp + OFFSET_ARG) );
			f->esp += OFFSET_ARG * 2;
			break;

		case SYS_FILESIZE: 
			f->eax = filesize ( *(int *)(f->esp + OFFSET_ARG) );
			f->esp += OFFSET_ARG * 2;
			break;

		case SYS_READ:
			f->eax = read (
				*(int *)(f->esp + (OFFSET_ARG * 1)), 
				*(char **)(f->esp + (OFFSET_ARG * 2)), 
				*(unsigned int *)(f->esp + (OFFSET_ARG * 3)) );	
			f->esp += OFFSET_ARG * 4;
			break;
		
		case SYS_WRITE:
			f->eax = write ( 
				*(int *)(f->esp + (OFFSET_ARG * 1)), 
				*(char **)(f->esp + (OFFSET_ARG * 2)), 
				*(unsigned int *)(f->esp + (OFFSET_ARG * 3)) );
			f->esp += OFFSET_ARG * 4;
			break;

		case SYS_SEEK: 
			seek( 
				*(int *)(f->esp + (OFFSET_ARG * 1)), 
				*(unsigned int *)(f->esp + (OFFSET_ARG * 2)) );
			f->esp += OFFSET_ARG * 3;
			break;

		case SYS_TELL: 
			f->eax = tell ( *(int *)(f->esp + (OFFSET_ARG * 1)) );
			f->esp += OFFSET_ARG * 2;
			break;

		case SYS_CLOSE: 
			close ( *(int *)(f->esp + (OFFSET_ARG * 1)) );
			f->esp += OFFSET_ARG * 2;
			break;
		default:
		printf("ERROR: invalid system call (%d)! \n", syscall_nr);          
	}

  thread_exit ();
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
	return process_execute (cmd_line);
}

/*
 * pseudOS: Waits for a child process pid and retrieves the child's exit status.
 */
int 
wait (pid_t pid)
{
	struct thread *t = thread_current ();
	struct list_elem *e;
	for (e = list_begin (&t->childs); e != list_end (&t->childs);
       e = list_next (e))
    {
    	struct child_process *cp = list_entry (e, struct child_process, childelem);
    	if(cp->pid == pid)
    	{
    		sema_down (&cp->alive);	// wait till child calls thread_exit
    		sema_up (&cp->alive);
    		return cp->exit_status;
    	}
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
 	if(!is_valid_usr_ptr(file)) 
 		thread_exit();

	return filesys_create (file, initial_size); 
}

/*
 * pseudOS: Deletes the file called file. Returns true if successful, false otherwise. 
 */
bool 
remove (const char *file)
{
 	if(!is_valid_usr_ptr(file)) 
 		thread_exit();

  return filesys_remove (file);
}

/*
 * pseudOS: Opens the file called file. Returns a nonnegative integer handle called a 
 * "file descriptor" (fd), or -1 if the file could not be opened.
 */
int 
open (const char *file)
{
 	if(!is_valid_usr_ptr(file)) 
 		thread_exit();

	struct file *f = filesys_open (file);
	if(file != NULL)
	{
		struct thread *t = thread_current ();
		int fds_size = sizeof (t->fds) / sizeof (t->fds[0]);
		
		int i;
		for(i = 0; i < fds_size; i++)
			if(t->fds[i] == NULL)	break;

		if(i == fds_size) 
			return -1; // fds array to small

		t->fds[i] = (int *) f;
		int fd = i + FD_INIT;
		return fd;
	}
  return -1;
}

/*
 * pseudOS: Returns the size, in bytes, of the file open as fd.
 */
int 
filesize (int fd)
{
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
 	if(!is_valid_usr_ptr(buffer)) 
 		thread_exit();

	if(fd == STDIN_FILENO)
	{
		uint8_t *buf = buffer;
		unsigned i;
		for(i = 0; i < size; i++)
			buf[i] = input_getc();
		return size;
	} 
	else if ( fd >= FD_INIT )
	{
		return file_read (
			(struct file *) thread_current ()->fds[fd - FD_INIT],
			buffer,
			size );
	}
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
 	if( ! is_valid_usr_ptr (buffer) ) 
 		thread_exit();

  if(fd == STDOUT_FILENO)
  {
  	putbuf(buffer, size);
  	return size;
  }
  else if (fd >= FD_INIT)
  {
  	return file_write (
			(struct file *) thread_current ()->fds[fd - FD_INIT],
			buffer,
			size );
  }
  return 0;
}

/*
 * pseudOS: Changes the next byte to be read or written in open file fd to position, 
 * expressed in bytes from the beginning of the file. (Thus, a position of 0 is the file's start.)
 */
void 
seek (int fd, unsigned position)
{
	file_seek (
		(struct file *) thread_current ()->fds[fd - FD_INIT], 
		position );
}

/* pseudOS: Returns the position of the next byte to be read or written in open file fd, 
 * expressed in bytes from the beginning of the file.
 */
unsigned 
tell (int fd)
{
	return file_tell( (struct file *) thread_current ()->fds[fd - FD_INIT] ); 
}

/*
 * pseudOS: Closes file descriptor fd. Exiting or terminating a process implicitly closes all its 
 * open file descriptors, as if by calling this function for each one.
 */
void 
close (int fd)
{
	file_close ( (struct file *) thread_current ()->fds[fd - FD_INIT] );
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
	// a pointer to unmapped virtual memory
	return (ptr != NULL)
				&& is_user_vaddr(ptr)
				&& pagedir_get_page(thread_current()->pagedir, ptr) != (void *)NULL;	
}