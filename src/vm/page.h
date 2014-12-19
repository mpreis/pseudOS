#ifndef PAGE_H
#define PAGE_H
/*
 * pseudOS
 */
#include <debug.h>
#include <hash.h>

struct spt_entry_t 
{
	struct hash_elem hashelem;
	void *vaddr;
	int64_t lru_ticks;
	void *swap_ptr;
	void *file_ptr;
	bool writeable;
	//int sharing_ctr;
};

void spt_init(struct hash *spt);
void spt_insert (struct hash *spt, void *vaddr, bool writeable);
void spt_remove (struct hash *spt, void *vaddr);
unsigned spt_entry_hash (const struct hash_elem *p_, void *aux UNUSED);
bool spt_entry_less (const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED);
struct spt_entry_t * spt_lookup (struct hash *spt, const void *vaddr);
void spt_free (struct hash *spt);

#endif