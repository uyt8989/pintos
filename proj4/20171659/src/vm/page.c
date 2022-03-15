#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <threads/malloc.h>
#include <threads/palloc.h>
#include "filesys/file.h"
#include "vm/page.h"
#include "vm/frame.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "userprog/syscall.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "lib/kernel/list.h"

void 
vm_init(struct hash *vm)
{
	/* initialize hash */
	hash_init(vm, vm_hash_func, vm_less_func, NULL);
}

/* if a's vm_entry adress is less than b's vm_entry address return true */
bool 
vm_less_func(const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED)
{
	struct vm_entry *vme_a = hash_entry(a, struct vm_entry, elem);
	struct vm_entry *vme_b = hash_entry(b, struct vm_entry, elem);

	if(vme_a->vaddr < vme_b->vaddr) return true;
	else return false;
}


unsigned 
vm_hash_func(const struct hash_elem *e, void *aux UNUSED)
{
	struct vm_entry *vme = hash_entry(e, struct vm_entry, elem);

	return hash_int((int)vme->vaddr);
}

/* destroy supplementary page table */
void 
vm_destroy(struct hash *vm)
{
	/* destory hash */
	hash_destroy(vm, vm_destroy_func);
}

/* destroy memory of vm_entry */
void 
vm_destroy_func(struct hash_elem *e, void *aux UNUSED)
{
	struct vm_entry *vme = hash_entry(e, struct vm_entry, elem);
	void *physical_address;

	/* if virtual address is loaded on physical memory */
	if(vme->is_loaded == true){
		/* get physical_address and free page */
		physical_address = pagedir_get_page(thread_current()->pagedir, vme->vaddr);
		free_page(physical_address);
		
		/* clear page table */
		pagedir_clear_page(thread_current()->pagedir, vme->vaddr);
	}
	/* free vm_entry */
	free(vme);
}

/* find vm_entry using virtual address */
struct vm_entry*
find_vme(void *vaddr)
{
	struct vm_entry vme;
	struct hash_elem *element;

	/* try to find vm_entry by hash_find*/
	vme.vaddr = pg_round_down(vaddr);
	element = hash_find(&thread_current()->vm, &vme.elem);
	
	/* if get a element return vm_entry */
	if(element != NULL)
		return hash_entry(element, struct vm_entry, elem);

	return NULL;
}

/* insert vm_entry to page table */
bool 
insert_vme(struct hash *vm, struct vm_entry *vme)
{
	/* if hash_insert is success, return true */
	if(hash_insert(vm, &vme->elem) == NULL)
		return true;

	return false;
}

/* delete vm_entry from page table */
bool 
delete_vme(struct hash *vm, struct vm_entry *vme)
{
	/* if hash_delete is success, return true */
	if(hash_delete(vm, &vme->elem) != NULL){
		free(vme);
		return true;
	}

	return false;
}

bool 
load_file(void *kaddr, struct vm_entry *vme)
{
	/* file read and if success, return true */
	/* read vm_entry's file to physical memory.*/
	if((int)vme->read_bytes == file_read_at(vme->file, kaddr, vme->read_bytes, vme->offset)){
		memset(kaddr + vme->read_bytes, 0, vme->zero_bytes);
		return true;
	} 
	return false;
}