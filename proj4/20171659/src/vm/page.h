#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <hash.h>

#define VM_BIN 0
#define VM_FILE 1
#define VM_ANON 2
#define CLOSE_ALL 9999

/* struct for vm_entry */
struct vm_entry{
	uint8_t type;                      // VM_BIN, VM_FILE, VM_ANON
	void *vaddr;                       // virtual address 
	bool writable;                     
	bool is_loaded;                    // if true, physical memory is loaded
	struct file *file;
	struct list_elem mmap_elem;        // list_elem for mmap_file's vm_list
	size_t offset;
	size_t read_bytes;                   
	size_t zero_bytes;
	size_t swap_slot;
	struct hash_elem elem;             // hash elem for thread's vm
};

/* struct for page */
struct page{
	void *kaddr;
	struct vm_entry *vme;
	struct thread *pg_thread;
	struct list_elem lru;
};

void vm_init(struct hash *vm);
bool vm_less_func(const struct hash_elem *a, const struct hash_elem *b, void *aux);
unsigned vm_hash_func(const struct hash_elem *e, void *aux);
void vm_destroy(struct hash *vm);
void vm_destroy_func(struct hash_elem *e, void *aux);
struct vm_entry *find_vme(void *vaddr);
bool insert_vme(struct hash *vm, struct vm_entry *vme);
bool delete_vme(struct hash *vm, struct vm_entry *vme);
bool load_file(void *kaddr, struct vm_entry *vme);

#endif