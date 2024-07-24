#ifndef VM_PAGE_H
#define VM_PAGE_H

#include "filesys/off_t.h"
#include <hash.h>
#include <stdbool.h>
#include "userprog/syscall.h"

/* The type of the virtual memory entry */
enum virtual_memory_type
  {
    FILE_PAGE,
    SWAP_PAGE,
    MMAP_PAGE
  };

struct virtual_memory_entry
  {
      uint8_t *uaddr;                       /* User virtual address of page */
  		struct hash_elem hash_elem;           /* Hash table element. */
      struct list_elem list_elem;           /* List element used for memory mapped files */
      enum virtual_memory_type page_type;   /* The virtual memory type, either file page or a swap page */
      bool pinned;                          /* Pinning mechanism used to prevent page faults in kernel */

      struct file *file;                    /* Reference to the user file */
      uint32_t read_bytes;                  /* Number of read bytes for loading the file */
      uint32_t zero_bytes;                  /* Number of zeroed bytes for loading the file */
      off_t ofs;                            /* Offset for reading the file */

      size_t swap_index;                    /* Index of the stored swap page */

      bool writable;                        /* Whether this frame can be written to */
      bool in_memory;                       /* Whether this frame is in memory */
  };


bool virtual_memory_entry_less (const struct hash_elem *a_, const struct hash_elem *b_, void *aux);
unsigned virtual_memory_entry_hash (const struct hash_elem *p_, void *aux);
void virtual_memory_destroy (struct hash_elem *e, void *aux);

struct virtual_memory_entry *find_vm_entry (uint8_t *uaddr);
void clear_vm_entry (struct virtual_memory_entry *vm_entry);
bool handle_vm_page_fault (struct virtual_memory_entry *vm_entry, bool pin_frame);

bool is_stack_grow_access (void *addr, uint32_t *esp);
struct virtual_memory_entry *create_swap_page_entry (void *addr);
bool create_file_page (void *upage, struct file *file, uint32_t read_bytes,
             uint32_t zero_bytes, off_t ofs, bool writable, mapid_t map_id);

#endif /* vm/page.h */