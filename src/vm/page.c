/*
 * pseudOS: supplemental page table
 */
#include "vm/page.h"
#include "devices/timer.h"
#include "threads/malloc.h"
#include "threads/synch.h"

static struct lock spt_lock;	/* pseudOS: Lock variable to ensure a secure execution of a system-call. */


void 
spt_init(struct hash *spt)
{
	lock_init (&spt_lock);
	hash_init (spt, spt_entry_hash, spt_entry_less, NULL);
}

void
spt_insert (struct hash *spt, void *vaddr)
{
	lock_acquire (&spt_lock);
	ASSERT(spt_lookup (spt, vaddr) == NULL);
	struct spt_entry_t *e = malloc(sizeof(struct spt_entry_t));
	e->vaddr = vaddr;
	e->swap_ptr = NULL;
	e->file_ptr = NULL;
	e->lru_ticks = timer_ticks();
	hash_insert (spt, &e->hashelem);
  	lock_release (&spt_lock);
}

void
spt_remove (struct hash *spt, void *vaddr)
{
	lock_acquire (&spt_lock);
	struct spt_entry_t *e = spt_lookup (spt, vaddr);
	ASSERT(e != NULL);
	hash_delete (spt, &e->hashelem);
  	lock_release (&spt_lock);
}

/* Returns a hash value for page p. */
unsigned
spt_entry_hash (const struct hash_elem *e_, void *aux UNUSED)
{
	const struct spt_entry_t *e = hash_entry (e_, struct spt_entry_t, hashelem);
	return hash_bytes (&e->vaddr, sizeof e->vaddr);
}

/* Returns true if page a precedes page b. */
bool
spt_entry_less (const struct hash_elem *a_, const struct hash_elem *b_,
           void *aux UNUSED)
{
	const struct spt_entry_t *a = hash_entry (a_, struct spt_entry_t, hashelem);
	const struct spt_entry_t *b = hash_entry (b_, struct spt_entry_t, hashelem);

	return a->vaddr < b->vaddr;
}

/* Returns the page containing the given virtual address,
   or a null pointer if no such page exists. */
struct spt_entry_t *
spt_lookup (struct hash *spt, const void *vaddr)
{
  struct spt_entry_t e;
  struct hash_elem *helem;

  e.vaddr = (void *) vaddr;
  helem = hash_find (spt, &e.hashelem);
  return helem != NULL ? hash_entry (helem, struct spt_entry_t, hashelem) : NULL;
}