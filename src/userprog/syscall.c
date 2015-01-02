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
#include <hash.h>

#define OFFSET_ARG 4							/* pseudOS: Offest of arguments on the stack. */
//static struct lock syscall_lock;	/* pseudOS: Lock variable to ensure a secure execution of a system-call. */

static void syscall_handler (struct intr_frame *);
static bool is_valid_fd(int fd);				/* pseudOS: Checks if the given file-descriptor is valid. */
static void check_args (struct intr_frame *f, int nr_of_args);
static void check_buffer (void *vaddr, unsigned size, void *esp);
static void check_stack_growth(void *vaddr, void *esp);
static bool is_valid_mapid(mapid_t mapping);
static bool is_valid_mapping (void *addr, off_t file_len);

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
		exit (SYSCALL_ERROR);

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
			check_stack_growth (*(char **)(f->esp + OFFSET_ARG), f->esp);
			f->eax = exec ( *(char **)(f->esp + OFFSET_ARG) );
			break; 

		case SYS_WAIT: 
			check_args (f, 1);
			f->eax = wait ( *(pid_t *)(f->esp + OFFSET_ARG) );
			break;

		case SYS_CREATE: 
			check_args (f, 2);
			check_stack_growth (*(char **)(f->esp + OFFSET_ARG), f->esp);
			f->eax = create ( 
				*(char **)(f->esp + OFFSET_ARG), 
				*(unsigned int *)(f->esp + OFFSET_ARG * 2) );
			break;

		case SYS_REMOVE: 
			check_args (f, 1);
			check_stack_growth (*(char **)(f->esp + OFFSET_ARG), f->esp);
			f->eax = remove ( *(char **)(f->esp + OFFSET_ARG) );
			break;

		case SYS_OPEN: 
			check_args (f, 1);
			check_stack_growth (*(char **)(f->esp + OFFSET_ARG), f->esp);
			f->eax = open ( *(char **)(f->esp + OFFSET_ARG) );
			break;

		case SYS_FILESIZE: 
			check_args (f, 1);
			f->eax = filesize ( *(int *)(f->esp + OFFSET_ARG) );
			break;

		case SYS_READ:
			check_args (f, 3);
			check_buffer (
				*(char **)(f->esp + OFFSET_ARG * 2), 
				*(unsigned int *)(f->esp + OFFSET_ARG * 3), 
				f->esp);
			f->eax = read (
				*(int *)(f->esp + OFFSET_ARG), 
				*(char **)(f->esp + OFFSET_ARG * 2), 
				*(unsigned int *)(f->esp + OFFSET_ARG * 3) );
			break;
		
		case SYS_WRITE:
			check_args (f, 3);
			check_buffer(
				*(char **)(f->esp + OFFSET_ARG * 2), 
				*(unsigned int *)(f->esp + OFFSET_ARG * 3), 
				f->esp);
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
			exit (SYSCALL_ERROR);         
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
	// if( ! is_valid_usr_ptr (cmd_line, 0) ) 
		// exit (SYSCALL_ERROR);
 	
 	lock_acquire (&syscall_lock);

 	pid_t pid = process_execute (cmd_line);
 	struct child_process *cp = thread_get_child (pid);
  
	sema_down (&cp->init);

	lock_release (&syscall_lock);
	if(cp->load_success) 
		return pid;
	return SYSCALL_ERROR;
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
	return SYSCALL_ERROR;
}

/*
 * pseudOS: Creates a new file called FILE initially INITIAL_SIZE bytes in size. 
 * Returns true if successful, false otherwise. 
 */
bool 
create (const char *file, unsigned initial_size)
{
 	// if( ! is_valid_usr_ptr (file, initial_size) )
 	// 	exit (SYSCALL_ERROR);
	// printf(" -1- create \n");
 	lock_acquire (&syscall_lock);
	// printf(" -2- create  %p \n", pagedir_get_page (thread_current ()->pagedir, file));
	bool b = filesys_create (file, initial_size); 
	// printf(" -3- create \n");
	lock_release (&syscall_lock);
	// printf(" -4- create \n");
	return b;
}

/*
 * pseudOS: Deletes the file called file. Returns true if successful, false otherwise. 
 */
