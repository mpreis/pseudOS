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

static void syscall_handler (struct intr_frame *);
static bool is_valid_fd(int fd);				/* pseudOS: Checks if the given file-descriptor is valid. */
static void check_args (struct intr_frame *f, unsigned nr_of_args);
static void check_buffer (void *vaddr, unsigned size, void *esp);
static void check_usr_ptr(void *vaddr, void *esp);
static bool is_valid_mapid(mapid_t mapping);
static bool is_valid_mapping (void *addr, off_t file_len);
static bool is_mapped_file (int fd);

static void set_spte_pin (void * addr, bool pinned);
static void set_args_pin (struct intr_frame *f, unsigned nr_of_args, bool pinned);
static void set_buffer_pin (void * buffer, unsigned size, bool pinned);

void
syscall_init (void) 
{
	lock_init (&syscall_lock);
	intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

void
syscall_handler (struct intr_frame *f) 
{
	switch(*(uint32_t *) (f->esp))
	{
		case SYS_HALT: 
			halt();
			break;

		case SYS_EXIT: 
			check_args (f, 1);
			set_args_pin (f, 1, SPT_PINNED);
			exit ( *(int *)(f->esp + OFFSET_ARG) );
			set_args_pin (f, 1, SPT_UNPINNED);
			break;

		case SYS_EXEC:
			check_args (f, 1);
			check_usr_ptr (*(char **)(f->esp + OFFSET_ARG), f->esp);
			set_args_pin (f, 1, SPT_PINNED);
			f->eax = exec ( *(char **)(f->esp + OFFSET_ARG) );
			set_args_pin (f, 1, SPT_UNPINNED);
			break; 

		case SYS_WAIT: 
			check_args (f, 1);
			set_args_pin (f, 1, SPT_PINNED);
			f->eax = wait ( *(pid_t *)(f->esp + OFFSET_ARG) );
			set_args_pin (f, 1, SPT_UNPINNED);
			break;

		case SYS_CREATE: 
			check_args (f, 2);
			check_usr_ptr (*(char **)(f->esp + OFFSET_ARG), f->esp);
			
			set_args_pin (f, 2, SPT_PINNED);
			f->eax = create ( 
				*(char **)(f->esp + OFFSET_ARG), 
				*(unsigned int *)(f->esp + OFFSET_ARG * 2) );
			
			set_args_pin (f, 2, SPT_UNPINNED);
			break;

		case SYS_REMOVE: 
			check_args (f, 1);
			check_usr_ptr (*(char **)(f->esp + OFFSET_ARG), f->esp);
			set_args_pin (f, 1, SPT_PINNED);
			f->eax = remove ( *(char **)(f->esp + OFFSET_ARG) );
			set_args_pin (f, 1, SPT_UNPINNED);
			break;

		case SYS_OPEN: 
			check_args (f, 1);
			check_usr_ptr (*(char **)(f->esp + OFFSET_ARG), f->esp);
			set_args_pin (f, 1, SPT_PINNED);
			f->eax = open ( *(char **)(f->esp + OFFSET_ARG) );
			set_args_pin (f, 1, SPT_UNPINNED);
			break;

		case SYS_FILESIZE: 
			check_args (f, 1);
			set_args_pin (f, 1, SPT_PINNED);
			f->eax = filesize ( *(int *)(f->esp + OFFSET_ARG) );
			set_args_pin (f, 1, SPT_UNPINNED);
			break;

		case SYS_READ:
			check_args (f, 3);
			check_buffer (
				*(char **)(f->esp + OFFSET_ARG * 2), 
				*(unsigned int *)(f->esp + OFFSET_ARG * 3), 
				f->esp);
			
			set_args_pin (f, 3, SPT_PINNED);
			set_buffer_pin (
				*(char **)(f->esp + OFFSET_ARG * 2), 
				*(unsigned int *)(f->esp + OFFSET_ARG * 3),
				SPT_PINNED);

			f->eax = read (
				*(int *)(f->esp + OFFSET_ARG), 
				*(char **)(f->esp + OFFSET_ARG * 2), 
				*(unsigned int *)(f->esp + OFFSET_ARG * 3) );
			
			set_buffer_pin (
				*(char **)(f->esp + OFFSET_ARG * 2), 
				*(unsigned int *)(f->esp + OFFSET_ARG * 3),
				SPT_UNPINNED);
			set_args_pin (f, 3, SPT_UNPINNED);
			break;
		
		case SYS_WRITE:
			check_args (f, 3);
			check_buffer(
				*(char **)(f->esp + OFFSET_ARG * 2), 
				*(unsigned int *)(f->esp + OFFSET_ARG * 3), 
				f->esp);
			
			set_args_pin (f, 3, SPT_PINNED);
			set_buffer_pin (
				*(char **)(f->esp + OFFSET_ARG * 2), 
				*(unsigned int *)(f->esp + OFFSET_ARG * 3),
				SPT_PINNED);

			f->eax = write ( 
				*(int *)(f->esp + OFFSET_ARG), 
				*(char **)(f->esp + OFFSET_ARG * 2), 
				*(unsigned int *)(f->esp + OFFSET_ARG * 3) );
			
			set_buffer_pin (
				*(char **)(f->esp + OFFSET_ARG * 2), 
				*(unsigned int *)(f->esp + OFFSET_ARG * 3),
				SPT_UNPINNED);
			set_args_pin (f, 3, SPT_UNPINNED);
			break;

		case SYS_SEEK: 
			check_args (f, 2);
			set_args_pin (f, 2, SPT_PINNED);
			seek( 
				*(int *)(f->esp + OFFSET_ARG ), 
				*(unsigned int *)(f->esp + OFFSET_ARG * 2) );
			set_args_pin (f, 2, SPT_UNPINNED);
			break;

		case SYS_TELL: 
			check_args (f, 1);
			set_args_pin (f, 1, SPT_PINNED);
			f->eax = tell ( *(int *)(f->esp + OFFSET_ARG) );
			set_args_pin (f, 1, SPT_UNPINNED);
			break;

		case SYS_CLOSE: 
			check_args (f, 1);
			set_args_pin (f, 1, SPT_PINNED);
			close ( *(int *)(f->esp + OFFSET_ARG) );
			set_args_pin (f, 1, SPT_UNPINNED);
			break;

		case SYS_MMAP:
			check_args (f, 2);
			set_args_pin (f, 2, SPT_PINNED);
			f->eax = mmap (
				*(int *)(f->esp + OFFSET_ARG), 
				*(void **)(f->esp + OFFSET_ARG * 2) );
			set_args_pin (f, 2, SPT_UNPINNED);
			break;

		case SYS_MUNMAP:
			check_args (f, 1);
			set_args_pin (f, 1, SPT_PINNED);
			munmap ( *(mapid_t *)(f->esp + OFFSET_ARG) );
			set_args_pin (f, 1, SPT_UNPINNED);
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
 	// lock_acquire (&syscall_lock);
 	pid_t pid = process_execute (cmd_line);
 	struct child_process *cp = thread_get_child (pid);
  
	sema_down (&cp->init);

	// lock_release (&syscall_lock);
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
		sema_up (&cp->alive);
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
	if( ! is_valid_fd (fd) || is_mapped_file (fd)
		||  thread_current ()->fds[fd - FD_INIT] == NULL)
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
	
	struct mapped_file_t *mfile = malloc(sizeof(struct mapped_file_t));
	if (!mfile) return MAP_FAILED;
	mfile->fd = fd;
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
				read_bytes, zero_bytes, true, SPT_UNPINNED, SPT_ENTRY_TYPE_MMAP);
		if(! spte)
		{
			munmap (mfile->mapid);
			return MAP_FAILED;
		}

		list_push_back (&mfile->spt_entries, &spte->listelem);

		addr += PGSIZE;
 		flen -= read_bytes;
		ofs  += read_bytes;
	}
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
			struct file *file = t->fds[mfile->fd - FD_INIT];
			struct list_elem *mfe_next;
			struct list_elem *mfe = list_begin (&mfile->spt_entries);
			while (mfe != list_end (&mfile->spt_entries))
			{
				mfe_next  = list_next (mfe);
				struct spt_entry_t *spte = list_entry (mfe, struct spt_entry_t, listelem);
				spte->pinned = SPT_PINNED;
				void *kpage = pagedir_get_page (t->pagedir, spte->upage);

				if (kpage && file && pagedir_is_dirty (t->pagedir, spte->upage))
				{
					lock_acquire (&syscall_lock);
					off_t written_bytes = file_write_at (file, kpage, spte->read_bytes, spte->ofs);
					lock_release (&syscall_lock);
					if((off_t)spte->read_bytes != written_bytes)
						exit (SYSCALL_ERROR);
				}
				spt_remove (t->spt, spte->upage);		/* remove supplemental page table entry. */
				spt_entry_free (&spte->hashelem, NULL);	/* free resources of entry. */
				mfe = mfe_next;
			}
			lock_acquire (&syscall_lock);
			close (mfile->fd);
			lock_release (&syscall_lock);
			list_remove (&mfile->elem);
			free (mfile);
		}
		e = next;
	} /* end iteration over mapped files */
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
check_args (struct intr_frame *f, unsigned nr_of_args)
{
	unsigned i;
	for (i = 1; i <= nr_of_args; i++)
		check_usr_ptr((f->esp + OFFSET_ARG * i), f->esp);
}

