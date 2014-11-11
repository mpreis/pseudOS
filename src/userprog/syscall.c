#include "userprog/syscall.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "devices/shutdown.h"
#include "filesys/filesys.h"
#include "pagedir.h"

#include <stdio.h>
#include <syscall-nr.h>

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
	int b;
	switch(syscall_nr)
	{
		case SYS_HALT: 
			halt();
			break;
		case SYS_EXIT: 
			// pseudOS: maybe we have to increment the stack-pointer: f->esp += OFFSET_ARG1;
			exit( *(int *)(f->esp + OFFSET_ARG) );
			break;
		// case SYS_EXEC: break; 
		// case SYS_WAIT: break;
		case SYS_CREATE: 
			create ( 
				*(char **)(f->esp + OFFSET_ARG), 
				*(unsigned int *)(f->esp + (OFFSET_ARG * 2)) );
			f->esp += OFFSET_ARG * 3;
			break;
		case SYS_REMOVE: 
			remove ( *(char **)(f->esp + OFFSET_ARG) );
			f->esp += OFFSET_ARG * 2;
			break;
		case SYS_OPEN: 
			b = open ( *(char **)(f->esp + OFFSET_ARG) );
			//f->esp += OFFSET_ARG * 2;
			printf("file desc: %d\n",b);
			
			b = open ( *(char **)(f->esp + OFFSET_ARG) );
			//f->esp += OFFSET_ARG * 2;
			printf("file desc: %d\n",b);
			
			b = open ( *(char **)(f->esp + OFFSET_ARG) );
			f->esp += OFFSET_ARG * 2;
			printf("file desc: %d\n",b);
			break;
		// case SYS_FILESIZE: break;
		// case SYS_READ: break;
		case SYS_WRITE:
			write ( 
				*(int *)(f->esp + (OFFSET_ARG * 1)), 
				*(char **)(f->esp + (OFFSET_ARG * 2)), 
				*(unsigned int *)(f->esp + (OFFSET_ARG * 3)) );
			f->esp += OFFSET_ARG * 4;
			break;
		// case SYS_SEEK: break;
		// case SYS_TELL: break;
		// case SYS_CLOSE: break;
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
  printf(" --- system call: halt\n");
  shutdown_power_off();
}

/*
 * pseudOS: Terminates the current user program, returning status to the kernel. 
 */
void 
exit (int status)
{
  printf(" --- system call: exit(%d) \n", status);
  thread_exit();
}

// /*
//  * pseudOS: Runs the executable whose name is given in cmd_line, passing any given arguments, 
//  * and returns the new process's program id (pid). Must return pid -1, 
//  * which otherwise should not be a valid pid, if the program cannot load or run for any reason. 
//  */
// pid_t 
// exec (const char *cmd_line)
// {
//   printf("system call: exec\n");
//   pid_t pid = -1;
//   return pid;
// }

// /*
//  * pseudOS: Waits for a child process pid and retrieves the child's exit status.
//  */
// int 
// wait (pid_t pid)
// {
//   printf("system call: wait\n");
//   return -1;
// }

/*
 * pseudOS: Creates a new file called file initially initial_size bytes in size. 
 * Returns true if successful, false otherwise. 
 */
bool 
create (const char *file, unsigned initial_size)
{
	return filesys_create (file, initial_size); 
}

/*
 * pseudOS: Deletes the file called file. Returns true if successful, false otherwise. 
 */
bool 
remove (const char *file)
{
  return filesys_remove (file);
}

/*
 * pseudOS: Opens the file called file. Returns a nonnegative integer handle called a 
 * "file descriptor" (fd), or -1 if the file could not be opened.
 */
int 
open (const char *file)
{
	struct file *f = filesys_open (file);
	if(file != NULL)
	{
		struct file_descriptor_t *new_fd;
		new_fd = malloc(sizeof *new_fd);
		new_fd->file_ptr = f;
		new_fd->fd = ( list_empty (&thread_current ()->fds) )
			? FD_INIT
			: (list_entry (
	    	list_back (&thread_current ()->fds), struct file_descriptor_t, elem))->fd + 1;
		list_insert_ordered(&thread_current()->fds, &new_fd->elem, thread_fds_less, NULL);
		return new_fd->fd;	
	}
  return -1;
}

// /*
//  * pseudOS: Returns the size, in bytes, of the file open as fd.
//  */
// int 
// filesize (int fd)
// {
//   printf("system call: filesize\n");
//   return -1;
// }

// /*
//  * pseudOS: Reads size bytes from the file open as fd into buffer. 
//  * Returns the number of bytes actually read (0 at end of file), or -1 if the file could not be read 
//  * (due to a condition other than end of file). Fd 0 reads from the keyboard using input_getc().
//  */
// int 
// read (int fd, void *buffer, unsigned size)
// {
//   printf("system call: read\n");
//   return -1;
// }

/*
 * pseudOS: Writes size bytes from buffer to the open file fd. 
 * Returns the number of bytes actually written, which may be less than size if some bytes 
 * could not be written.
 */
int 
write (int fd, const void *buffer, unsigned size)
{
  printf("system call: write - size: %u\n", size);
  printf("valid ptr: %d\n", is_valid_usr_ptr(buffer));
 
 	if(!is_valid_usr_ptr(buffer)) 
 		thread_exit();

  if(fd == STDOUT_FILENO)
  {
  	putbuf(buffer, size);
  	return size;
  }
  return 0;
}

// /*
//  * pseudOS: Changes the next byte to be read or written in open file fd to position, 
//  * expressed in bytes from the beginning of the file. (Thus, a position of 0 is the file's start.)
//  */
// void 
// seek (int fd, unsigned position)
// {
//   printf("system call: seek\n");
// }

// /* pseudOS: Returns the position of the next byte to be read or written in open file fd, 
//  * expressed in bytes from the beginning of the file.
//  */
// unsigned 
// tell (int fd)
// {
//   printf("system call: tell\n");
//   return 0;
// }

// /*
//  * pseudOS: Closes file descriptor fd. Exiting or terminating a process implicitly closes all its 
//  * open file descriptors, as if by calling this function for each one.
//  */
// void 
// close (int fd)
// {
//   printf("system call: close\n");
// }

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