bool 
remove (const char *file)
{
 	// if( ! is_valid_usr_ptr (file, 0) ) 
 	// 	return false;

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
	// if( ! is_valid_usr_ptr (file, 0) ) 
 // 		exit (SYSCALL_ERROR);
	
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
			return SYSCALL_ERROR; // fds array to small
		}
		t->fds[i] = f;
		int fd = i + FD_INIT;

		lock_release (&syscall_lock);
		return fd;
	}
	lock_release (&syscall_lock);
	return SYSCALL_ERROR;
}

/*
 * pseudOS: Returns the size, in bytes, of the file open as fd.
 */
int 
filesize (int fd)
{
	if( ! is_valid_fd(fd) || thread_current ()->fds[fd - FD_INIT] == NULL )
		exit (SYSCALL_ERROR);
	
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
	if( !is_valid_fd(fd) ) 
		exit (SYSCALL_ERROR);
	
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
	return SYSCALL_ERROR;
}

/*
 * pseudOS: Writes size bytes from buffer to the open file fd. 
 * Returns the number of bytes actually written, which may be less than size if some bytes 
 * could not be written.
 */
int 
write (int fd, const void *buffer, unsigned size)
{ 
	if( !is_valid_fd(fd) ) 
		exit (SYSCALL_ERROR);
	
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
	return SYSCALL_ERROR;
}

/*
 * pseudOS: Changes the next byte to be read or written in open file fd to position, 
 * expressed in bytes from the beginning of the file. (Thus, a position of 0 is the file's start.)
 */
void 
seek (int fd, unsigned position)
{
	if( ! is_valid_fd(fd) || thread_current ()->fds[fd - FD_INIT] == NULL )
		exit (SYSCALL_ERROR);
	
	lock_acquire (&syscall_lock);
	file_seek (thread_current ()->fds[fd - FD_INIT], position);
	lock_release (&syscall_lock);
}

/* pseudOS: Returns the position of the next byte to be read or written in open file fd, 
 * expressed in bytes from the beginning of the file.
 */
unsigned 
tell (int fd)
{
	if( ! is_valid_fd(fd) || thread_current ()->fds[fd - FD_INIT] == NULL )
		exit (SYSCALL_ERROR);
	
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
	if(! is_valid_fd (fd) || fd < FD_INIT) 		/* pseudOS: 5. */
		return MAP_FAILED;
	
	struct thread *t = thread_current ();
	struct file *f = t->fds[fd - FD_INIT];
	if(f == NULL) 
		return MAP_FAILED;
	
	off_t flen = file_length (f);
	if(	   flen == 0							/* pseudOS: 1. */
		|| addr == 0 							/* pseudOS: 4. */
		|| ((uint32_t)addr % PGSIZE) != 0		/* pseudOS: 2. */
		|| ! is_valid_mapping (addr, flen))		/* pseudOS: 3. */
		return MAP_FAILED;
	
	//lock_acquire (&syscall_lock);
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
				read_bytes, zero_bytes, true);
		if(! spte)
		{
			lock_release (&syscall_lock);
			munmap (mfile->mapid);
			return MAP_FAILED;
		}

		list_push_back (&mfile->spt_entries, &spte->listelem);

		addr += PGSIZE;
 		flen -= read_bytes;
		ofs  += read_bytes;
	}
	//lock_release (&syscall_lock);
	return mfile->mapid;
}

