#include "filesys/cache.h"
#include "devices/timer.h"
#include "threads/thread.h"
#include "threads/malloc.h"

#include <string.h>

static struct list cache_used;
static struct list cache_free;

static struct lock cache_global_lock;

static void cache_write_behind (void *aux UNUSED);
static void cache_read_ahead (void *aux);

static void cache_evict (void);
static struct cache_entry_t * cache_lookup (block_sector_t sector_idx);
static struct cache_entry_t * cache_load (block_sector_t sector_idx);
static void cache_init_entry (struct cache_entry_t *ce);

void
cache_init (void)
{
	list_init (&cache_used);
	list_init (&cache_free);

	lock_init (&cache_global_lock);

	unsigned i;
	for(i = 0; i < CACHE_SIZE; i++)
	{
		struct cache_entry_t *ce = malloc (sizeof(struct cache_entry_t));
		ce->buffer = malloc (BLOCK_SECTOR_SIZE);
		cache_init_entry (ce);
		list_push_back (&cache_free, &ce->elem);
	}
	thread_create ("cache_write_behind", PRI_DEFAULT, &cache_write_behind, NULL);
}

static void
cache_init_entry (struct cache_entry_t *ce)
{
	ce->sector_idx = CACHE_SECTOR_IDX_DEFAULT;
	ce->dirty = false;
	memset (ce->buffer, 0, BLOCK_SECTOR_SIZE);
}

void
cache_write_sector (void *buffer, block_sector_t sector_idx)
{
	cache_write (buffer, sector_idx, 0, BLOCK_SECTOR_SIZE, 0);	
}

void 
cache_write (void *buffer, block_sector_t sector_idx, int sector_ofs, 
	int chunk_size, off_t bytes_written)
{
	lock_acquire (&cache_global_lock);
	struct cache_entry_t *ce = cache_lookup (sector_idx);
	if (!ce) 
		ce = cache_load (sector_idx);
	ce->dirty = true;
	memcpy (ce->buffer + sector_ofs, buffer + bytes_written, chunk_size);
	lock_release (&cache_global_lock);
}

void
cache_read_sector (void *buffer, block_sector_t sector_idx)
{
	cache_read (buffer, sector_idx, 0, BLOCK_SECTOR_SIZE, 0);	
}

void 
cache_read (void *buffer, block_sector_t sector_idx, int sector_ofs, 
	int chunk_size, off_t bytes_read)
{
	lock_acquire (&cache_global_lock);
	struct cache_entry_t *ce = cache_lookup (sector_idx);
	if (!ce) 
		ce = cache_load (sector_idx);
	memcpy (buffer + bytes_read, ce->buffer + sector_ofs, chunk_size);

	block_sector_t sector_idx_next = sector_idx+1;
	lock_release (&cache_global_lock);
	thread_create ("cache_read_ahead", PRI_DEFAULT, &cache_read_ahead, (void *)sector_idx_next);
}

void
cache_flush (void)
{
	struct list_elem *e;
	for (e = list_begin (&cache_used); e != list_end (&cache_used); e = list_next (e))
	{
		struct cache_entry_t *ce = list_entry (e, struct cache_entry_t, elem);
		if (ce->dirty)
		{
			block_write (fs_device, ce->sector_idx, ce->buffer);
			ce->dirty = false;
		}
	}
}

static struct cache_entry_t *
cache_lookup (block_sector_t sector_idx)
{
	struct list_elem *e;
	for (e = list_begin (&cache_used); e != list_end (&cache_used); e = list_next (e))
	{
		struct cache_entry_t *ce = list_entry (e, struct cache_entry_t, elem);
		if (ce->sector_idx == sector_idx)
			return ce;
	}
	return NULL;
}

static struct cache_entry_t *
cache_load (block_sector_t sector_idx)
{
	if(list_empty (&cache_free))
		cache_evict ();

	struct list_elem *e = list_pop_front (&cache_free);
	struct cache_entry_t *ce = list_entry (e, struct cache_entry_t, elem);
	ce->sector_idx = sector_idx;
	block_read (fs_device, sector_idx, ce->buffer);
	list_push_back (&cache_used, &ce->elem);
	return ce;
}

static void
cache_evict (void)
{
	struct list_elem *e = list_pop_front (&cache_used);
	
	struct cache_entry_t *ce = list_entry (e, struct cache_entry_t, elem);
	if (ce->dirty)
	{
		block_write (fs_device, ce->sector_idx, ce->buffer);
		cache_flush ();
	}

	cache_init_entry (ce);
	list_push_back (&cache_free, &ce->elem);
}

static void 
cache_write_behind (void *aux UNUSED)
{
	while (true)
	{
		lock_acquire (&cache_global_lock);
		cache_flush ();
		lock_release (&cache_global_lock);
		timer_sleep (CACHE_WRITE_BEHIND_PERIOD);
	}
}

static void
cache_read_ahead (void *aux)
{
	lock_acquire (&cache_global_lock);
	
	block_sector_t sector_idx = (block_sector_t) aux;
	struct cache_entry_t *ce_next = cache_lookup (sector_idx);
	if(!ce_next)
		cache_load (sector_idx);

	lock_release (&cache_global_lock);
}
