#include "vm/page.h"
#include <hash.h>
#include <string.h>
#include "threads/palloc.h"
#include "userprog/process.h"
#include "userprog/pagedir.h"
#include "filesys/file.h"
#include "threads/malloc.h"
#include "filesys/filesys.h"
#include "threads/vaddr.h"
#include "vm/frame.h"

#define PROCESS_MAXIMUM_STACK_SIZE 8000000 // 8 MB

/* Returns a hash value for virtual memory entry. */
unsigned
virtual_memory_entry_hash (const struct hash_elem *p_, void *aux UNUSED)
{
  const struct virtual_memory_entry *vm_entry = hash_entry (p_, struct virtual_memory_entry, hash_elem);
  return hash_bytes (&vm_entry->uaddr, sizeof vm_entry->uaddr);
}

/* Returns true if virtual memory entry a precedes virtual memory entry b. */
bool
virtual_memory_entry_less (const struct hash_elem *a_, const struct hash_elem *b_,
           void *aux UNUSED)
{
  const struct virtual_memory_entry *a = hash_entry (a_, struct virtual_memory_entry, hash_elem);
  const struct virtual_memory_entry *b = hash_entry (b_, struct virtual_memory_entry, hash_elem);

  return a->uaddr < b->uaddr;
}

void
virtual_memory_destroy (struct hash_elem *e, void *aux UNUSED)
{
  struct virtual_memory_entry *a = hash_entry (e, struct virtual_memory_entry, hash_elem);
  free(a);
}

void 
clear_vm_entry (struct virtual_memory_entry *vm_entry)
{
  // Clear from virtual memory hash table 
  hash_delete (&thread_current ()->virtual_memory, &vm_entry->hash_elem);
  // Clear from the page directory so the page may be used by another process
  pagedir_clear_page (thread_current ()->pagedir, vm_entry->uaddr);

  free_frame (vm_entry);
  free (vm_entry);
}

struct virtual_memory_entry *
find_vm_entry (uint8_t *uaddr)
{
  struct hash *vm = &thread_current()->virtual_memory;
  struct hash_elem *e;
  struct virtual_memory_entry vm_entry;

  vm_entry.uaddr = pg_round_down (uaddr);
  e = hash_find (vm, &vm_entry.hash_elem);
  return e != NULL ? hash_entry (e, struct virtual_memory_entry, hash_elem) : NULL;
}


/* 
	Handler for page faults in order to implement virtual memory. 
	Returns true if successful, and false if any issues occurred. 
*/
bool
handle_vm_page_fault (struct virtual_memory_entry *vm_entry)
{
  /* 
    Handle file page virtual memory entries 
  */
  if (vm_entry->page_type == FILE_PAGE)
  {
    /* Get a page of memory. */
  	uint8_t *kpage = allocate_frame (vm_entry, PAL_USER);

  	if (kpage == NULL)
    	return false;

  	struct file *file = vm_entry->file;
    size_t page_read_bytes = vm_entry->read_bytes;
    size_t page_zero_bytes = vm_entry->zero_bytes;

    file_seek (file, vm_entry->ofs);

    /* Load this page. */
    if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
    { 
      free_frame (vm_entry);
      return false; 
    }
    memset (kpage + page_read_bytes, 0, page_zero_bytes);

    /* Add the page to the process's address space. */
    if (!install_page (vm_entry->uaddr, kpage, vm_entry->writable)) 
    {
      free_frame (vm_entry);
      return false; 
    }
    // Mark the virtual memory entry as in-memory
    vm_entry->in_memory = true;

  	return true;
  }
  // TODO SWAP PAGE Handler
  else if (vm_entry->page_type == SWAP_PAGE)
  {
  	return true;
  }
  return false;
}

bool 
is_stack_grow_access (void *addr, uint32_t *esp)
{
  void *new_stack_top = PHYS_BASE - pg_round_down (addr);
  if (new_stack_top >= (void *) PROCESS_MAXIMUM_STACK_SIZE)
    return false;
  // Within 32B of current stack top 
  else if ((uint32_t *)addr >= (esp - 32))
    return true;
  else return false;
}

bool
create_stack_entry (void *addr)
{
  struct virtual_memory_entry *vm_entry = malloc(sizeof(struct virtual_memory_entry));
  if (vm_entry == NULL)
    return false;

  vm_entry->uaddr = pg_round_down (addr);
  vm_entry->writable = true;
  vm_entry->in_memory = true;
  vm_entry->page_type = SWAP_PAGE;

  uint8_t *kpage = allocate_frame (vm_entry, PAL_USER | PAL_ZERO);

  if (kpage == NULL)
  {
    free (vm_entry);
    return false;
  }

  bool success = install_page (vm_entry->uaddr, kpage, true);
  if (!success)
  {
    free_frame (vm_entry);
    free (vm_entry);
    return false;
  }

  hash_insert (&thread_current()->virtual_memory, &vm_entry->hash_elem);

  return true;
}






