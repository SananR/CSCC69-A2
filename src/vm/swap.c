#include "swap.h"
#include "lib/kernel/bitmap.h"
#include "devices/block.h"
#include "threads/synch.h"
#include "lib/debug.h"

/* Bitmap used to manage swap state */
static struct bitmap *swap_bitmap;
static struct lock swap_lock;
static struct block *swap_block;

void 
swap_init ()
{
   lock_init (&swap_lock);
   swap_block = block_get_role (BLOCK_SWAP);
   // 8 sectors per page (512 bytes per sector)
   swap_bitmap = bitmap_create (block_size (swap_block) / 8);
   if (swap_bitmap == NULL || swap_block == NULL)
      PANIC("Swap initialization failed");
}

size_t
memory_to_swap (void *uaddr)
{
   if (!lock_held_by_current_thread (&swap_lock))
      lock_acquire (&swap_lock);

   // Find the next empty swap slot and mark as used
   size_t next_empty = bitmap_scan (swap_bitmap, 0, 1, false);

   // No empty slots or other error
   if (next_empty == BITMAP_ERROR)
   {
      lock_release (&swap_lock);
      return -1;
   }
   bitmap_flip (swap_bitmap, next_empty);

   // Write the data from user address to swap
   for (int i=0; i<8; i++)
      block_write (swap_block, (next_empty * 8) + i, (uint8_t *)uaddr + i * BLOCK_SECTOR_SIZE);

   lock_release (&swap_lock);
   return next_empty;
}

void 
swap_to_memory (size_t swap_index, void *uaddr)
{
   if (!lock_held_by_current_thread (&swap_lock))
      lock_acquire (&swap_lock);

   // Index is not marked as used
   if (!bitmap_test (swap_bitmap, swap_index))
      PANIC ("Tried to swap an index into memory that isn't marked as used.");

   // Mark the index as used
   bitmap_flip (swap_bitmap, swap_index);

   // Read page into user address
   for (int i=0; i<8; i++)
      block_read (swap_block, (swap_index * 8) + i, (uint8_t *)uaddr + i * BLOCK_SECTOR_SIZE);

   lock_release (&swap_lock);
}