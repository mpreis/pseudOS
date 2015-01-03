#ifndef PAGE_H
#define PAGE_H
/*
 * pseudOS
 */
#include <debug.h>
#include <hash.h>
#include "filesys/off_t.h"

#define SWAP_INIT_IDX -1

enum spt_entry_type_t
{
	SPT_ENTRY_TYPE_MMAP,
	SPT_ENTRY_TYPE_SWAP,
	SPT_ENTRY_TYPE_CNTR
};

struct spt_entry_t 
{
	struct hash_elem hashelem;
	struct list_elem listelem;
	int64_t lru_ticks;
	struct file *file;
	off_t ofs;
	uint8_t *upage;
	uint32_t read_bytes;
	uint32_t zero_bytes;
	bool writable;
	int32_t swap_page_index;
	enum spt_entry_type_t type;
};

struct lock spt_lock;

void spt_init(struct hash *spt);
void spt_init_lock(void);
struct spt_entry_t * spt_insert (struct hash *spt, struct file *file, off_t ofs, 
	uint8_t *upage, uint32_t read_bytes, uint32_t zero_bytes, bool writable, enum spt_entry_type_t type);
struct spt_entry_t * spt_remove (struct hash *spt, void *upage);
unsigned spt_entry_hash (const struct hash_elem *p_, void *aux UNUSED);
bool spt_entry_less (const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED);
struct spt_entry_t * spt_lookup (struct hash *spt, const void *upage);
void spt_free (struct hash *spt);
void spt_entry_free (struct hash_elem *e, void *aux);
bool spt_load_page (struct spt_entry_t *spte);

#endif