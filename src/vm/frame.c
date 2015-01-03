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
#include "userprog/syscall.h"
#include "vm/frame.h"
#include "vm/swap.h"
#include <list.h>
#include <stdlib.h>


#define FRAME_FAILED -1
#define FRAME_TABLE_SIZE 300 // TODO
#define FRAME_FREE false
#define FRAME_USED true

static struct lock ft_lock;

static struct frame_table_entry_t *frame_table_get_entry (void *upage);
static int frame_table_get_free_frame (void);
static struct frame_table_t *frame_table;
static int frame_table_evict_frame (void);

void
frame_table_init (void)
{
	frame_table = malloc(sizeof(struct frame_table_t));
	lock_init (&ft_lock);
	init_frame_table (frame_table);
}

void 
init_frame_table(struct frame_table_t *ft)
{
	ASSERT (ft != NULL);
	ft->used_frames = bitmap_create (FRAME_TABLE_SIZE);
	bitmap_set_all (ft->used_frames, FRAME_FREE);
	list_init (&ft->frames);
}

bool
frame_table_insert (struct spt_entry_t *spte)
{
	if(is_kernel_vaddr (spte->upage))
		return true;

	int new_frame_idx = frame_table_get_free_frame ();
	ASSERT (new_frame_idx != FRAME_FAILED);
	
	lock_acquire (&ft_lock);
	bitmap_set (frame_table->used_frames, new_frame_idx , FRAME_USED);
	
	struct frame_table_entry_t *new_fte = malloc(sizeof(struct frame_table_entry_t));
	new_fte->spte = spte;
	new_fte->owner = thread_current ();
	new_fte->used_frames_idx = new_frame_idx;
	list_push_back (&frame_table->frames, &new_fte->listelem);
	lock_release (&ft_lock);
	return true;
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
		bitmap_set (frame_table->used_frames, fte->used_frames_idx, FRAME_FREE);
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
	for (e = list_begin (&frame_table->frames); 
		 e != list_end (&frame_table->frames); 
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

static int
frame_table_get_free_frame (void)
{
	lock_acquire (&ft_lock);
	unsigned i;
	for (i = 0; i < FRAME_TABLE_SIZE; i++)
		if(! bitmap_test (frame_table->used_frames, i))
		{
			lock_release (&ft_lock);
			return i;
		}
	lock_release (&ft_lock);
	return frame_table_evict_frame ();
}

static int
frame_table_evict_frame (void)
{	
	lock_acquire (&ft_lock);
	struct frame_table_entry_t *fte = list_entry(list_begin(&frame_table->frames), 
												 struct frame_table_entry_t, listelem);

	int evicted_frame_idx = fte->used_frames_idx;	// TODO
	void * kpage = pagedir_get_page (fte->owner->pagedir, fte->spte->upage);
	
	if(fte->spte->type == SPT_ENTRY_TYPE_SWAP)
	{
		if(!pagedir_is_dirty (fte->owner->pagedir, fte->spte->upage))
		{
			printf(" --- not dirty \n");
		}
		fte->spte->swap_page_index = (pagedir_is_dirty (fte->owner->pagedir, fte->spte->upage))
			? swap_evict (fte->spte->upage)
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

	pagedir_set_accessed (fte->owner->pagedir, fte->spte->upage, false);
	pagedir_clear_page (fte->owner->pagedir, fte->spte->upage);
	palloc_free_page (kpage);

	lock_release (&ft_lock);
	frame_table_remove (fte->spte->upage);
	return evicted_frame_idx;
}