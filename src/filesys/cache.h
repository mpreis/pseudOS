#ifndef CACHE_H
#define CACHE_H

#include "devices/block.h"
#include "filesys/off_t.h"
#include "filesys/filesys.h"

#include <list.h>

#define CACHE_SIZE 64
#define CACHE_SECTOR_IDX_DEFAULT 0
#define CACHE_WRITE_BEHIND_PERIOD 100

struct cache_entry_t
{
	block_sector_t sector_idx;
	void *buffer;
	bool dirty;
	bool accessed;
	struct list_elem elem;
};

void cache_init (void);
void cache_flush (void);

void cache_write_sector (void *buffer, block_sector_t sector_idx);
void cache_write (void *buffer, block_sector_t sector_idx, int sector_ofs, 
	int chunk_size, off_t bytes_written);

void cache_read_sector (void *buffer, block_sector_t sector_idx);
void cache_read (void *buffer, block_sector_t sector_idx, int sector_ofs, 
	int chunk_size, off_t bytes_read);
#endif