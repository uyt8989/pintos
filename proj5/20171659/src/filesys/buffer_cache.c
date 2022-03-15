#include <debug.h>
#include <string.h>
#include "filesys/buffer_cache.h"
#include "filesys/filesys.h"

void
bc_init (void)
{
  lock_init (&bc_lock);

  for (int i = 0; i < BUFFER_CACHE_SIZE; i++){
    cache[i].valid = false;
    cache[i].dirty = false;
    cache[i].access = false;
  }
}

void
bc_read (block_sector_t sector, void *target)
{
  lock_acquire (&bc_lock);

  struct bc_entry_t *slot = bc_lookup (sector);
  
  //not in cache
  if (slot == NULL) {
    slot = bc_select_victim ();
    block_read (fs_device, sector, slot->buffer);

    slot->disk_sector = sector;
    slot->valid = true;
    slot->dirty = false;
  }

  slot->access = true;
  memcpy(target, slot->buffer, BLOCK_SECTOR_SIZE);

  lock_release (&bc_lock);
}

void
bc_write (block_sector_t sector, const void *source)
{
  lock_acquire (&bc_lock);

  struct bc_entry_t *slot = bc_lookup (sector);

  //not in cache
  if (slot == NULL) {
    slot = bc_select_victim ();
    block_read (fs_device, sector, slot->buffer);

    slot->disk_sector = sector;
    slot->valid = true;
    slot->dirty = false;
  }

  slot->access = true;
  slot->dirty = true;
  memcpy(slot->buffer, source, BLOCK_SECTOR_SIZE);

  lock_release (&bc_lock);
}

void
bc_flush (struct bc_entry_t *entry)
{
  block_write (fs_device, entry->disk_sector, entry->buffer);
  entry->dirty = false;
}

void
bc_flush_all (void)
{
  lock_acquire (&bc_lock);

  for (size_t i = 0; i < BUFFER_CACHE_SIZE; i++)
    if (cache[i].valid == true && cache[i].dirty == true)
      bc_flush(&(cache[i]));
  
  lock_release (&bc_lock);
}

struct bc_entry_t*
bc_lookup (block_sector_t sector)
{
  for (size_t i = 0; i < BUFFER_CACHE_SIZE; ++ i)
    if (cache[i].valid == true && cache[i].disk_sector == sector) 
      return &(cache[i]);
  return NULL;
}

struct bc_entry_t*
bc_select_victim (void)
{
  uint8_t clock = 0;

  while (true) {
    if (cache[clock].valid == false) 
      return &(cache[clock]);

    if (cache[clock].access == false) break;
    
    cache[clock].access = false;

    clock++;
    clock %= BUFFER_CACHE_SIZE;
  }

  if(cache[clock].dirty == true)
    bc_flush(&cache[clock]);
  cache[clock].valid = false;

  return &(cache[clock]);
}