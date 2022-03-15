#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include "lib/user/syscall.h"
#include "threads/vaddr.h"
#include "filesys/inode.h"
#include "threads/palloc.h"
#include "devices/input.h"
#include "filesys/directory.h"

/* modified5 : file flag */
#define FD_FILE 1
#define FD_DIRECTORY 2

/* modified5 : file descriptor */
struct file_desc {
  int id;
  struct list_elem elem;
  struct file* file;
  struct dir* dir;
};

/* modified5 : referenced from pintos doc */
static bool 
put_user (uint8_t *udst, uint8_t byte)
{
  int error_code;
  asm ("movl $1f, %0; movb %b2, %1; 1:"
       : "=&a" (error_code), "=m" (*udst) : "q" (byte));
  return error_code != -1;
}

void syscall_init (void);
/*modified: system call function*/
void check_vaddr(const void *vaddr);
void halt (void);
void exit (int status);
pid_t exec (const char *cmd_lime);
int wait (pid_t pid);
/*modified2: file system*/
bool create (const char *file, unsigned initial_size);
bool remove (const char *file);
int open (const char *file);
int filesize (int fd);
int read (int fd, void *buffer, unsigned size);
int write (int fd, const void *buffer, unsigned size);
void seek (int fd, unsigned position);
unsigned tell (int fd);
void close (int fd);
/*modified5 : file system system call */
bool chdir(const char *dir);
bool mkdir(const char *dir);
bool readdir(int fd, char *name);
bool isdir(int fd);
int inumer(int fd);
/*modified: additional system call function*/
int fibonacci(int n);
int max_of_four_int(int a, int b, int c, int d);
/*modfied4 : file system */
struct file_desc* find_file_desc(struct thread *t, int fd, int flag);

#endif /* userprog/syscall.h */
