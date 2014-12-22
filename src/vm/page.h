#ifndef PAGE_H
#define PAGE_H
/*
 * pseudOS
 */
#include <debug.h>
#include <hash.h>
#include "filesys/off_t.h"

enum spt_entry_type
{
	SPT_ENTRY_TYPE_FILE,		/* Common file. */
	SPT_ENTRY_TYPE_MMAP,		/* Memory mapped file. */
	SPT_ENTRY_TYPE_SWAP 		/* SWAP. */
};

struct spt_entry_t 
{
	struct hash_elem hashelem;
	int64_t lru_ticks;
	struct file *file;
	off_t ofs;
	uint8_t *upage;
	uint32_t read_bytes;
	uint32_t zero_bytes;
	bool writable;
	enum spt_entry_type type;
};

void spt_init(struct hash *spt);
bool spt_insert (struct hash *spt, struct file *file, off_t ofs, 
	uint8_t *upage, uint32_t read_bytes, uint32_t zero_bytes,
	bool writable, enum spt_entry_type type);
void spt_remove (struct hash *spt, void *upage);
unsigned spt_entry_hash (const struct hash_elem *p_, void *aux UNUSED);
bool spt_entry_less (const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED);
struct spt_entry_t * spt_lookup (struct hash *spt, const void *upage);
void spt_free (struct hash *spt);

#endif