/*
 * pseudOS: swap table
 */
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "vm/page.h"
#include "vm/frame.h"
#include "vm/swap.h"

#include <bitmap.h>
#include <list.h>
#include <string.h>

static struct lock swap_lock;
static struct block *swap_block;
static struct bitmap *swap_bitmap;

static int sectors_per_page;

void 
swap_init(void)
{
	sectors_per_page = PGSIZE / BLOCK_SECTOR_SIZE;

	swap_block = block_get_role(BLOCK_SWAP);
	if(swap_block == NULL)
		PANIC("No Swap space found!");

	swap_bitmap = bitmap_create(block_size(swap_block) / sectors_per_page);
	if(swap_bitmap == NULL)
		PANIC("Cannot create swap bitmap!");

	lock_init(&swap_lock);
}


int32_t
swap_evict (void *upage)
{
	lock_acquire(&swap_lock);
	size_t idx = bitmap_scan_and_flip(swap_bitmap, 0, 1, SWAP_FREE);
	if(idx == BITMAP_ERROR)
		PANIC("Swap space is full!");

	void *kpage = pagedir_get_page (thread_current ()->pagedir, upage);
	
	void *buffer = kpage;
	block_sector_t sector = idx * sectors_per_page;
	for (; buffer < kpage + PGSIZE; buffer+=BLOCK_SECTOR_SIZE, sector++)
		block_write (swap_block, sector, buffer);

	lock_release(&swap_lock);
	return (int32_t)idx;
}

void
swap_free (int32_t idx, void *upage)
{
	lock_acquire(&swap_lock);
	if(bitmap_test (swap_bitmap, idx) != SWAP_USED)
		PANIC("Invalid swap page index!");
	bitmap_flip(swap_bitmap, idx);

	void *kpage = pagedir_get_page (thread_current ()->pagedir, upage);
	
	void *buffer = kpage;
	block_sector_t sector = idx * sectors_per_page;
	for (; buffer < kpage + PGSIZE; buffer+=BLOCK_SECTOR_SIZE, sector++)
		block_read (swap_block, sector, buffer);

	lock_release(&swap_lock);
}
