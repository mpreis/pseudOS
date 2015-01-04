/*
 * pseudOS: supplemental page table
 */
#include "devices/timer.h"
#include "filesys/off_t.h"
#include "filesys/file.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/interrupt.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "userprog/syscall.h"
#include "vm/frame.h"
#include "vm/page.h"
#include "vm/swap.h"

#include <string.h>

static bool spt_load_page_swap (struct spt_entry_t *spte);
static bool spt_load_page_file (struct spt_entry_t *spte);
	
void 
spt_init(struct hash *spt)
{
	hash_init (spt, spt_entry_hash, spt_entry_less, NULL);
}
	
void 
spt_init_lock()
{
	lock_init (&spt_lock);
}

struct spt_entry_t * 
spt_insert (struct hash *spt, struct file *file, off_t ofs, uint8_t *upage, 
	uint32_t read_bytes, uint32_t zero_bytes, bool writable, enum spt_entry_type_t type)
{
	ASSERT ( (read_bytes + zero_bytes) == PGSIZE );
	//ASSERT(spt_lookup (spt, upage) == NULL);
	if(spt_lookup (spt, upage) != NULL)
	{
		return NULL;
	}

	struct spt_entry_t *e = malloc(sizeof(struct spt_entry_t));
	e->file = file;
	e->ofs = ofs;
	e->upage = upage;
	e->read_bytes = read_bytes;
	e->zero_bytes = zero_bytes;
	e->writable = writable;
	e->type = type;
	e->swap_page_index = SWAP_INIT_IDX;
	e->lru_ticks = timer_ticks();
	
	struct hash_elem *he = hash_insert (spt, &e->hashelem);

	if(he != NULL) 
	{
		free (e);
		return NULL;
	}
	return e;
}

struct spt_entry_t *
spt_remove (struct hash *spt, void *upage)
{
	struct spt_entry_t *e = spt_lookup (spt, upage);
	if(e != NULL)
	{
		hash_delete (spt, &e->hashelem);
  		return e;
  	}
  	return NULL;
}

/* Returns a hash value for page p. */
unsigned
spt_entry_hash (const struct hash_elem *e_, void *aux UNUSED)
{
	const struct spt_entry_t *e = hash_entry (e_, struct spt_entry_t, hashelem);
	return hash_bytes (&e->upage, sizeof e->upage);
}

/* Returns true if page a precedes page b. */
bool
spt_entry_less (const struct hash_elem *a_, const struct hash_elem *b_,
           void *aux UNUSED)
{
	const struct spt_entry_t *a = hash_entry (a_, struct spt_entry_t, hashelem);
	const struct spt_entry_t *b = hash_entry (b_, struct spt_entry_t, hashelem);

	return a->upage < b->upage;
}

/* Returns the page containing the given virtual address,
   or a null pointer if no such page exists. */
struct spt_entry_t *
spt_lookup (struct hash *spt, const void *upage)
{
  struct spt_entry_t e;
  struct hash_elem *helem;

  e.upage = (void *) pg_round_down(upage);
  helem = hash_find (spt, &e.hashelem);
  return helem != NULL ? hash_entry (helem, struct spt_entry_t, hashelem) : NULL;
}

/* pseudOS: Frees all resources of the supplemental page table
 * (including the supplemental page table itself).
 */
void
spt_free (struct hash *spt)
{
	hash_destroy (spt, spt_entry_free);
	free (spt);
}

void
spt_entry_free (struct hash_elem *e, void *aux UNUSED)
{
	struct thread *t = thread_current ();
	struct spt_entry_t *spte = hash_entry (e, struct spt_entry_t, hashelem);

	if(pagedir_is_dirty (t->pagedir, spte->upage))
		pagedir_set_dirty (t->pagedir, spte->upage, false);

	if(pagedir_is_accessed (t->pagedir, spte->upage))
		pagedir_set_accessed (t->pagedir, spte->upage, false);

	frame_table_remove (&spte->upage);				/* release frame. */
	pagedir_clear_page (t->pagedir, spte->upage);	/* remove pagedir entry. */
	free (spte);									/* free entry itself. */
}

bool
spt_load_page (struct spt_entry_t *spte)
{
	if(spte == NULL) return false;

	struct thread *t = thread_current ();
	if(pagedir_get_page (t->pagedir, spte->upage))
		return true;

	spte->pinned = true;
	bool status = false;
	if(spte->swap_page_index != SWAP_INIT_IDX) 
	{
		status = spt_load_page_swap(spte);
		if (status)
			pagedir_set_dirty (t->pagedir, spte->upage, true);
	}
	else 
		status = spt_load_page_file(spte);

	if (status)
		pagedir_set_accessed (t->pagedir, spte->upage, true);

	return status;
}

static bool
spt_load_page_swap (struct spt_entry_t *spte)
{	
	if(!frame_table_insert (spte))
		return false;

	swap_free (spte->swap_page_index, spte->upage);
	spte->swap_page_index = SWAP_INIT_IDX;
	spte->pinned = false;

	return true;
}

static bool
spt_load_page_file (struct spt_entry_t *spte)
{
	void * kpage = frame_table_insert (spte);
	if(!kpage) return false;
	
	lock_acquire (&syscall_lock);
	if(spte->read_bytes > 0)
	{
		off_t read_bytes = file_read_at (spte->file, kpage, spte->read_bytes, spte->ofs);
		
		if((off_t)spte->read_bytes != read_bytes)
		{
			palloc_free_page (kpage);
			frame_table_remove (spte->upage);
			lock_release (&syscall_lock);
			return false;
		} 
		memset(kpage + spte->read_bytes, 0, spte->zero_bytes);
	}

	spte->pinned = false;
	lock_release (&syscall_lock);
	return true;
}
