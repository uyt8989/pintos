#ifndef SWAP_H
#define SWAP_H

#define SECTORS_PER_PAGE 8
#define SWAP_FREE 0
#define SWAP_USED 1

struct lock swap_lock;
struct bitmap *swap_map;
struct block *swap_block;

void swap_init(void);
void swap_in(size_t used_index, void* kaddr);
size_t swap_out(void* kaddr);

#endif