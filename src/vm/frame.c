/*
 * pseudOS
 */
#include "vm/frame.h"
#include "threads/loader.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include <stdlib.h>

#define FRAME_FAILED -1

static struct frame_table_entry_t *frame_table_get_entry (void *vaddr);
static int frame_table_get_free_frame (void);
static struct frame_table_t *frame_table;

void
frame_table_init (void)
{
	frame_table = malloc(sizeof(struct frame_table_t));
	init_frame_table (frame_table);
}

void 
init_frame_table(struct frame_table_t *ft)
{
	ASSERT (ft != NULL);
	ft->used_frames = bitmap_create (init_ram_pages);
	list_init (&ft->frames);
}

void
frame_table_insert (void *vaddr)
{
	struct frame_table_entry_t *fte = frame_table_get_entry (vaddr);
	if(fte != NULL)
	{
		// frame already exists
		return;
	}

	int new_frame_idx = frame_table_get_free_frame ();
	ASSERT (new_frame_idx != FRAME_FAILED);

	bitmap_set (frame_table->used_frames, new_frame_idx , true);
	
	struct frame_table_entry_t *new_fte = malloc(sizeof(struct frame_table_entry_t));
	new_fte->vaddr = vaddr;
	new_fte->used_frames_idx = new_frame_idx;
	list_push_back (&frame_table->frames, &new_fte->ftelem);
}

void
frame_table_remove (void *vaddr)
{
	struct frame_table_entry_t *fte = frame_table_get_entry (vaddr);
	if(fte != NULL)
	{
		bitmap_set (frame_table->used_frames, fte->used_frames_idx, false);
		list_remove (&fte->ftelem);
	}
}

static struct frame_table_entry_t *
frame_table_get_entry (void *vaddr)
{
	struct list_elem *e;
	for (e = list_begin (&frame_table->frames); 
		 e != list_end (&frame_table->frames); 
		 e = list_next (e))
	{
		struct frame_table_entry_t *fte = list_entry (e, struct frame_table_entry_t, ftelem);
		if(fte->vaddr == vaddr)
			return fte;
	}
	return NULL;
}

static int
frame_table_get_free_frame (void)
{
	unsigned i;
	for (i = 0; i < init_ram_pages; i++)
		if(! bitmap_test (frame_table->used_frames, i))
			return i;
	return FRAME_FAILED;
}