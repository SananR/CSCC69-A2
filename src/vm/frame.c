#include "vm/frame.h"
#include <stdio.h>
#include "threads/synch.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/thread.h"

/* List of allocated frames */
static struct list lru_list;
/* Lock for lru list. */
static struct lock lru_lock;


struct frame *find_frame (struct virtual_memory_entry *vm_entry);

void
initialize_lru_list ()
{
	list_init (&lru_list);
	lock_init (&lru_lock);
} 

uint8_t *
allocate_frame (struct virtual_memory_entry *vm_entry, enum palloc_flags flag)
{
  	uint8_t *kpage = palloc_get_page (flag);

  	if (kpage != NULL) 
  	{
  		struct frame *fm = malloc(sizeof(struct frame));
  		if (fm == NULL)
  		{
  			palloc_free_page (kpage);
  			return NULL;
  		}
  		fm->page = kpage;
  		fm->vm_entry = vm_entry;
  		fm->owner = thread_current ();

  		//Insert into LRU List
  		lock_acquire (&lru_lock);
  		list_push_back (&lru_list, &fm->elem);
  		lock_release (&lru_lock);
  	}
  	else
  	{
  		//TODO EVICTION 
  		return NULL;
  	}

  	return kpage;
}

bool
free_frame (struct virtual_memory_entry *vm_entry)
{
	struct frame *fm = find_frame (vm_entry);
	if (fm == NULL) 
		return false;
	list_remove (&fm->elem);
	palloc_free_page (fm->page);
	free (fm);
}

struct frame *
find_victim_frame ()
{

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