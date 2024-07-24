#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "vm/page.h"
#include "threads/palloc.h"

struct frame
  {
  	  uint8_t *page;							                /* Reference to the physical page address */
      struct thread *owner;                       /* Reference to the owner thread/process */
  	  struct virtual_memory_entry *vm_entry;      /* Reference to the corresponding virtual memory entry */
  	  struct list_elem elem;                      /* List element for LRU list */
  };

void initialize_lru_list (void);
uint8_t *allocate_frame (struct virtual_memory_entry *vm_entry, enum palloc_flags flag);
void free_frame (struct virtual_memory_entry *vm_entry);
void free_all_frames (struct thread *t);
struct frame *find_victim_frame (void);
bool evict_frame (void);

#endif /* vm/frame.h */