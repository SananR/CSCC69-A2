#include "page.h"
#include <hash.h>


/* Returns a hash value for virtual memory entry. */
unsigned
virtual_memory_entry_hash (const struct hash_elem *p_, void *aux UNUSED)
{
  const struct page *p = hash_entry (p_, struct virtual_memory_entry, hash_elem);
  return hash_bytes (&p->uaddr, sizeof p->uaddr);
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

struct virtual_memory_entry *
find_vm_entry (uint8_t *uaddr)
{
  struct hash *vm = &thread_current()->virtual_memory;
  struct hash_elem *e;
  struct virtual_memory_entry *vm_entry;

  vm_entry.uaddr = uaddr;
  e = hash_find (&vm, &vm_entry.hash_elem);
  return e != NULL ? hash_entry (e, struct page, hash_elem) : NULL;
}


/* 
	Handler for page faults in order to implement virtual memory. 
	Returns true if successful, and false if any issues occurred. 
*/
bool
handle_vm_page_fault (struct virtual_memory_entry *vm_entry)
{
  // Handle file page virtual memory entries
  if (vm_entry->page_type == FILE_PAGE)
  {
    /* Get a page of memory. */
  	uint8_t *kpage = palloc_get_page (PAL_USER);
  	if (kpage == NULL)
    	return false;

    size_t page_read_bytes = vm_entry->read_bytes;
    size_t page_zero_bytes = vm_entry->zero_bytes;
    
    /* Load this page. */
    if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
    {
      palloc_free_page (kpage);
      return false; 
    }
    memset (kpage + page_read_bytes, 0, page_zero_bytes);
    /* Add the page to the process's address space. */
    if (!install_page (vm_entry->uaddr, kpage, vm_entry->writable)) 
    {
      palloc_free_page (kpage);
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
}