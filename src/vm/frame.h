#ifndef FRAME_H
#define FRAME_H
/*
 * pseudOS
 */
#include "vm/page.h"
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
	int used_frames_idx;
	struct thread *owner;
	struct spt_entry_t *spte;
};

void frame_table_init (void);
void init_frame_table(struct frame_table_t *ft);
bool frame_table_insert (struct spt_entry_t *stpe);
void frame_table_remove (void *upage);

#endif