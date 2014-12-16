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
	void *vaddr;
	int used_frames_idx;
};


void frame_table_init (void);
void init_frame_table(struct frame_table_t *ft);
void frame_table_insert (void *vaddr);
void frame_table_remove (void *vaddr);

#endif