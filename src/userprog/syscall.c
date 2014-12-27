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
#include "vm/page.h"
#include "pagedir.h"
#include "process.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <list.h>
#include <string.h>

#define OFFSET_ARG 4							/* pseudOS: Offest of arguments on the stack. */
static struct lock syscall_lock;	/* pseudOS: Lock variable to ensure a secure execution of a system-call. */

static void syscall_handler (struct intr_frame *);
static bool is_valid_fd(int fd);				/* pseudOS: Checks if the given file-descriptor is valid. */
static void check_args (struct intr_frame *f, int nr_of_args);
static void check_stack_growth(void *vaddr, void *esp);
static void check_stack_growth_size(void *vaddr, unsigned size, void *esp);

void
syscall_init (void) 
{
	lock_init (&syscall_lock);
	intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

void
syscall_handler (struct intr_frame *f) 
{
	if(!is_valid_usr_ptr(f->esp, 0)) 
		exit(-1);

	switch(*(uint32_t *) (f->esp))
	{
		case SYS_HALT: 
			halt();
			break;

		case SYS_EXIT: 
			check_args (f, 1);
			exit ( *(int *)(f->esp + OFFSET_ARG) );
			break;

		case SYS_EXEC:
			check_args (f, 1);
			f->eax = exec ( *(char **)(f->esp + OFFSET_ARG) );
			break; 

		case SYS_WAIT: 
			check_args (f, 1);
			f->eax = wait ( *(pid_t *)(f->esp + OFFSET_ARG) );
			break;

		case SYS_CREATE: 
			check_args (f, 2);
			//check_stack_growth_size((f->esp + OFFSET_ARG), *(unsigned int *)(f->esp + OFFSET_ARG * 2), f->esp);
			f->eax = create ( 
				*(char **)(f->esp + OFFSET_ARG), 
				*(unsigned int *)(f->esp + OFFSET_ARG * 2) );
			break;

		case SYS_REMOVE: 
			check_args (f, 1);
			f->eax = remove ( *(char **)(f->esp + OFFSET_ARG) );
			break;

		case SYS_OPEN: 
			check_args (f, 1);
			f->eax = open ( *(char **)(f->esp + OFFSET_ARG) );
			break;

		case SYS_FILESIZE: 
			check_args (f, 1);
			f->eax = filesize ( *(int *)(f->esp + OFFSET_ARG) );
			break;

		case SYS_READ:
			check_args (f, 3);
			//check_stack_growth_size((f->esp + OFFSET_ARG * 2), *(unsigned int *)(f->esp + OFFSET_ARG * 3), f->esp);
			f->eax = read (
				*(int *)(f->esp + OFFSET_ARG), 
				*(char **)(f->esp + OFFSET_ARG * 2), 
				*(unsigned int *)(f->esp + OFFSET_ARG * 3) );
			break;
		
		case SYS_WRITE:
			check_args (f, 3);
			//check_stack_growth_size((f->esp + OFFSET_ARG * 2), *(unsigned int *)(f->esp + OFFSET_ARG * 3), f->esp);
			f->eax = write ( 
				*(int *)(f->esp + OFFSET_ARG), 
				*(char **)(f->esp + OFFSET_ARG * 2), 
				*(unsigned int *)(f->esp + OFFSET_ARG * 3) );
			break;

		case SYS_SEEK: 
			check_args (f, 2);
			seek( 
				*(int *)(f->esp + OFFSET_ARG ), 
				*(unsigned int *)(f->esp + OFFSET_ARG * 2) );
			break;

		case SYS_TELL: 
			check_args (f, 1);
			f->eax = tell ( *(int *)(f->esp + OFFSET_ARG) );
			break;

		case SYS_CLOSE: 
			check_args (f, 1);
			close ( *(int *)(f->esp + OFFSET_ARG) );
			break;

		case SYS_MMAP:
			check_args (f, 2);
			f->eax = mmap (
				*(int *)(f->esp + OFFSET_ARG), 
				*(void **)(f->esp + OFFSET_ARG * 2) );
			break;

		case SYS_MUNMAP:
			check_args (f, 1);
			munmap ( *(mapid_t *)(f->esp + OFFSET_ARG) );
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
	if( ! is_valid_usr_ptr (cmd_line, 0) ) 
 	{
 		exit(-1);
 	}
 	lock_acquire (&syscall_lock);

 	pid_t pid = process_execute (cmd_line);
 	struct child_process *cp = thread_get_child (pid);
  
	sema_down (&cp->init);

	lock_release (&syscall_lock);
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
 	if( ! is_valid_usr_ptr (file, initial_size) ) 
 	{
 		exit (-1);
	}

 	lock_acquire (&syscall_lock);
	bool b = filesys_create (file, initial_size); 
	lock_release (&syscall_lock);
	return b;
}

/*
 * pseudOS: Deletes the file called file. Returns true if successful, false otherwise. 
 */
bool 
remove (const char *file)
{
 	if( ! is_valid_usr_ptr (file, 0) ) 
 		return false;

	lock_acquire (&syscall_lock);
	bool b = filesys_remove (file);
	lock_release (&syscall_lock);
	return b;
}

/*
 * pseudOS: Opens the file called file. Returns a nonnegative integer handle called a 
 * "file descriptor" (fd), or -1 if the file could not be opened.
 */
int 
open (const char *file)
{
	if( ! is_valid_usr_ptr (file, 0) ) 
 	{
 		exit (-1);
	}
	
 	lock_acquire (&syscall_lock);
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
			lock_release (&syscall_lock);
			return -1; // fds array to small
		}
		t->fds[i] = f;
		int fd = i + FD_INIT;

		lock_release (&syscall_lock);
		return fd;
	}
	lock_release (&syscall_lock);
	return -1;
}

