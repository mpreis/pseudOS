/*
 * pseudOS
 */
#include "filesys/file.h"
#include "filesys/off_t.h"
#include "threads/loader.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "userprog/syscall.h"
#include "vm/frame.h"
#include "vm/swap.h"
#include <list.h>
#include <stdlib.h>

#define FRAME_FAILED -1

static struct lock ft_lock;
static struct list frame_table;

static struct frame_table_entry_t *frame_table_get_entry (void *upage);
static void * frame_table_evict_frame (void);

void
frame_table_init (void)
{
	lock_init (&ft_lock);
	init_frame_table (&frame_table);
}

void 
init_frame_table(struct list *ft)
{
	ASSERT (ft != NULL);
	list_init (ft);
}

void *
frame_table_insert (struct spt_entry_t *spte)
{
	if(is_kernel_vaddr (spte->upage)) 
		return NULL;

	lock_acquire (&ft_lock);
	
	void * kpage = palloc_get_page ( PAL_USER | PAL_ZERO );

	if(!kpage)
	{
		frame_table_evict_frame ();
		kpage = palloc_get_page ( PAL_USER | PAL_ZERO );
		if(!kpage)
		{	
			PANIC ("Unable to palloc page!");
		}
	}

	if (!install_page (spte->upage, kpage, spte->writable)) 
	{
		palloc_free_page (kpage);
		lock_release (&ft_lock);
		return NULL;
	}
	
	struct frame_table_entry_t *new_fte = malloc(sizeof(struct frame_table_entry_t));
	new_fte->spte = spte;
	new_fte->owner = thread_current ();
	list_push_back (&frame_table, &new_fte->listelem);
	
	lock_release (&ft_lock);
	return kpage;
}

void
frame_table_remove (void *upage)
{
	if(is_kernel_vaddr (upage)) 
		return;
	
	struct frame_table_entry_t *fte = frame_table_get_entry (upage);
	if(fte != NULL)
	{
		lock_acquire (&ft_lock);
		list_remove (&fte->listelem);
		free (fte);
		lock_release (&ft_lock);
	} 
	else 
	{
		PANIC ("Cannot find frame (upage=%p)!", upage);
	}
}

static struct frame_table_entry_t *
frame_table_get_entry (void *upage)
{
	if(upage == NULL)
		return NULL;

	lock_acquire (&ft_lock);
	struct list_elem *e;
	for (e = list_begin (&frame_table); 
		 e != list_end (&frame_table); 
		 e = list_next (e))
	{
		struct frame_table_entry_t *fte = list_entry (e, struct frame_table_entry_t, listelem);
		if(fte->spte->upage == upage)
		{
			lock_release (&ft_lock);
			return fte;
		}
	}
	lock_release (&ft_lock);
	return NULL;
}


static void *
frame_table_evict_frame (void)
{	
	struct list_elem *e = list_begin(&frame_table);
	struct frame_table_entry_t *fte = NULL;
	while(fte == NULL)
	{	
		struct frame_table_entry_t *tmp_fte = list_entry(e, struct frame_table_entry_t, listelem);
		if(!tmp_fte->spte->pinned)
		{
			if( pagedir_is_accessed (tmp_fte->owner->pagedir, tmp_fte->spte->upage) )
				pagedir_set_accessed (tmp_fte->owner->pagedir, tmp_fte->spte->upage, false);	
			else
				fte = tmp_fte;
		}
		e = list_next(e);
		if(e == list_end(&frame_table))
			e = list_begin(&frame_table);
	}	
	
	void *kpage = pagedir_get_page (fte->owner->pagedir, fte->spte->upage);
	if(fte->spte->type == SPT_ENTRY_TYPE_SWAP)
	{
		fte->spte->swap_page_index = (pagedir_is_dirty (fte->owner->pagedir, fte->spte->upage))
			? swap_evict (kpage)
			: SWAP_INIT_IDX;
	} 
	else if (fte->spte->type == SPT_ENTRY_TYPE_MMAP)
	{
		fte->spte->swap_page_index = SWAP_INIT_IDX;
		lock_acquire (&syscall_lock);
		off_t written_bytes = file_write_at (fte->spte->file, kpage, 
											 fte->spte->read_bytes, fte->spte->ofs);
		lock_release (&syscall_lock);
		if((off_t)fte->spte->read_bytes != written_bytes)
		{
			PANIC ("Cannot write all bytes (%d/%d)", written_bytes, fte->spte->read_bytes);
		}
	}
	else
	{
		PANIC ("Invalid supplement page table entry type (upage=%p, type=%d)!", 
			fte->spte->upage, fte->spte->type);
	}

	pagedir_set_accessed (fte->owner->pagedir, fte->spte->upage, false);
	pagedir_clear_page (fte->owner->pagedir, fte->spte->upage);
	list_remove (&fte->listelem);
	palloc_free_page (kpage);
	free (fte);

	return kpage;
}