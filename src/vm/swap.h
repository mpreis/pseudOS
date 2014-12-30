#ifndef SWAP_H
#define SWAP_H
/*
 * pseudOS
 */

#include "devices/block.h"

#define SWAP_FREE 0
#define SWAP_USED 1


void swap_init(void);
size_t swap_evict (void *upage);
void swap_free (block_sector_t sector, void *upage);

#endif