#include "filesys/cache.h"
#include "devices/timer.h"
#include "threads/thread.h"
#include "threads/malloc.h"

#include <string.h>

static struct list cache_used;					// a list of acutal used cache entries
static struct list cache_free;					// a list of acutal unused cache entries
static struct list cache_read_ahead_requests;	//

static struct lock cache_global_lock;
static struct lock cache_read_ahead_lock;
static struct condition cache_not_empty_cond;

static void cache_write_behind_deamon (void *aux UNUSED);
static void cache_read_ahead_deamon (void *aux) 		UNUSED;
static void cache_read_ahead (block_sector_t sector) 	UNUSED;

static void cache_evict (void);
static struct cache_entry_t * cache_lookup (block_sector_t sector_idx);
static struct cache_entry_t * cache_load (block_sector_t sector_idx);
static void cache_init_entry (struct cache_entry_t *ce);

struct cache_read_ahead_entry
{
	block_sector_t sector;
	struct list_elem elem;
};

void
cache_init (void)
{
	list_init (&cache_used);
	list_init (&cache_free);
	list_init (&cache_read_ahead_requests);

	lock_init (&cache_global_lock);
	lock_init (&cache_read_ahead_lock);
	cond_init (&cache_not_empty_cond);

	unsigned i;
	for(i = 0; i < CACHE_SIZE; i++)
	{
		struct cache_entry_t *ce = malloc (sizeof(struct cache_entry_t));
		ce->buffer = malloc (BLOCK_SECTOR_SIZE);
		cache_init_entry (ce);
		list_push_back (&cache_free, &ce->elem);
	}
	thread_create ("c_wb_deamon", PRI_DEFAULT, &cache_write_behind_deamon, NULL);
	// thread_create ("c_ra_deamon", PRI_DEFAULT, &cache_read_ahead_deamon, NULL);
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
	struct cache_entry_t *ce = cache_lookup (sector_idx);
	if (!ce) 
		ce = cache_load (sector_idx);

	lock_acquire (&cache_global_lock);
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
	struct cache_entry_t *ce = cache_lookup (sector_idx);
	if (!ce) 
		ce = cache_load (sector_idx);
	
	lock_acquire (&cache_global_lock);
	memcpy (buffer + bytes_read, ce->buffer + sector_ofs, chunk_size);
	lock_release (&cache_global_lock);
	// cache_read_ahead (sector_idx+1);
}

void
cache_flush (void)
{
	lock_acquire (&cache_global_lock);
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
	lock_release (&cache_global_lock);
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
	lock_acquire (&cache_global_lock);
	if(list_empty (&cache_free))
		cache_evict ();

	struct list_elem *e = list_pop_front (&cache_free);
	struct cache_entry_t *ce = list_entry (e, struct cache_entry_t, elem);
	ce->sector_idx = sector_idx;
	block_read (fs_device, sector_idx, ce->buffer);
	list_push_back (&cache_used, &ce->elem);
	lock_release (&cache_global_lock);
	return ce;
}

static void
cache_evict (void)
{
	struct list_elem *e = list_pop_front (&cache_used);
	
	struct cache_entry_t *ce = list_entry (e, struct cache_entry_t, elem);
	if (ce->dirty)
		block_write (fs_device, ce->sector_idx, ce->buffer);

	cache_init_entry (ce);
	list_push_back (&cache_free, &ce->elem);
}

static void 
cache_write_behind_deamon (void *aux UNUSED)
{
	while (true)
	{
		// lock_acquire (&cache_global_lock);
		cache_flush ();
		// lock_release (&cache_global_lock);
		timer_sleep (CACHE_WRITE_BEHIND_PERIOD);
	}
}

static void
cache_read_ahead (block_sector_t sector)
{
	lock_acquire (&cache_read_ahead_lock);
	
	struct cache_entry_t *ce_next = cache_lookup (sector);
	if(!ce_next)
	{
		struct cache_read_ahead_entry *e = malloc (sizeof(struct cache_read_ahead_entry));
		e->sector = sector;
		list_push_back (&cache_read_ahead_requests, &e->elem);
		cond_signal (&cache_not_empty_cond, &cache_read_ahead_lock);
	}
	lock_release (&cache_read_ahead_lock);
}


static void
cache_read_ahead_deamon (void *aux UNUSED)
{
	while (true)
	{
		lock_acquire (&cache_read_ahead_lock);
		while (list_empty (&cache_read_ahead_requests))
			cond_wait (&cache_not_empty_cond, &cache_read_ahead_lock);

		struct list_elem *le = list_pop_front (&cache_read_ahead_requests);
		struct cache_read_ahead_entry *ce = list_entry (le, struct cache_read_ahead_entry, elem);
		cache_load (ce->sector);
		free (ce);
		lock_release (&cache_read_ahead_lock);
	}
}