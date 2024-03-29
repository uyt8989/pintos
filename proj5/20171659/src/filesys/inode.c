#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "filesys/buffer_cache.h"
#include "threads/malloc.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

#define DIRECT_BLOCKS 123
#define INDIRECT_BLOCKS 1
#define DOUBLE_INDIRECT_BLOCKS 1
#define INDIRECT_BLOCKS_PER_SECTOR 128

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
/*modified5 : file system */
struct inode_disk
  {
    /** Data sectors */
    block_sector_t direct_blocks[DIRECT_BLOCKS];
    block_sector_t indirect_block;
    block_sector_t doubly_indirect_block;

    bool is_dir;
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    //uint32_t unused[125];               /* Not used. */
  };

struct inode_indirect_block {
  block_sector_t blocks[INDIRECT_BLOCKS_PER_SECTOR];
};

/* modified5 : file system */
static block_sector_t index_to_sector (const struct inode_disk *idisk, off_t index);
static bool inode_allocate (struct inode_disk *disk_inode, off_t length);
static bool inode_allocate_indirect (block_sector_t* p_entry, size_t num_sectors, int level);
static void inode_free (struct inode *inode);
static void inode_free_indirect (block_sector_t entry, size_t num_sectors, int level);

static inline size_t
min (size_t a, size_t b)
{
  return a < b ? a : b;
}

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct inode_disk data;             /* Inode content. */
    /* modified5 : file system */
    //struct lock extend_lock; 
  };

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos)
{
  ASSERT (inode != NULL);
  /* modified5 : file system */
  if (0 <= pos && pos < inode->data.length) {
    off_t block_index = pos / BLOCK_SECTOR_SIZE;
    return index_to_sector (&inode->data, block_index);
  }
  else
    return -1;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void)
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length, bool is_dir)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
      /* modified5 : directory */
      disk_inode->is_dir = is_dir;
      if(inode_allocate (disk_inode, disk_inode->length)){
        bc_write (sector, disk_inode); //buffer cache
        success = true;
      }
      free (disk_inode);
    }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e))
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector)
        {
          inode_reopen (inode);
          return inode;
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;

  /* modified5 : buffer cache */
  bc_read (inode->sector, &inode->data);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode)
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);

      /* Deallocate blocks if removed. */
      if (inode->removed)
        {
          free_map_release (inode->sector, 1);
          /* modified5 : file system */
          inode_free (inode);
        }

      free (inode);
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode)
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset)
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;

  while (size > 0)
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* modified5 : buffer cache */
          bc_read (sector_idx, buffer + bytes_read);
        }
      else
        {
          /* Read sector into bounce buffer, then partially copy
             into caller's buffer. */
          if (bounce == NULL)
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }

          /* modified5 : buffer cache */
          bc_read (sector_idx, bounce);
          memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  free (bounce);

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode).
   */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset)
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;

  /* modified5 : file growth */
  if(byte_to_sector(inode, offset + size - 1) == -1u ) {
    bool success;
    success = inode_allocate (& inode->data, offset + size);
    if (!success) return 0; 
    inode->data.length = offset + size;
    bc_write (inode->sector, & inode->data);
  }

  while (size > 0)
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* modified5 : buffer cache */
          bc_write (sector_idx, buffer + bytes_written);
        }
      else
        {
          /* We need a bounce buffer. */
          if (bounce == NULL)
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }

          /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all zeros. */
          if (sector_ofs > 0 || chunk_size < sector_left)
            bc_read (sector_idx, bounce); //buffer cache
          else
            memset (bounce, 0, BLOCK_SECTOR_SIZE);
          memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
          /* modified5 : write to buffer cache */
          bc_write (sector_idx, bounce);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  free (bounce);

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode)
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode)
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}

/* ================== modified5 : directory ============================ */

bool
inode_is_directory (const struct inode *inode)
{
  return inode->data.is_dir;
}

bool
inode_is_removed (const struct inode *inode)
{
  return inode->removed;
}

/* ================== modified5 : index structure ============================ */

static block_sector_t
index_to_sector (const struct inode_disk *idisk, off_t block_index)
{
  off_t base = 0, bound = 0;
  block_sector_t result;

  // direct 
  bound += DIRECT_BLOCKS;
  if (block_index < bound) return idisk->direct_blocks[block_index];

  // single indirect
  base = bound;
  bound += INDIRECT_BLOCKS_PER_SECTOR;
  if (block_index < bound) {
    struct inode_indirect_block 
    *indirect_block = calloc(1, sizeof(struct inode_indirect_block));

    bc_read (idisk->indirect_block, indirect_block);
    result = indirect_block->blocks[block_index - base];
   
    free(indirect_block);
    return result;
  }

  // doubly indirect
  base = bound;
  bound += INDIRECT_BLOCKS_PER_SECTOR * INDIRECT_BLOCKS_PER_SECTOR;
  if (block_index < bound) {
    off_t single_index =  (block_index - base) / INDIRECT_BLOCKS_PER_SECTOR;
    off_t doubly_index = (block_index - base) % INDIRECT_BLOCKS_PER_SECTOR;

    struct inode_indirect_block 
    *indirect_block = calloc(1, sizeof(struct inode_indirect_block));

    bc_read (idisk->doubly_indirect_block, indirect_block);
    bc_read (indirect_block->blocks[single_index], indirect_block);
    result = indirect_block->blocks[doubly_index];

    free(indirect_block);
    return result;
  }

  return -1;
}

