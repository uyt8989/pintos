#include "filesys/directory.h"
#include <stdio.h>
#include <string.h>
#include <list.h>
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/malloc.h"
#include "threads/thread.h"

/* A directory. */
struct dir
  {
    struct inode *inode;                /* Backing store. */
    off_t pos;                          /* Current position. */
  };

/* A single directory entry. */
struct dir_entry
  {
    block_sector_t inode_sector;        /* Sector number of header. */
    char name[NAME_MAX + 1];            /* Null terminated file name. */
    bool in_use;                        /* In use or free? */
  };

/* modified5: file system */
void parse_path(const char *path, char *directory, char *filename);
bool dir_is_empty (struct dir *dir);
struct dir* dir_open_path (const char *path);

void
parse_path(const char *path, char *directory, char *filename)
{
  char copied[strlen(path) + 1];
  char *dir = directory;
  char *temp, *last = "", *next_ptr;

  strlcpy(copied, path, strlen(path) + 1);  

  // absolute
  if(strlen(path) && path[0] == '/'){
    dir[0] = '/';
    dir++;
  }

  for(temp = strtok_r(copied, "/", &next_ptr); 
      temp != NULL; temp = strtok_r(NULL, "/", &next_ptr)){
    int len = strlen(last);
    if(len > 0 && (dir != NULL)){
      strlcpy(dir, last, len + 1);        
      dir[len] = '/';
      dir += len + 1;
    }
    last = temp;
  }
    
  *dir = '\0';
  strlcpy(filename, last, strlen(last) + 1);  
}

bool 
dir_is_empty (struct dir *dir)
{
  struct dir_entry e;
  off_t ofs;

  for (ofs = sizeof(e); 
      inode_read_at(dir->inode, &e, sizeof(e), ofs) == sizeof(e); ofs += sizeof (e)){
    if (e.in_use == true) return false;
  }

  return true;
}

struct dir*
dir_open_path (const char *path)
{
  char copied[strlen(path) + 1];
  char *temp, *next_ptr;

  strlcpy(copied, path, strlen(path) + 1);

  struct dir* cur_dir;
  if(copied[0] == '/') //absolute path
    cur_dir = dir_open_root();
  else {  //relative path
    struct thread *t = thread_current();
    if (t->cwd == NULL) cur_dir = dir_open_root();
    else cur_dir = dir_reopen(t->cwd);
  }

  for(temp = strtok_r(copied, "/", &next_ptr); 
      temp != NULL; temp = strtok_r(NULL, "/", &next_ptr)){
    struct inode *inode = NULL;
    if(!dir_lookup(cur_dir, temp, &inode)) {
      dir_close(cur_dir);
      return NULL;
    }
    struct dir *next = dir_open(inode);
    if(next == NULL) {
      dir_close(cur_dir);
      return NULL;
    }
    dir_close(cur_dir);
    cur_dir = next;
  }

  if (inode_is_removed (dir_get_inode(cur_dir))) {
    dir_close(cur_dir);
    return NULL;
  }

  return cur_dir;
}

/* Creates a directory with space for ENTRY_CNT entries in the
   given SECTOR.  Returns true if successful, false on failure. */
bool
dir_create (block_sector_t sector, size_t entry_cnt)
{
  bool success;
  struct dir_entry e;

  /* modified5 : directory */
  success = inode_create (sector, entry_cnt * sizeof(struct dir_entry), true);
  if(!success) return success;

  struct dir *dir = dir_open(inode_open(sector));
  e.inode_sector = sector;

  if (inode_write_at(dir->inode, &e, sizeof e, 0) != sizeof(e)) success = false;
  
  dir_close (dir);

  return success;
}

/* Opens and returns the directory for the given INODE, of which
   it takes ownership.  Returns a null pointer on failure. */
struct dir *
dir_open (struct inode *inode)
{
  struct dir *dir = calloc (1, sizeof *dir);
  if (inode != NULL && dir != NULL)
    {
      dir->inode = inode;
      /*modified5 : directory */
      dir->pos = sizeof(struct dir_entry); 
      return dir;
    }
  else
    {
      inode_close (inode);
      free (dir);
      return NULL;
    }
}

/* Opens the root directory and returns a directory for it.
   Return true if successful, false on failure. */
struct dir *
dir_open_root (void)
{
  return dir_open (inode_open (ROOT_DIR_SECTOR));
}

/* Opens and returns a new directory for the same inode as DIR.
   Returns a null pointer on failure. */
struct dir *
dir_reopen (struct dir *dir)
{
  return dir_open (inode_reopen (dir->inode));
}

/* Destroys DIR and frees associated resources. */
void
dir_close (struct dir *dir)
{
  if (dir != NULL)
    {
      inode_close (dir->inode);
      free (dir);
    }
}

