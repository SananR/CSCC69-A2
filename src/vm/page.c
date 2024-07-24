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
#include "swap.h"

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
    
    lock_acquire(&file_lock);
    file_seek (file, vm_entry->ofs);

    /* Load this page. */
    if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
    { 
      lock_release (&file_lock);
      free_frame (vm_entry);
      return false; 
    }
    lock_release (&file_lock);
    
    memset (kpage + page_read_bytes, 0, page_zero_bytes);

    /* Add the page to the process's address space. */
    if (!install_page (vm_entry->uaddr, kpage, vm_entry->writable)) 
    {
      lock_release (&file_lock);
      free_frame (vm_entry);
      return false; 
    }
    // Mark the virtual memory entry as in-memory
    vm_entry->in_memory = true;

  	return true;
  }
  else if (vm_entry->page_type == SWAP_PAGE)
  {
    /* Get a page of memory. */
    uint8_t *kpage = allocate_frame (vm_entry, PAL_USER);

    if (kpage == NULL)
      return false;

    /* Add the page to the process's address space. */
    if (!install_page (vm_entry->uaddr, kpage, vm_entry->writable)) 
    {
      free_frame (vm_entry);
      return false; 
    }

    // Load data from swap
    swap_to_memory (vm_entry->swap_index, vm_entry->uaddr);
    vm_entry->in_memory = true;
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

struct virtual_memory_entry *
create_swap_page_entry (void *addr)
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
    return NULL;
  }

  bool success = install_page (vm_entry->uaddr, kpage, true);
  if (!success)
  {
    free_frame (vm_entry);
    free (vm_entry);
    return NULL;
  }

  hash_insert (&thread_current()->virtual_memory, &vm_entry->hash_elem);

  return vm_entry;
}

bool
create_file_page (void *upage, struct file *file, uint32_t read_bytes,
             uint32_t zero_bytes, off_t ofs, bool writable, mapid_t map_id)
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  struct mmap_file *mfile = NULL;

  // Create and add mmap_file entry if this is for memory mapped file
  if (map_id >= 0)
  {
    mfile = malloc (sizeof (struct mmap_file));
    if (mfile == NULL)
      return false;
    mfile->map_id = map_id;
    list_init (&mfile->vm_entries);
    list_push_back (&thread_current ()->mmap_list, &mfile->elem);
  }

  while (read_bytes > 0 || zero_bytes > 0) 
  {
    /* Calculate how to fill this page.
       We will read PAGE_READ_BYTES bytes from FILE
       and zero the final PAGE_ZERO_BYTES bytes. */
    size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
    size_t page_zero_bytes = PGSIZE - page_read_bytes;

    /* Create and initialize virtual memory entry */
    struct virtual_memory_entry *vm_entry = malloc(sizeof(struct virtual_memory_entry));

    if (vm_entry == NULL) 
      return false;
    
    vm_entry->uaddr = upage;
    vm_entry->file = file;
    vm_entry->read_bytes = page_read_bytes;
    vm_entry->zero_bytes = page_zero_bytes;
    vm_entry->ofs = ofs;
    vm_entry->writable = writable;
    vm_entry->in_memory = false;
    vm_entry->page_type = FILE_PAGE;

    // Add vm_entry to virtual memory hash table
    hash_insert (&thread_current()->virtual_memory, &vm_entry->hash_elem);

    // Add entries to mmap_file for memory mapped files 
    if (mfile != NULL)
    {
      list_push_back (&mfile->vm_entries, &vm_entry->list_elem);
    }

    /* Advance. */
    read_bytes -= page_read_bytes;
    zero_bytes -= page_zero_bytes;
    upage += PGSIZE;
    ofs += PGSIZE;
  }
  return true;
}




