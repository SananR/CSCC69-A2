#include "vm/frame.h"
#include <stdio.h>
#include "threads/synch.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
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
/* Used to implement the clock algorithm for eviction */
static struct list_elem *clock_hand; 


struct frame *find_frame (struct virtual_memory_entry *vm_entry);

void
initialize_lru_list ()
{
	list_init (&lru_list);
	lock_init (&lru_lock);
	clock_hand = NULL;
} 

struct frame *
allocate_frame (struct virtual_memory_entry *vm_entry, enum palloc_flags flag)
{
  	uint8_t *kpage = palloc_get_page (flag);

  	if (kpage != NULL) 
  	{
  		goto done;
  	}
  	else
  	{
  		struct frame *victim = evict_frame ();
  		if (victim == NULL)
  			PANIC ("Unable to evict a frame!");

  		// Update frame metadata to show new owner and vm entry
		victim->vm_entry = vm_entry;
		victim->owner = thread_current ();

		// Zero the page if PAL_ZERO is set
		if (flag & PAL_ZERO)
        	memset (victim->page, 0, PGSIZE);

		return victim;
  	}	
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
	lock_init (&fm->frame_lock);

	//Insert into LRU List
   	if (!lock_held_by_current_thread (&lru_lock))
		lock_acquire (&lru_lock);
	list_push_back (&lru_list, &fm->elem);
	lock_release (&lru_lock);

  	return fm;
}

void
free_frame (struct frame *fm)
{
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

struct frame * 
evict_frame ()
{
	struct frame *victim = find_victim_frame ();

	if (victim == NULL)
		return NULL;

	struct virtual_memory_entry *vm_entry = victim->vm_entry;

	/* Do not evict pinned frames */
	while (vm_entry->pinned)
	{
		victim = find_victim_frame ();
		vm_entry = victim->vm_entry;
	}

	if (vm_entry == NULL)
		PANIC("Virtual memory entry does not exist for frame");

	// Acquire lock on the frame so that it cannot be faulted in while being evicted and vise versa 
	lock_acquire (&victim->frame_lock);
	vm_entry->in_memory = false;

	if (pagedir_is_dirty (victim->owner->pagedir, vm_entry->uaddr) && vm_entry->writable)
	{
		if (vm_entry->page_type == MMAP_PAGE)
		{
			lock_acquire (&file_lock);
			file_seek (vm_entry->file, 0);
      		file_write_at (vm_entry->file, vm_entry->uaddr, vm_entry->read_bytes, vm_entry->ofs);
      		lock_release (&file_lock);
		}
		else
		{
			size_t index = memory_to_swap (vm_entry->uaddr);
			if (index < 0)
			{
				lock_release (&victim->frame_lock);
				return NULL;
			}
			vm_entry->swap_index = index;
			vm_entry->page_type = SWAP_PAGE;
		}
	}

	// Removing from existing pagedir, but reusing the same frame 
	pagedir_clear_page (victim->owner->pagedir, vm_entry->uaddr);

	lock_release (&victim->frame_lock);

	return victim;
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
	while (pagedir_is_accessed (victim->owner->pagedir, victim->vm_entry->uaddr) &&
		   pagedir_is_accessed (victim->owner->pagedir, victim->page))
	{
		// Clear accessed and move hand to next
		pagedir_set_accessed (victim->owner->pagedir, victim->vm_entry->uaddr, false);
		pagedir_set_accessed (victim->owner->pagedir, victim->page, false);
		clock_hand = list_next (clock_hand);
		// If last entry, move back to start (circular)
		if (list_tail (&lru_list) == clock_hand)
			clock_hand = list_begin (&lru_list);
		victim = list_entry (clock_hand, struct frame, elem);
		while (!is_user_vaddr(victim->vm_entry->uaddr))
		{
			clock_hand = list_next (clock_hand);
			// If last entry, move back to start (circular)
			if (list_tail (&lru_list) == clock_hand)
				clock_hand = list_begin (&lru_list);
			victim = list_entry (clock_hand, struct frame, elem);
		}
	}
    clock_hand = list_next(clock_hand);
    if (clock_hand == list_end(&lru_list)) {
        clock_hand = list_begin(&lru_list);
    }
	
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