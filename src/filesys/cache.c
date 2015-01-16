#include "filesys/cache.h"
#include "devices/timer.h"
#include "threads/thread.h"
#include "threads/malloc.h"

#include <string.h>

static struct list cache_used;
static struct list cache_free;

static struct lock cache_used_lock;
static struct lock cache_free_lock;

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

	lock_init (&cache_used_lock);
	lock_init (&cache_free_lock);

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
	ce->accessed = false;
	memset (ce->buffer, 0, BLOCK_SECTOR_SIZE);
}


void 
cache_write (void *buffer, block_sector_t sector_idx, int sector_ofs, 
	int chunk_size, off_t bytes_written)
{
	struct cache_entry_t *ce = cache_lookup (sector_idx);
	if (!ce) 
		ce = cache_load (sector_idx);
	ce->dirty = true;
	memcpy (ce->buffer + sector_ofs, buffer + bytes_written, chunk_size);
}

void 
cache_read (void *buffer, block_sector_t sector_idx, int sector_ofs, 
	int chunk_size, off_t bytes_read)
{
	struct cache_entry_t *ce = cache_lookup (sector_idx);
	if (!ce) 
		ce = cache_load (sector_idx);
	memcpy (buffer + bytes_read, ce->buffer + sector_ofs, chunk_size);


	block_sector_t sector_idx_next = sector_idx+1;
	struct cache_entry_t *ce_next = cache_lookup (sector_idx_next);
	if(!ce_next)
		thread_create ("cache_read_ahead", PRI_DEFAULT, &cache_read_ahead, (void *)sector_idx_next);
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
	lock_acquire (&cache_free_lock);
	if(list_empty (&cache_free))
		cache_evict ();

	struct list_elem *e = list_pop_front (&cache_free);
	lock_release (&cache_free_lock);
	
	struct cache_entry_t *ce = list_entry (e, struct cache_entry_t, elem);
	//? lock_acquire (&ce->lock);
	ce->sector_idx = sector_idx;
	ce->accessed = true;
	block_read (fs_device, sector_idx, ce->buffer);
	//? lock_release (&ce->lock);
	
	lock_acquire (&cache_used_lock);
	list_push_back (&cache_used, &ce->elem);
	lock_release (&cache_used_lock);
	
	return ce;
}

static void
cache_evict (void)
{
	lock_acquire (&cache_used_lock);
	struct list_elem *e = list_pop_front (&cache_used);
	lock_release (&cache_used_lock);
	
	struct cache_entry_t *ce = list_entry (e, struct cache_entry_t, elem);
	if (ce->dirty)
		block_write (fs_device, ce->sector_idx, ce->buffer);

	cache_init_entry (ce);
	list_push_back (&cache_free, &ce->elem);
}

static void 
cache_write_behind (void *aux UNUSED)
{
	struct list_elem *e;
	while (true)
	{
		for (e = list_begin (&cache_used); e != list_end (&cache_used); e = list_next (e))
		{
			struct cache_entry_t *ce = list_entry (e, struct cache_entry_t, elem);
			if (ce->dirty)
			{
				block_write (fs_device, ce->sector_idx, ce->buffer);
				ce->dirty = false;
			}
		}
		timer_sleep (CACHE_WRITE_BEHIND_PERIOD);
	}
}

static void
cache_read_ahead (void *aux)
{
	block_sector_t sector_idx = (block_sector_t) aux;
	cache_load (sector_idx);
}
