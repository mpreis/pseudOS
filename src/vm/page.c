/*
 * pseudOS: supplemental page table
 */
#include "vm/page.h"
#include "vm/frame.h"
#include "devices/timer.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "userprog/pagedir.h"

static void spt_entry_free (struct hash_elem *e, void *aux);

static struct lock spt_lock;

void 
spt_init(struct hash *spt)
{
	lock_init (&spt_lock);
	hash_init (spt, spt_entry_hash, spt_entry_less, NULL);
}

bool
spt_insert (struct hash *spt, struct file *file, off_t ofs, 
	uint8_t *upage, uint32_t read_bytes, uint32_t zero_bytes,
	bool writable, enum spt_entry_type type)
{
	lock_acquire (&spt_lock);
	ASSERT(spt_lookup (spt, upage) == NULL);
	
	struct spt_entry_t *e = malloc(sizeof(struct spt_entry_t));
	e->file = file;
	e->ofs = ofs;
	e->upage = upage;
	e->read_bytes = read_bytes;
	e->zero_bytes = zero_bytes;
	e->writable = writable;
	e->type = type;
	e->lru_ticks = timer_ticks();
	
	struct hash_elem *he = hash_insert (spt, &e->hashelem);
	lock_release (&spt_lock);
  	return he == NULL;
}

void
spt_remove (struct hash *spt, void *upage)
{
	lock_acquire (&spt_lock);
	struct spt_entry_t *e = spt_lookup (spt, upage);
	ASSERT(e != NULL);
	hash_delete (spt, &e->hashelem);
  	lock_release (&spt_lock);
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

static void
spt_entry_free (struct hash_elem *e, void *aux UNUSED)
{
	struct thread *t = thread_current ();
	struct spt_entry_t *spt_e = hash_entry (e, struct spt_entry_t, hashelem);

	if(pagedir_is_dirty (t->pagedir, spt_e->upage))
	{
		// TODO: handle dirty page when deallocating
		pagedir_set_dirty (t->pagedir, spt_e->upage, false);
	}
	if(pagedir_is_accessed (t->pagedir, spt_e->upage))
	{
		// TODO: handle accessed page when deallocating
		pagedir_set_accessed (t->pagedir, spt_e->upage, false);
	}

	frame_table_remove (spt_e->upage);
	free(spt_e);
}
