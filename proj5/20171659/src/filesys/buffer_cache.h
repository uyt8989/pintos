#ifndef FILESYS_BUFFER_CACHE_H
#define FILESYS_BUFFER_CACHE_H

#include "devices/block.h"
#include "threads/synch.h"

#define BUFFER_CACHE_SIZE 64

struct bc_entry_t {
  block_sector_t disk_sector;
  uint8_t buffer[BLOCK_SECTOR_SIZE];

  bool valid;     // valid bit
  bool dirty;     // dirty bit
  bool access;    // access bit
};

struct bc_entry_t cache[BUFFER_CACHE_SIZE];
struct lock bc_lock;

void bc_init (void);
void bc_read (block_sector_t sector, void *target);
void bc_write (block_sector_t sector, const void *source);
void bc_flush (struct bc_entry_t *entry);
void bc_flush_all (void);
struct bc_entry_t* bc_lookup (block_sector_t sector);
struct bc_entry_t* bc_select_victim (void);

#endif
