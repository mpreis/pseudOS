#ifndef FRAME_H
#define FRAME_H
/*
 * pseudOS
 */
#include <list.h>
#include <bitmap.h>

struct frame_table_t 
{
	struct bitmap *used_frames;
	struct list frames;
};

struct frame_table_entry_t
{
	struct list_elem ftelem;
	void *page_ptr;
	int used_frames_idx;
};


void frame_table_init (void);
void init_frame_table(struct frame_table_t *ft);
void frame_table_insert (void *upage);
void frame_table_remove (void *upage);

#endif