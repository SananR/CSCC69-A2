#include "vm/frame.h"
#include <stdio.h>
#include "threads/synch.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include <stdlib.h>
#include "lib/random.h"
#include "lib/kernel/list.h"
#include "filesys/file.h"
#include "swap.h"

/* List of allocated frames */
static struct list lru_list;
/* Lock for lru list. */
static struct lock lru_lock;
static struct list_elem *clock_hand; 


struct frame *find_frame (struct virtual_memory_entry *vm_entry);

void
initialize_lru_list ()
{
	list_init (&lru_list);
	lock_init (&lru_lock);
	clock_hand = NULL;
} 

uint8_t *
allocate_frame (struct virtual_memory_entry *vm_entry, enum palloc_flags flag)
{
  	uint8_t *kpage = palloc_get_page (flag);

  	if (kpage != NULL) 
  		goto done;
  	else if (evict_frame ())
  	{
		kpage = palloc_get_page (flag);
		goto done;
  	}	
	else PANIC ("Unable to evict a frame!");
	
  done: ;
    struct frame *fm = malloc(sizeof(struct frame));
	if (fm == NULL)
	{
		palloc_free_page (kpage);
		return NULL;
	}
	fm->page = kpage;
	fm->vm_entry = vm_entry;
	fm->owner = thread_current ();
	//fm->pinned = false;

	//Insert into LRU List
   	if (!lock_held_by_current_thread (&lru_lock))
		lock_acquire (&lru_lock);
	list_push_back (&lru_list, &fm->elem);
	lock_release (&lru_lock);

  	return kpage;
}

// void 
// free_all_frames (struct thread *t)
// {
// 	struct list_elem *e;

// 	for (e = list_begin (&lru_list); e != list_end (&lru_list); e = list_next (e))
// 	{
// 		struct frame *fm = list_entry (e, struct frame, elem);
// 		if (fm->owner == t)
// 		{
// 			list_remove (&fm->elem);
// 			palloc_free_page (fm->page);
// 			free (fm);
// 		}
// 	}
// }

void
free_frame (struct frame *fm)
{
	//struct frame *fm = find_frame (vm_entry);
	if (fm == NULL) 
		return;
	// If frame was loaded in memory then clear the pagedir entry
	if (fm->vm_entry->in_memory)
	{
		fm->vm_entry->in_memory = false;
		pagedir_clear_page (fm->owner->pagedir, fm->vm_entry->uaddr);
	}
	list_remove (&fm->elem);
	palloc_free_page (fm->page);
	free (fm);
}

void
free_vm_frame (struct virtual_memory_entry *vm_entry)
{
	struct frame *fm = find_frame (vm_entry);
	free_frame (fm);
}

bool 
evict_frame ()
{
	struct frame *victim = find_victim_frame ();

	if (victim == NULL)
		return false;

	struct virtual_memory_entry *vm_entry = victim->vm_entry;

	/* Do not evict pinned frames */
	while (vm_entry->pinned)
	{
		victim = find_victim_frame ();
		vm_entry = victim->vm_entry;
	}

	if (vm_entry == NULL)
		PANIC("Virtual memory entry does not exist for frame");

	if (pagedir_is_dirty (victim->owner->pagedir, vm_entry->uaddr) && vm_entry->writable)
	{
		// /* Writable file pages */
		// if (vm_entry->page_type == FILE_PAGE && vm_entry->writable)
		// {
		// 	// Write back to the backed file
  		// 	lock_acquire (&file_lock);
        //   	file_seek (vm_entry->file, 0);
        //   	file_write_at (vm_entry->file, vm_entry->uaddr, vm_entry->read_bytes, vm_entry->ofs);
  		// 	lock_release (&file_lock);
		// }
		// /* Swap pages */
		// else if (vm_entry->page_type == SWAP_PAGE)
		// {
			size_t index = memory_to_swap (vm_entry->uaddr);
			if (index < 0)
				return false;
			vm_entry->swap_index = index;
			vm_entry->page_type = SWAP_PAGE;
		//}
	}

	// Free the victim frame
	free_frame (victim);

	return true;
}

// TODO Clock replacement / LRU algorithm 
struct frame *
find_victim_frame ()
{
   	if (!lock_held_by_current_thread (&lru_lock))
		lock_acquire (&lru_lock);
	size_t ls = list_size(&lru_list);  
	if (ls == 0)
	{
		lock_release (&lru_lock);
    	return NULL;
	}

	if (clock_hand == NULL)
		clock_hand = list_begin (&lru_list);

	struct frame *victim = list_entry (clock_hand, struct frame, elem);

	// Clock algorithm 
	while (pagedir_is_accessed (victim->owner->pagedir, victim->vm_entry->uaddr))
	{
		// Clear accessed and move hand to next
		pagedir_set_accessed (victim->owner->pagedir, victim->vm_entry->uaddr, false);
		clock_hand = list_next (clock_hand);
		// If last entry, move back to start (circular)
		if (list_tail (&lru_list) == clock_hand)
			clock_hand = list_begin (&lru_list);
		victim = list_entry (clock_hand, struct frame, elem);
	}

    // Generate a random index
  	//size_t victim_index = random_ulong() % ls;

	// Iterate through the list to the random index
	// struct list_elem *e = list_begin (&lru_list);
	// for (size_t i = 0; i < victim_index; i++) {
	//   e = list_next (e);
	// }

	// Get the frame at the random index
	//struct frame *victim = list_entry (e, struct frame, elem);
	lock_release (&lru_lock);
	return victim;
}

struct frame *
find_frame (struct virtual_memory_entry *vm_entry)
{
	struct list_elem *e;

	for (e = list_begin (&lru_list); e != list_end (&lru_list); e = list_next (e))
	{
		struct frame *fm = list_entry (e, struct frame, elem);
		if (fm->vm_entry == vm_entry)
			return fm;
	}
	return NULL;
}