/*
 * pseudOS: Returns the size, in bytes, of the file open as fd.
 */
int 
filesize (int fd)
{
	if( ! is_valid_fd(fd) || thread_current ()->fds[fd - FD_INIT] == NULL )
	{
 		exit (-1);
	}
	
	lock_acquire (&syscall_lock);
	int size = file_length ( thread_current ()->fds[fd - FD_INIT] );
	lock_release (&syscall_lock);
	return size;
}

/*
 * pseudOS: Reads size bytes from the file open as fd into buffer. 
 * Returns the number of bytes actually read (0 at end of file), or -1 if the file could not be read 
 * (due to a condition other than end of file). Fd 0 reads from the keyboard using input_getc().
 */
int 
read (int fd, void *buffer, unsigned size)
{

	if( !is_valid_usr_ptr (buffer, size) || !is_valid_fd(fd) ) 
 	{
 		exit (-1);
	}
	
 	lock_acquire (&syscall_lock);
	if(fd == STDIN_FILENO)
	{
		uint8_t *buf = buffer;
		unsigned i;
		for(i = 0; i < size; i++)
			buf[i] = input_getc();

		lock_release (&syscall_lock);
		return size;
	} 
	else if ( fd >= FD_INIT && thread_current ()->fds[fd - FD_INIT] != NULL )
	{
		int r = file_read (
			thread_current ()->fds[fd - FD_INIT],
			buffer, size );
		lock_release (&syscall_lock);
		return r;
	}
	lock_release (&syscall_lock);
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
	if( !is_valid_usr_ptr (buffer, size) || !is_valid_fd(fd) ) 
 	{
 		exit (-1);
	}
	
 	lock_acquire (&syscall_lock);
	if(fd == STDOUT_FILENO)
	{
		putbuf(buffer, size);
		lock_release(&syscall_lock);
		return size;
	}
	else if (fd >= FD_INIT && thread_current ()->fds[fd - FD_INIT] != NULL )
	{
		int r = file_write (
			thread_current ()->fds[fd - FD_INIT],
			buffer, size );
		lock_release (&syscall_lock);
		return r;
	}
	lock_release (&syscall_lock);
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
	{
 		exit (-1);
	}
	
	lock_acquire (&syscall_lock);
	file_seek (
		thread_current ()->fds[fd - FD_INIT], 
		position );
	lock_release (&syscall_lock);
}

/* pseudOS: Returns the position of the next byte to be read or written in open file fd, 
 * expressed in bytes from the beginning of the file.
 */