void 
munmap (mapid_t mapping)
{
	if(! is_valid_mapid (mapping))
		exit (SYSCALL_ERROR);

	struct thread *t = thread_current ();
	struct list_elem *next, *e = list_begin (&t->mapped_files);
	while(e != list_end (&t->mapped_files))
	{
		next = list_next (e);
		struct mapped_file_t *mfile = list_entry (e, struct mapped_file_t, elem);
		if (mapping == mfile->mapid || mapping == MUNMAP_ALL)
		{
			struct file *file = file_reopen (mfile->file);
			if(!file) exit (SYSCALL_ERROR);
			
			struct list_elem *mfe_next;
			struct list_elem *mfe = list_begin (&mfile->spt_entries);
			while (mfe != list_end (&mfile->spt_entries))
			{
				mfe_next  = list_next (mfe);
				struct spt_entry_t *spte = list_entry (mfe, struct spt_entry_t, listelem);
				if (pagedir_is_dirty (t->pagedir, spte->upage))
				{
					lock_acquire (&syscall_lock);

					void *kpage = pagedir_get_page (t->pagedir, spte->upage);
					if(kpage != NULL)
					{
						off_t written_bytes = file_write_at (file, kpage, spte->read_bytes, spte->ofs);
						if((off_t)spte->read_bytes != written_bytes)
						{
							lock_release (&syscall_lock);
							exit (SYSCALL_ERROR);
						}
						pagedir_clear_page (t->pagedir, spte->upage);
					}
					lock_release (&syscall_lock);
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
	if(ptr == NULL || ! is_user_vaddr(ptr) || ! is_user_vaddr(ptr + size)) 
		return false;
	
	//lock_acquire (&syscall_lock);
	/* Check if every page is mapped */
	uint32_t *pg;
	for (pg  = pg_round_down (ptr); 
		 pg <= (uint32_t *) pg_round_down (ptr + size);
		 pg += PGSIZE
		)
	{
		struct spt_entry_t *spte;
		if ( (spte = spt_lookup (thread_current ()->spt, pg)) != NULL )
		{
			spt_load_page (spte);
		}
		else 
		{
			//lock_release (&syscall_lock);
			return false;
		}
	}
	//lock_release (&syscall_lock);
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

/*
 * pseudOS: Checks if the given mapid is valid.
 */
static bool
is_valid_mapid(mapid_t mapping)
{
	return (0 <= mapping && mapping < thread_current ()->next_mapid) 
			|| mapping == MUNMAP_ALL; 
}

/*
 * pseudOS: Check if the given mapping overlaps with an existing one.
 */
static bool
is_valid_mapping (void *addr, off_t file_len)
{
	uint8_t *range_begin = (uint8_t*) addr;
	uint8_t *range_end = (uint8_t*)(addr + file_len);

	uint8_t *tmp_addr;
	for (tmp_addr = range_begin; tmp_addr < range_end; tmp_addr += PGSIZE)
	{
		if (spt_lookup (thread_current ()->spt, tmp_addr) != NULL)
			return false;
	}
	return true;
}

/*
 * pseudOS: Checks if the given number of arguments are valid user pointers.
 */
static void
check_args (struct intr_frame *f, int nr_of_args)
{
	unsigned i;
	for (i = 1; i <= nr_of_args; i++)
		check_stack_growth((f->esp + OFFSET_ARG * i), f->esp);
}

/*
 * pseudOS: Checks if a stack growth is requiered and grows the stack in case.
 */
static void
check_stack_growth(void *vaddr, void *esp)
{
	// printf(" -0- check_stack_growth \n");
	if(vaddr == NULL || !is_user_vaddr(vaddr) 
		|| pg_round_down(vaddr) == NULL || !is_user_vaddr(pg_round_down(vaddr))
		|| vaddr < USER_VADDR_BOTTOM ) 
		exit (SYSCALL_ERROR);

	// printf(" -1- check_stack_growth \n");
	struct spt_entry_t *e = spt_lookup (thread_current ()->spt, vaddr);

	if(e)
	{
		// printf(" -1.1- check_stack_growth \n");
		spt_load_page (e);
		// printf(" -1.2- check_stack_growth \n");
		// if(!spt_load_page (e))
		// 	exit (SYSCALL_ERROR);	// maybe not needed
	}
	else if(vaddr >= (esp - 32))
    {
	    // pseudOS: create a new page for the stack, if there is no entry 
		// for vaddr in the supplemental page table and if vaddr is higher 
		// than (esp - 32) 
		// printf(" -1.3- check_stack_growth \n");
	    if(!stack_growth (vaddr))
	    	exit (SYSCALL_ERROR);
	} else 
	{
		exit (SYSCALL_ERROR);
	}

	// printf(" -2- check_stack_growth \n");

}

/*
 * pseudOS: Checks for a given buffer if a stack growth is required.
 */
static void
check_buffer (void *buffer, unsigned size, void *esp)
{
	unsigned i;
	for (i = 0; i < size; i++)
		check_stack_growth(buffer+i, esp);
}
