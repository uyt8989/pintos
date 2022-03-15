#include "threads/thread.h"
#include "userprog/syscall.h"
#include "lib/user/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/*check address(User Memory Access)*/
void 
check_vaddr(const void *vaddr) 
{
  if (!is_user_vaddr(vaddr)) {
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
  return process_execute(cmd_line);
}

void 
exit (int status) 
{
  thread_current()->exit_status = status;
  printf("%s: exit(%d)\n", thread_name(), status);
  thread_exit ();
}

int
wait(pid_t pid)
{
  return process_wait(pid);
}

int 
read (int fd, void* buffer, unsigned size) 
{
  int i = -1;
  if (fd == 0) {
    for(i = 0; i < size; i++)
      if(input_getc() == '\0'){
        break;
      }
  }
  return i;
}

int 
write (int fd, const void *buffer, unsigned size) 
{
  if (fd == 1) {
    putbuf((char *)buffer, size);
    return size;
  }
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
      break;
    case SYS_REMOVE:
      break;
    case SYS_OPEN:
      break;
    case SYS_FILESIZE:
      break;
    case SYS_READ:
      check_vaddr(f->esp + 20); check_vaddr(f->esp + 24); check_vaddr(f->esp + 28);
      f->eax = read((int)*(uint32_t *)(f->esp+20), (void *)*(uint32_t *)(f->esp + 24), 
            (unsigned)*((uint32_t *)(f->esp + 28)));
      break;
    case SYS_WRITE:
      check_vaddr(f->esp + 20); check_vaddr(f->esp + 24); check_vaddr(f->esp + 28);
      f->eax = write((int)*(uint32_t *)(f->esp+20), (void *)*(uint32_t *)(f->esp + 24), 
            (unsigned)*((uint32_t *)(f->esp + 28)));
      break;
    case SYS_SEEK:
      break;
    case SYS_TELL:
      break;
    case SYS_CLOSE:
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

