#ifndef SWAP_H
#define SWAP_H
/*
 * pseudOS
 */

#include "devices/block.h"

#define SWAP_FREE 0
#define SWAP_USED 1


void swap_init(void);
int32_t swap_evict (void *kpage);
void swap_free (int32_t sector, void *kpage);

#endif