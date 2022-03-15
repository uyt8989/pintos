#include <stdio.h>
#include <syscall-nr.h>
#include <string.h>
#include "lib/user/syscall.h"
#include "devices/shutdown.h"
#include "threads/thread.h"
#include "threads/interrupt.h"
#include "threads/synch.h"
#include "userprog/syscall.h"
#include "userprog/process.h"
#include "filesys/off_t.h"
#include "filesys/file.h"
#include "filesys/filesys.h"

/* An open file. */
struct file 
{
  struct inode *inode;        /* File's inode. */
  off_t pos;                  /* Current position. */
  bool deny_write;            /* Has file_deny_write() been called? */
};

struct lock file_lock;

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  lock_init(&file_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/*check address(User Memory Access)*/
void 
check_vaddr(const void *vaddr) 
{
  if (!is_user_vaddr(vaddr)) {
    if (lock_held_by_current_thread(&file_lock))
      lock_release (&file_lock);
    exit(-1);
  }
}

/*modified: make system call function*/
void 
halt()
{
  shutdown_power_off();
}

pid_t
exec(const char *cmd_line)
{
  lock_acquire(&file_lock);
  pid_t result = process_execute(cmd_line);
  lock_release(&file_lock);
  return result;
}

void 
exit (int status) 
{
	struct thread *cur = thread_current ();

  cur->exit_status = status;
  printf("%s: exit(%d)\n", thread_name(), status);
  
  /*modifed5 : free file descriptor list */
  struct list *fd = &cur->file_descriptors;
  while (!list_empty(fd)) {
    struct list_elem *e = list_pop_front (fd);
    struct file_desc *desc = list_entry(e, struct file_desc, elem);
    file_close(desc->file);
    palloc_free_page(desc);
  }

  /*modifed5 : free cwd */
  if(cur->cwd) dir_close (cur->cwd);

  /*modifed5 : free executing file */
  if(cur->executing_file) {
    file_allow_write(cur->executing_file);
    file_close(cur->executing_file);
  }

  thread_exit ();
}

int
wait(pid_t pid)
{
  return process_wait(pid);
}

/*modified2: file system*/
int 
read (int fd, void* buffer, unsigned size) 
{
  int result;
  
  check_vaddr(buffer + size - 1);

  lock_acquire (&file_lock);

  if(fd == 0) { 
    for(unsigned i = 0; i < size; i++)
      if(!put_user(buffer + i, input_getc())) break;

    lock_release (&file_lock);
    return size;
  }

  struct file_desc* fdesc = find_file_desc(thread_current(), fd, FD_FILE);
  if (fdesc == NULL){
    lock_release (&file_lock);
    exit(-1);
  }

  if(fdesc && fdesc->file) result = file_read(fdesc->file, buffer, size);
  else{          
    lock_release(&file_lock);
    exit(-1);
   }

  lock_release (&file_lock);

  return result;
}

int 
write (int fd, const void *buffer, unsigned size) 
{
  int result;
  
  check_vaddr(buffer + size - 1);

  lock_acquire (&file_lock);

  if(fd == 1) { 
    putbuf(buffer, size);

    lock_release(&file_lock);
    return size;
  }

  struct file_desc* fdesc = find_file_desc(thread_current(), fd, FD_FILE);

  if(fdesc && fdesc->file) result = file_write(fdesc->file, buffer, size);
  else{
    lock_release(&file_lock);
    exit(-1);
  }
 
  lock_release (&file_lock);

  return result;
}

/*modified: make additional system call function*/
int 
fibonacci(int n)
{
  int prev, next, temp;

  if(n < 0) return -1;
  else if(n == 0 || n == 1) return n;

  prev = 0; next = 1;
  for(int i = 2; i <= n; i++){
    temp = next;
    next = prev + next;
    prev = temp;
  }

  return next;
}

int 
max_of_four_int(int a, int b, int c, int d)
{
  int temp = a;

  if(temp < b) temp = b;
  if(temp < c) temp = c;
  if(temp < d) temp = d;

  return temp;
}

/*modified2: file system*/
bool 
create (const char *file, unsigned initial_size) 
{
  if(file == NULL){
    exit(-1);
  }
  
  lock_acquire(&file_lock);
  bool result = filesys_create(file, initial_size, false);
  lock_release(&file_lock);

  return result;
}

bool 
remove (const char *file) 
{
  if(file == NULL){
    exit(-1);
  }

  lock_acquire(&file_lock);
  bool result = filesys_remove(file);
  lock_release(&file_lock);

  return result;
}

int 
open (const char *file) 
{
  if(file == NULL)
    exit(-1);

  struct file_desc* fd = palloc_get_page(0);

  lock_acquire(&file_lock);
  
  struct file* fp = filesys_open(file);
  if (fp == NULL) {
      palloc_free_page(fd);
      lock_release(&file_lock);
      return -1; 
  } 

  fd->file = fp;

  //modified5 : directory
  struct inode *inode = file_get_inode(fd->file);
  if(inode != NULL && inode_is_directory(inode))
    fd->dir = dir_open(inode_reopen(inode));
  else fd->dir = NULL;

  //modified5 : add to file descriptor
  struct list* fd_list = &thread_current()->file_descriptors;
  if (list_empty(fd_list)) fd->id = 3;  
  else fd->id = (list_entry(list_back(fd_list), struct file_desc, elem)->id) + 1;
  
  list_push_back(fd_list, &(fd->elem));
  
  if(strcmp(thread_current()->name, file) == 0)
    file_deny_write(fp);

  lock_release (&file_lock);

  return fd->id;
}

int 
filesize (int fd) 
{
  lock_acquire (&file_lock);

  struct file_desc* fdesc = find_file_desc(thread_current(), fd, FD_FILE);
  if(fdesc == NULL) {
    lock_release (&file_lock);
    return -1;
  }

  int result = file_length(fdesc->file);

  lock_release (&file_lock);

  return result;
}

void 
seek (int fd, unsigned position)
{
  lock_acquire (&file_lock);

  struct file_desc* fdesc = find_file_desc(thread_current(), fd, FD_FILE);
  if (fdesc == NULL){
    lock_release (&file_lock);
    return;
  }

  if(fdesc && fdesc->file) file_seek(fdesc->file, position);

  lock_release (&file_lock);
}

unsigned 
tell (int fd) 
{
  lock_acquire (&file_lock);

  unsigned result;
  struct file_desc* fdesc = find_file_desc(thread_current(), fd, FD_FILE);
  if (fdesc == NULL){
    lock_release (&file_lock);
    return false;
  }
  
  if(fdesc && fdesc->file) result = file_tell(fdesc->file);
  else result = -1;

  lock_release (&file_lock);

  return result;
}

void 
close (int fd) 
{
  lock_acquire (&file_lock);

  struct file_desc* fdesc = find_file_desc(thread_current(), fd, FD_FILE | FD_DIRECTORY);
  if(fdesc == NULL) {
    lock_release (&file_lock);
    return;
  }

  if(fdesc && fdesc->file) {
    file_close(fdesc->file);
    if(fdesc->dir) dir_close(fdesc->dir);
    list_remove(&(fdesc->elem));
    palloc_free_page(fdesc);
  }

  lock_release (&file_lock);
}

/* modified5 : file system */
bool chdir(const char *dir){
  lock_acquire(&file_lock);

  bool result = filesys_chdir(dir);

  lock_release(&file_lock);

  return result;
}

bool mkdir(const char *dir){
  lock_acquire(&file_lock);

  bool result = filesys_create(dir, 0, true);

  lock_release(&file_lock);

  return result;
}

bool readdir(int fd, char *name){
  struct file_desc* fdesc;
  bool result;

  lock_acquire (&file_lock);

  fdesc = find_file_desc(thread_current(), fd, FD_DIRECTORY);
  if (fdesc == NULL){
    lock_release (&file_lock);
    return false;
  }

  struct inode *inode = file_get_inode(fdesc->file);
  if(inode == NULL || !inode_is_directory(inode)){
    lock_release(&file_lock);
    return false;
  }
  
  result = dir_readdir (fdesc->dir, name);

  lock_release (&file_lock);

  return result;
}

bool isdir(int fd){
  lock_acquire (&file_lock);

  struct file_desc* fdesc = find_file_desc(thread_current(), fd, FD_FILE | FD_DIRECTORY);
  if (fdesc == NULL){
    lock_release (&file_lock);
    return false;
  }

  bool result = inode_is_directory (file_get_inode(fdesc->file));

  lock_release (&file_lock);

  return result;
}

int inumber(int fd){
  lock_acquire (&file_lock);

  struct file_desc* fdesc = find_file_desc(thread_current(), fd, FD_FILE | FD_DIRECTORY);
  if (fdesc == NULL){
    lock_release (&file_lock);
    return false;
  }

  int result = (int)inode_get_inumber (file_get_inode(fdesc->file));

  lock_release (&file_lock);
  return result;
}

struct file_desc*
find_file_desc(struct thread *t, int fd, int flag)
{
  struct list_elem *e;

  if (!list_empty(&t->file_descriptors)) {
    for(e=list_begin(&t->file_descriptors); e!=list_end(&t->file_descriptors); e=list_next(e)){
      struct file_desc *desc = list_entry(e, struct file_desc, elem);
      if(desc->id == fd) {
        if ((desc->dir != NULL && (flag & FD_DIRECTORY)) ||
          (desc->dir == NULL && (flag & FD_FILE))) return desc;
      }
    }
  }

  return NULL; 
}

/*modified: make system call handler*/
static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  switch ((int)*(uint32_t *)(f->esp)) {
    case SYS_HALT:
		  halt();
		  break;
	  case SYS_EXIT:
      check_vaddr(f->esp + 4);
      exit((int)*(uint32_t *)(f->esp + 4));
		  break;
	  case SYS_EXEC:
		  check_vaddr(f->esp + 4);
      f->eax = exec((const char *)*(uint32_t *)(f->esp + 4));
      break;
	  case SYS_WAIT:
		  check_vaddr(f->esp + 4);
      f->eax = wait((pid_t)*(uint32_t *)(f->esp + 4));
      break;
	  case SYS_CREATE:
      check_vaddr(f->esp + 4); check_vaddr(f->esp + 8);
      f->eax = create((const char *)*(uint32_t *)(f->esp + 4), (unsigned)*(uint32_t *)(f->esp + 8));
		  break;
	  case SYS_REMOVE:
		  check_vaddr(f->esp + 4);
      f->eax = remove((const char*)*(uint32_t *)(f->esp + 4));
      break;
	  case SYS_OPEN:
		  check_vaddr(f->esp + 4);
      f->eax = open((const char*)*(uint32_t *)(f->esp + 4));
      break;
	  case SYS_FILESIZE:
      check_vaddr(f->esp + 4);
      f->eax = filesize((int)*(uint32_t *)(f->esp + 4));
		  break;
	  case SYS_READ:
		  check_vaddr(f->esp + 4); check_vaddr(f->esp + 8); check_vaddr(f->esp + 12);
      f->eax = read((int)*(uint32_t *)(f->esp+4), (void *)*(uint32_t *)(f->esp + 8), 
            (unsigned)*((uint32_t *)(f->esp + 12)));
		  break;
	  case SYS_WRITE:
       check_vaddr(f->esp + 4); check_vaddr(f->esp + 8); check_vaddr(f->esp + 12);
       f->eax = write((int)*(uint32_t *)(f->esp + 4), (void *)*(uint32_t *)(f->esp + 8), 
            (unsigned)*((uint32_t *)(f->esp + 12)));
		  break;
	  case SYS_SEEK:
      check_vaddr(f->esp + 4); check_vaddr(f->esp + 8);
      seek((int)*(uint32_t *)(f->esp + 4), (unsigned)*(uint32_t *)(f->esp + 8));
		  break;
	  case SYS_TELL:
      check_vaddr(f->esp + 4); 
      f->eax = tell((int)*(uint32_t *)(f->esp + 4));
		  break;
	  case SYS_CLOSE:
      check_vaddr(f->esp + 4);
      close((int)*(uint32_t *)(f->esp + 4));
		  break;
    /*additional system call*/
    case SYS_FIBO:
      check_vaddr(f->esp + 4);
      f->eax = fibonacci((int)*(uint32_t *)(f->esp + 4));
      break;
    case SYS_MOF:
      check_vaddr(f->esp + 4); check_vaddr(f->esp + 8); 
      check_vaddr(f->esp + 12); check_vaddr(f->esp + 16);
      f->eax = max_of_four_int((int)*(uint32_t *)(f->esp + 4), (int)*(uint32_t *)(f->esp + 8), 
            (int)*((uint32_t *)(f->esp + 12)), (int)*((uint32_t *)(f->esp + 16)));
      break;
    case SYS_CHDIR:
      check_vaddr(f->esp + 4);
      f->eax = chdir((const char *)*(uint32_t *)(f->esp + 4));
      break; 
    case SYS_MKDIR:
      check_vaddr(f->esp + 4);
      f->eax = mkdir((const char *)*(uint32_t *)(f->esp + 4));
      break;                 
    case SYS_READDIR:
      check_vaddr(f->esp + 4); check_vaddr(f->esp + 8); 
      f->eax = readdir((int)*(uint32_t *)(f->esp + 4), (char *)*(uint32_t *)(f->esp + 8));
      break;             
    case SYS_ISDIR:
      check_vaddr(f->esp + 4);
      f->eax = isdir((int)*(uint32_t *)(f->esp + 4));
      break;                 
    case SYS_INUMBER:
      check_vaddr(f->esp + 4);
      f->eax = inumber((int)*(uint32_t *)(f->esp + 4));
      break;                 
  }
}