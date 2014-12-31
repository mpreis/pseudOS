/*
 * pseudOS
 */
#include "vm/frame.h"
#include "vm/swap.h"
#include "threads/loader.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include <list.h>
#include <stdlib.h>

#define FRAME_FAILED -1

static struct lock ft_lock;

static struct frame_table_entry_t *frame_table_get_entry (void *vaddr);
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
	ft->used_frames = bitmap_create (init_ram_pages);
	list_init (&ft->frames);
}

bool
frame_table_insert (void *vaddr)
{
	struct frame_table_entry_t *fte = frame_table_get_entry (vaddr);
	if(fte != NULL) return false;	// frame already exists
	
	int new_frame_idx = frame_table_get_free_frame ();
	ASSERT (new_frame_idx != FRAME_FAILED);
	
	lock_acquire (&ft_lock);
	
	bitmap_set (frame_table->used_frames, new_frame_idx , true);
	
	struct frame_table_entry_t *new_fte = malloc(sizeof(struct frame_table_entry_t));
	new_fte->vaddr = vaddr;
	new_fte->used_frames_idx = new_frame_idx;
	list_push_back (&frame_table->frames, &new_fte->ftelem);
	
	lock_release (&ft_lock);
	return true;
}

void
frame_table_remove (void *vaddr)
{
	struct frame_table_entry_t *fte = frame_table_get_entry (vaddr);
	if(fte != NULL)
	{
		lock_acquire (&ft_lock);
		bitmap_set (frame_table->used_frames, fte->used_frames_idx, false);
		list_remove (&fte->ftelem);
		lock_release (&ft_lock);
	}
}

static struct frame_table_entry_t *
frame_table_get_entry (void *vaddr)
{
	if(vaddr == NULL)
		return NULL;

	lock_acquire (&ft_lock);
	struct list_elem *e;
	for (e = list_begin (&frame_table->frames); 
		 e != list_end (&frame_table->frames); 
		 e = list_next (e))
	{
		struct frame_table_entry_t *fte = list_entry (e, struct frame_table_entry_t, ftelem);
		if(fte->vaddr == vaddr)
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
	for (i = 0; i < init_ram_pages; i++)
		if(! bitmap_test (frame_table->used_frames, i))
		{
			lock_release (&ft_lock);
			return i;
		}
	lock_release (&ft_lock);
	return false; //frame_table_evict_frame ();
}

static int
frame_table_evict_frame (void)
{
	lock_acquire (&ft_lock);
	// pseudOS: select frame to evict
	int evicted_frame_idx = 0;	// TODO
	struct frame_table_entry_t *eframe = list_entry(list_pop_front(&frame_table->frames), 
													struct frame_table_entry_t, ftelem);

	// pseudOS: write frame to swap space
	swap_evict (eframe->vaddr);

	// pseudOS: free allocated resources of evicted frame
	palloc_free_page(eframe->vaddr);
	pagedir_clear_page(thread_current ()->pagedir, eframe->vaddr);

	lock_release (&ft_lock);
	return evicted_frame_idx;
}