bool
inode_allocate (struct inode_disk *idisk, off_t length)
{
  static char zeros[BLOCK_SECTOR_SIZE];
  size_t bound;

  if (length < 0) return false;
  
  size_t remain_index = bytes_to_sectors(length);
  // maximum size
  if(remain_index > DIRECT_BLOCKS + INDIRECT_BLOCKS * INDIRECT_BLOCKS_PER_SECTOR + 
  DOUBLE_INDIRECT_BLOCKS * INDIRECT_BLOCKS_PER_SECTOR * INDIRECT_BLOCKS_PER_SECTOR) return false;

  // direct
  if(remain_index < DIRECT_BLOCKS) bound = remain_index;
  else bound = DIRECT_BLOCKS;

  for (size_t i = 0; i < bound; i++) {
    if (idisk->direct_blocks[i] == 0) {
      if(!free_map_allocate(1, &idisk->direct_blocks[i])) return false;
      bc_write (idisk->direct_blocks[i], zeros);
    }
  }

  remain_index -= bound;
  if(remain_index == 0) goto done; 


  // indirect
  if(remain_index < INDIRECT_BLOCKS_PER_SECTOR) bound = remain_index;
  else bound = INDIRECT_BLOCKS_PER_SECTOR;
  if(!inode_allocate_indirect(&idisk->indirect_block, bound, 1)) return false;
  
  remain_index -= bound;
  if(remain_index == 0) goto done; 


  // doubly indirect 
  bound = remain_index;
  if(!inode_allocate_indirect(&idisk->doubly_indirect_block, bound, 2)) return false;
  remain_index -= bound;
 
done:
  return (remain_index == 0);
}

static bool
inode_allocate_indirect (block_sector_t* sector, size_t remain_index, int level)
{
  static char zeros[BLOCK_SECTOR_SIZE];
  size_t bound, subsize, unit = 1;
  if(level == 2) unit = INDIRECT_BLOCKS_PER_SECTOR;

  // direct
  if (level == 0) {
    if (*sector == 0) {
      if(!free_map_allocate(1, sector)) return false;
      bc_write (*sector, zeros);
    }
    return true;
  }

  // indirect
  struct inode_indirect_block indirect_block;
  if(*sector == 0) {
     if(!free_map_allocate(1, sector)) return false;
    bc_write (*sector, zeros);
  }
  bc_read(*sector, &indirect_block);

  bound = remain_index / unit + 1;
  for (size_t i = 0; i < bound; i++) {
    if(remain_index > unit) subsize = unit;
    else subsize = remain_index;

    if(!inode_allocate_indirect(&indirect_block.blocks[i], subsize, level - 1)) return false;
    remain_index -= subsize;
  }
  bc_write (*sector, &indirect_block);
  return true;
}

static void 
inode_free (struct inode *inode)
{
  if(inode->data.length < 0) return;

  size_t remain_index = bytes_to_sectors(inode->data.length);
  size_t bound;

  // direct 
  if(remain_index < DIRECT_BLOCKS) bound = remain_index;
  else bound = DIRECT_BLOCKS;
  for (size_t i = 0; i < bound; i++) 
    free_map_release (inode->data.direct_blocks[i], 1);
  remain_index -= bound;

  // indirect 
  if(remain_index < INDIRECT_BLOCKS_PER_SECTOR) bound = remain_index;
  else bound = INDIRECT_BLOCKS_PER_SECTOR;
  if(bound > 0) {
    inode_free_indirect (inode->data.indirect_block, bound, 1);
    remain_index -= bound;
  }

  // doubly indirect 
  bound = remain_index;
  if(bound > 0) {
    inode_free_indirect (inode->data.doubly_indirect_block, bound, 2);
    remain_index -= bound;
  }

  return ;
}

static void
inode_free_indirect (block_sector_t sector, size_t remain_index, int level)
{
   size_t bound, subsize, unit = 1;

  if (level == 0) {
    free_map_release (sector, 1);
    return;
  }

  struct inode_indirect_block indirect_block;
  bc_read(sector, &indirect_block);

  if(level == 2) unit = INDIRECT_BLOCKS_PER_SECTOR;
  bound = remain_index / unit + 1;
  for (size_t i = 0; i < bound; i++) {
    if(remain_index > unit) subsize = unit;
    else subsize = remain_index;

    inode_free_indirect (indirect_block.blocks[i], subsize, level - 1);
    remain_index -= subsize;
  }

  free_map_release (sector, 1);
}