unsigned 
tell (int fd)
{
	if( ! is_valid_fd(fd) || thread_current ()->fds[fd - FD_INIT] == NULL )
	{
 		exit (-1);
	}
	
	lock_acquire (&syscall_lock);
	unsigned pos = file_tell( (struct file *) thread_current ()->fds[fd - FD_INIT] ); 
	lock_release (&syscall_lock);
	return pos;
}

/*
 * pseudOS: Closes file descriptor fd. Exiting or terminating a process implicitly closes all its 
 * open file descriptors, as if by calling this function for each one.
 */
void 
close (int fd)
{
	if( ! is_valid_fd (fd) || thread_current ()->fds[fd - FD_INIT] == NULL)
		return;

	lock_acquire (&syscall_lock);
	file_close ( thread_current ()->fds[fd - FD_INIT] );
	thread_current ()->fds[fd - FD_INIT] = NULL;
	lock_release (&syscall_lock);
}

/* 
 * pseudOS: 
 	A call to mmap may fail if 
 	 1. the file open as fd has a length of zero bytes. 
 	 2. It must fail if addr is not page-aligned or 
 	 3. if the range of pages mapped overlaps any existing set of mapped pages, 
 	 		including the stack or pages mapped at executable load time. 
 	 4. It must also fail if addr is 0, because some Pintos code assumes virtual page 0 is not mapped. 
 	 5. Finally, file descriptors 0 and 1, representing console input and output, are not mappable.
 */
mapid_t 
mmap (int fd, void *addr)
{
	// printf (" --- --- mmap \n");
	if(! is_valid_fd (fd) || fd < FD_INIT) 		/* pseudOS: 5. */
		return MAP_FAILED;
	
	struct thread *t = thread_current ();
	struct file *f = t->fds[fd - FD_INIT];
	if(f == NULL) 
		return MAP_FAILED;
	
	off_t flen = file_length (f);
	if(	   flen == 0							/* pseudOS: 1. */
		|| addr == 0 							/* pseudOS: 4. */
		//|| ! is_valid_usr_ptr (addr, nr_of_pages) /* pseudOS: 3. */
		|| ((uint32_t)addr % PGSIZE) != 0 )		/* pseudOS: 2. */
		return MAP_FAILED;
	
	lock_acquire (&syscall_lock);
	struct mapped_file_t *mfile = malloc(sizeof(struct mapped_file_t));
	if (!mfile) return MAP_FAILED;
	mfile->file = f;
	mfile->addr = addr;
	mfile->mapid = (mapid_t) t->next_mapid++;
	list_init (&mfile->spt_entries);
	list_push_back (&t->mapped_files, &mfile->elem);

	off_t ofs = 0;
	while(flen > 0)
	{
		uint32_t read_bytes = (flen > PGSIZE ) ? PGSIZE : flen ;
		uint32_t zero_bytes = PGSIZE - read_bytes;

		struct spt_entry_t *spte = spt_insert (t->spt, f, ofs, (uint8_t *)addr, 
				read_bytes, zero_bytes, true, SPT_ENTRY_TYPE_MMAP);
		if(! spte)
		{
			munmap (mfile->mapid);
			lock_release (&syscall_lock);
			return MAP_FAILED;
		}

		list_push_back (&mfile->spt_entries, &spte->listelem);

		addr += PGSIZE;
 		flen -= read_bytes;
		ofs  += read_bytes;
	}
	lock_release (&syscall_lock);
	// printf (" --- --- mmap end: %d \n", mfile->mapid);
	return mfile->mapid;
}

