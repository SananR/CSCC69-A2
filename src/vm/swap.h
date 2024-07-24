#ifndef VM_SWAP_H
#define VM_SWAP_H

#include <stddef.h>

void swap_init (void);
void swap_to_memory (size_t swap_index, void *uaddr);
size_t memory_to_swap (void *uaddr);

#endif /* vm/swap.h */