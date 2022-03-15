#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "devices/input.h"
#include "userprog/process.h"
#include "devices/shutdown.h"
#include "filesys/filesys.h"
#include "filesys/off_t.h"
#include "threads/synch.h"
#include "filesys/file.h"
#include "string.h"
#include "vm/page.h"

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

void 
check_valid_buffer(void *buffer, unsigned size, void *esp UNUSED, bool to_write)
{
   struct vm_entry *vme;
   unsigned i;
   char *check_buffer = (char *)buffer;

   /* check buffer */
   for(i=0; i<size; i++){
      check_vaddr((void *)check_buffer);
      vme = find_vme((void *)check_buffer);
      if(vme != NULL){
         if(to_write == true && vme->writable == false)
               exit(-1);
      }
      check_buffer++;
   }
}

/*modified: make system call function*/
void
check_vaddr(void *addr)
{
	/* if address is user_address */
	if(!is_user_vaddr(addr))
	  exit(-1);
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
  return process_execute(cmd_line);
}

void 
exit (int status) 
{
  thread_current()->exit_status = status;
  printf("%s: exit(%d)\n", thread_name(), status);
  for(int i = 3; i < thread_current()->fd_num; i++){
    if(thread_current()->fd[i] != NULL){
      close(i);
    }
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
  int i = 0;
  
  lock_acquire(&file_lock);
  
  if (fd == 0) {
    for (unsigned i = 0; i < size; i++) {
      if (((char *)buffer)[i] == '\0') {
        break;
      }   
    }   
  } 
  else if (fd > 2) {
    if (thread_current()->fd[fd] == NULL) {
      lock_release(&file_lock);
      exit(-1);
    }

    lock_release(&file_lock);
    return file_read(thread_current()->fd[fd], buffer, size);
  }
  
  lock_release(&file_lock);
  return i;
}

int 
write (int fd, const void *buffer, unsigned size) 
{
  
  lock_acquire(&file_lock);
  if (fd == 1) {
    putbuf(buffer, size);
    lock_release(&file_lock);
    return size;
  } 
  else if (fd > 2) {
    if (thread_current()->fd[fd] == NULL) {
      lock_release(&file_lock);
      exit(-1);
    }

    lock_release(&file_lock);
    return file_write(thread_current()->fd[fd], buffer, size);
  }
  lock_release(&file_lock);
  return -1; 
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
  bool result = filesys_create(file, initial_size);
  lock_release(&file_lock);

  return result;
}

bool 
remove (const char *file) 
{
  if(file == NULL){
    exit(-1);
  }
  return filesys_remove(file);
}

int 
open (const char *file) 
{
  if(file == NULL){
    exit(-1);
  }
  lock_acquire(&file_lock);
  struct file* fp = filesys_open(file);
  if (fp == NULL) {
      lock_release(&file_lock);
      return -1; 
  } 
  else {
     for (int i = thread_current()->fd_num; i < 128; i++) {
        if (thread_current()->fd[i] == NULL) {
          if(strcmp(thread_current()->name, file) == 0){
            file_deny_write(fp);
          }
    
          thread_current()->fd[i] = fp; 
          thread_current()->fd_num++;
          lock_release(&file_lock);
          return i;
      }   
    }  
  }
  lock_release(&file_lock);
  return -1; 
}

int 
filesize (int fd) 
{
  if(thread_current()->fd[fd] == NULL){
    exit(-1);
  }
  return file_length(thread_current()->fd[fd]);
}

void 
seek (int fd, unsigned position)
{
  if (thread_current()->fd[fd] == NULL) {
    exit(-1);
  }
  file_seek(thread_current()->fd[fd], position);
}

unsigned 
tell (int fd) 
{
  if (thread_current()->fd[fd] == NULL) {
    exit(-1);
  }
  return file_tell(thread_current()->fd[fd]);
}

void 
close (int fd) 
{
  struct file* fp;
  if (thread_current()->fd[fd] == NULL) {
    exit(-1);
  }
  fp = thread_current()->fd[fd];
  thread_current()->fd[fd] = NULL;
  return file_close(fp);
}

/*modified: make system call handler*/
static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  int syscall_num;
  int arg[6];
  void *esp = f->esp;
  check_vaddr(esp);

  syscall_num = *(int *)esp;
  switch(syscall_num)
  {
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
      check_valid_buffer((void *)*(uint32_t *)(f->esp + 8), (unsigned)*(uint32_t *)(f->esp + 12), f->esp, true);
      f->eax = read((int)*(uint32_t *)(f->esp+4), (void *)*(uint32_t *)(f->esp + 8), 
            (unsigned)*((uint32_t *)(f->esp + 12)));
		  break;
	  case SYS_WRITE:
       check_vaddr(f->esp + 4); check_vaddr(f->esp + 8); check_vaddr(f->esp + 12);
       check_valid_buffer((void *)*(uint32_t *)(f->esp + 8), (unsigned)*(uint32_t *)(f->esp + 12), f->esp, true);
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
  }
}