/* Returns the inode encapsulated by DIR. */
struct inode *
dir_get_inode (struct dir *dir)
{
  return dir->inode;
}

/* Searches DIR for a file with the given NAME.
   If successful, returns true, sets *EP to the directory entry
   if EP is non-null, and sets *OFSP to the byte offset of the
   directory entry if OFSP is non-null.
   otherwise, returns false and ignores EP and OFSP. */
static bool
lookup (const struct dir *dir, const char *name,
        struct dir_entry *ep, off_t *ofsp)
{
  struct dir_entry e;
  size_t ofs;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* modified5 : file system */
  for (ofs = sizeof(e); inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e)
    if (e.in_use && !strcmp (name, e.name))
      {
        if (ep != NULL)
          *ep = e;
        if (ofsp != NULL)
          *ofsp = ofs;
        return true;
      }
  return false;
}

/* Searches DIR for a file with the given NAME
   and returns true if one exists, false otherwise.
   On success, sets *INODE to an inode for the file, otherwise to
   a null pointer.  The caller must close *INODE. */
bool
dir_lookup (const struct dir *dir, const char *name,
            struct inode **inode)
{
  struct dir_entry e;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* modified5 : subdirectory */
  if (strcmp (name, "..") == 0) {                  //parent
    inode_read_at (dir->inode, &e, sizeof(e), 0);
    *inode = inode_open (e.inode_sector);
  }

  else if (strcmp (name, ".") == 0)                //current
    *inode = inode_reopen (dir->inode);

  else if (lookup(dir, name, &e, NULL)) 
    *inode = inode_open (e.inode_sector);

  else
    *inode = NULL;

  return *inode != NULL;
}

/* Adds a file named NAME to DIR, which must not already contain a
   file by that name.  The file's inode is in sector
   INODE_SECTOR.

   Returns true if successful, false on failure.
   Fails if NAME is invalid (i.e. too long) or a disk or memory
   error occurs. */
bool
dir_add (struct dir *dir, const char *name, block_sector_t inode_sector, bool is_dir)
{
  struct dir_entry e;
  off_t ofs;
  bool success = false;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Check NAME for validity. */
  if (*name == '\0' || strlen (name) > NAME_MAX)
    return false;

  /* Check that NAME is not in use. */
  if (lookup (dir, name, NULL, NULL))
    goto done;

  /* modified5 : update child directory */
  if (is_dir){
    struct dir *child_dir = dir_open(inode_open(inode_sector));

    if(!child_dir) goto done;

    e.inode_sector = inode_get_inumber(dir_get_inode(dir));

    if (inode_write_at(child_dir->inode, &e, sizeof(e), 0) != sizeof(e)) {
      dir_close (child_dir);
      goto done;
    }

    dir_close (child_dir);
  }

  /* Set OFS to offset of free slot.
     If there are no free slots, then it will be set to the
     current end-of-file.

     inode_read_at() will only return a short read at end of file.
     Otherwise, we'd need to verify that we didn't get a short
     read due to something intermittent such as low memory. */
  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e)
    if (!e.in_use)
      break;

  /* Write slot. */
  e.in_use = true;
  strlcpy (e.name, name, sizeof e.name);
  e.inode_sector = inode_sector;
  success = inode_write_at (dir->inode, &e, sizeof e, ofs) == sizeof e;

 done:
  return success;
}

/* Removes any entry for NAME in DIR.
   Returns true if successful, false on failure,
   which occurs only if there is no file with the given NAME. */
bool
dir_remove (struct dir *dir, const char *name)
{
  struct dir_entry e;
  struct inode *inode = NULL;
  bool success = false;
  off_t ofs;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Find directory entry. */
  if (!lookup (dir, name, &e, &ofs))
    goto done;

  /* Open inode. */
  inode = inode_open (e.inode_sector);
  if (inode == NULL)
    goto done;

  /* modified5 : non empty directory */
  if (inode_is_directory(inode)) {
    struct dir *target = dir_open(inode);
    bool is_empty = dir_is_empty(target);
    dir_close (target);
    if (!is_empty) goto done;
  }

  /* Erase directory entry. */
  e.in_use = false;
  if (inode_write_at (dir->inode, &e, sizeof e, ofs) != sizeof e)
    goto done;

  /* Remove inode. */
  inode_remove (inode);
  success = true;

 done:
  inode_close (inode);
  return success;
}

/* Reads the next directory entry in DIR and stores the name in
   NAME.  Returns true if successful, false if the directory
   contains no more entries. */
bool
dir_readdir (struct dir *dir, char name[NAME_MAX + 1])
{
  struct dir_entry e;

  while (inode_read_at (dir->inode, &e, sizeof e, dir->pos) == sizeof e)
    {
      dir->pos += sizeof e;
      if (e.in_use)
        {
          strlcpy (name, e.name, NAME_MAX + 1);
          return true;
        }
    }
  return false;
}