void 
munmap (mapid_t mapping)
{
	// printf(" --- --- munmap mapping: %d \n", mapping);

	if(mapping < 0)
		return;

	struct thread *t = thread_current ();
	struct list_elem *next;
	struct list_elem *e = list_begin (&t->mapped_files);
	while(e != list_end (&t->mapped_files))
	{
		next = list_next (e);
		struct mapped_file_t *mfile = list_entry (e, struct mapped_file_t, elem);
		if (mapping == mfile->mapid || mapping == MUNMAP_ALL)
		{
			struct file *file = file_reopen (mfile->file);
			if(!file)
			{
				exit (-1);
			}
			
			struct list_elem *mfe_next;
			struct list_elem *mfe = list_begin (&mfile->spt_entries);
			// printf (" --- spt_entries - size: %d \n", list_size (&mfile->spt_entries));
			while (mfe != list_end (&mfile->spt_entries))
			{
				mfe_next  = list_next (mfe);
				struct spt_entry_t *spte = list_entry (mfe, struct spt_entry_t, listelem);
				if (   pagedir_is_accessed (t->pagedir, spte->upage) 
					|| pagedir_is_dirty (t->pagedir, spte->upage))
				{
					lock_acquire (&syscall_lock);

					void *kpage = pagedir_get_page (t->pagedir, spte->upage);
					off_t written_bytes = file_write_at (file, kpage, 
					 							spte->read_bytes, spte->ofs);
					
					
					// printf (" -- file_write_at file: %p, kpage: %p, upage: %p, rb: %u, ofs: %u;\n", 
					// 	file, kpage, spte->upage, spte->read_bytes, spte->ofs);
					lock_release (&syscall_lock);
					
					if((off_t)spte->read_bytes != written_bytes)
					{
			 			// printf(" -1- munmap - wb: %u rb: %u \n", written_bytes, spte->read_bytes);
			 			exit (-1);
					}
					
				}
				spt_remove (t->spt, spte->upage);		/* remove supplemental page table entry. */
				spt_entry_free (&spte->hashelem, NULL);	/* free resources of entry. */
				mfe = mfe_next;
			}
			file_close (file);
			list_remove (&mfile->elem);
			free (mfile);
		}
		e = next;
	} /* end iteration over mapped files */
	// printf (" --- --- munmap end. \n");

}

/* 
 * pseudOS: Checks if the given pointer is a valid user-space pointer.
 * 
 * Snippet of the Pintos docu: 
 * As part of a system call, the kernel must often access 
 * memory through pointers provided by a user program. The kernel 
 * must be very careful about doing so, because the user can pass 
 * a null pointer, a pointer to unmapped virtual memory, or a pointer 
 * to kernel virtual address space (above PHYS_BASE). All of these types 
 * of invalid pointers must be rejected without harm to the kernel or other 
 * running processes, by terminating the offending process and freeing its 
 * resources.
 */
bool
is_valid_usr_ptr(const void * ptr, unsigned size)
{
	lock_acquire (&syscall_lock);
	if(ptr == NULL || ! is_user_vaddr(ptr) || ! is_user_vaddr(ptr + size))
	{
		lock_release (&syscall_lock);
		return false;
	}

	/* Check if every page is mapped */
	uint32_t *pg;
	for (	pg = pg_round_down (ptr); 
			pg <= (uint32_t *) pg_round_down (ptr + size);
			pg += PGSIZE
		)
	{
		if ( ! pagedir_get_page (thread_current ()->pagedir, pg) )
		{
			lock_release (&syscall_lock);
			return false;
		}
	}
	lock_release (&syscall_lock);
	return true;
}

/*
 * pseudOS: Checks if the given file-descriptor is valid. 
 */
static bool
is_valid_fd(int fd)
{
	 return (0 <= fd && fd < FD_ARR_DEFAULT_LENGTH);
}

static void
check_args (struct intr_frame *f, int nr_of_args)
{
	int i;
	for (i = 1; i <= nr_of_args; i++)
	{
		if(!is_user_vaddr(f->esp + OFFSET_ARG * i)) 
			exit (-1);

		check_stack_growth((f->esp + OFFSET_ARG * i), f->esp);
	}
}

static void
check_stack_growth(void *vaddr, void *esp)
{
	struct spt_entry_t *e = spt_lookup (thread_current ()->spt, vaddr);

	// pseudOS: create a new page for the stack, if there is no entry 
	// for vaddr in the supplemental page table and if vaddr is higher 
	// than (esp - 32) 
	if( (e == NULL) && (vaddr >= (esp - 32)) )
        stack_growth (vaddr);
}

static void
check_stack_growth_size(void *buffer, unsigned size, void *esp)
{
	uint32_t *pg;
	for (pg = pg_round_down (buffer); pg <= (uint32_t *) pg_round_down (buffer + size); pg += PGSIZE)
		check_stack_growth(pg, esp);
}