/*
 * pseudOS: Checks if a page has to be loaded or a stack growth is requiered 
 * and grows the stack in case.
 */
static void
check_usr_ptr (void *vaddr, void *esp)
{
	if(vaddr == NULL || !is_user_vaddr(vaddr))
		exit (SYSCALL_ERROR);

	struct spt_entry_t *e = spt_lookup (thread_current ()->spt, vaddr);

	if(e)
	{
		spt_load_page (e);
	}
	else if(vaddr >= (esp - 32))
    {
	    // pseudOS: create a new page for the stack, if there is no entry 
		// for vaddr in the supplemental page table and if vaddr is higher 
		// than (esp - 32) 
	    if(!stack_growth (vaddr))
	    	exit (SYSCALL_ERROR);
	} else 
		exit (SYSCALL_ERROR);
}

/*
 * pseudOS: Check buffer pages are valid.
 */
static void
check_buffer (void *buffer, unsigned size, void *esp)
{
	unsigned i;
	for (i = 0; i < size; i++)
		check_usr_ptr(buffer+i, esp);
}

/*
 * pseudOS: Returns true if there is a mapping for the given FD, otherwise false.
 */
static bool
is_mapped_file (int fd)
{
	struct thread *t = thread_current ();
	struct list_elem *e;
	for (e  = list_begin (&t->mapped_files);
		 e != list_end (&t->mapped_files);
		 e  = list_next (e))
	{
		struct mapped_file_t *mfile = list_entry (e, struct mapped_file_t, elem);
		if(mfile->fd == fd)
			return true;
	}
	return false;
}

/*
 * pseudOS: Looks up ADDR in the supplemental page table. 
 * If ther is an entry pinned is set to PINNED.
 */
static void
set_spte_pin (void * addr, bool pinned)
{
	struct spt_entry_t *spte = spt_lookup (thread_current ()->spt, addr);
	if(spte != NULL)
		spte->pinned = pinned;	
}

/*
 * pseudOS: Sets NR_OF_ARGS values of the stack PINNED.
 */
static void
set_args_pin (struct intr_frame *f, unsigned nr_of_args, bool pinned)
{
	unsigned i;
	for (i = 1; i <= nr_of_args; i++)
	{	
		void * addr = (f->esp + OFFSET_ARG * i);
		set_spte_pin (addr, pinned);
	}
}

/*
 * pseudOS: Sets the pages of the given buffer to PINNED.
 */
static void 
set_buffer_pin (void * buffer, unsigned size, bool pinned)
{
	unsigned i;
	for (i = 0; i < size; i++)
		set_spte_pin (buffer + i, pinned);
}
