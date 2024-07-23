#include "userprog/syscall.h"
#include <stdio.h>
#include <stdlib.h>
#include "devices/shutdown.h"
#include "devices/input.h"
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "../syscall-nr.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "process.h"
#include "vm/page.h"
#include "userprog/pagedir.h"

#define USER_PROCESS_MAXIMUM_ARGUMENTS 5

struct process_file* get_process_file (int fd);
void validate_user_string (char *str);
void validate_user_address (uint8_t * addr);
void extract_arguments (struct intr_frame *f, int *buf, int count);
static void syscall_handler (struct intr_frame *);

/* Reads a byte at user virtual address UADDR.
   UADDR must be below PHYS_BASE.
   Returns the byte value if successful, -1 if a segfault
   occurred. */
static int
get_user (const uint8_t *uaddr)
{
  int result;
  asm ("movl $1f, %0; movzbl %1, %0; 1:"
       : "=&a" (result) : "m" (*uaddr));
  return result;
}

static void
copy_in (void *dst_, const void *usrc_, size_t size)
{
  uint8_t *dst = dst_;
  const uint8_t *usrc = usrc_;
  for (; size > 0; size--, dst++, usrc++) 
    *dst = get_user (usrc);
}

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}


static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  // Save user stack pointer to handle kernel page fault stack growth
  thread_current()->user_esp = f->esp;

  unsigned syscall_number;
  int args[USER_PROCESS_MAXIMUM_ARGUMENTS];

  // Validate 4 bytes for syscall number
  for (unsigned i=0; i<=sizeof syscall_number; i++) 
    validate_user_address (f->esp + i);

  copy_in (&syscall_number, f->esp, sizeof syscall_number);

  switch (syscall_number)
  {
    case SYS_WRITE:
      extract_arguments (f, args, 3);
      for (unsigned i=0; i < (unsigned) args[2]; i++)
        validate_user_address ((void *) args[1] + i);
      f->eax = write (args[0], (void *)args[1], (unsigned) args[2]);
      break;
    case SYS_READ:
      extract_arguments (f, args, 3);
      for (unsigned i=0; i < (unsigned) args[2]; i++)
        validate_user_address ((void *) args[1] + i);
      f->eax = read (args[0], (void *)args[1], (unsigned) args[2]);
      break;
    case SYS_EXIT:
      extract_arguments (f, args, 1);
      exit (args[0]);
      break;
    case SYS_HALT:
      halt ();
      break;
    case SYS_EXEC:
      extract_arguments (f, args, 1);
      validate_user_string ((char *)args[0]);
      f->eax = exec ((char *)args[0]);
      break;
    case SYS_CREATE:
      extract_arguments (f, args, 2);
      validate_user_string ((char *)args[0]);
      f->eax = create ((char *)args[0], (unsigned) args[1]);
      break;
    case SYS_REMOVE:
      extract_arguments (f, args, 1);
      validate_user_string ((char *)args[0]);
      f->eax = remove ((char *)args[0]);
      break;
    case SYS_OPEN:
      extract_arguments (f, args, 1);
      validate_user_string ((char *)args[0]);
      f->eax = open ((char *)args[0]);
      break;
    case SYS_CLOSE:
      extract_arguments (f, args, 1);
      close ((int)args[0]);
      break;
    case SYS_FILESIZE:
      extract_arguments (f, args, 1);
      f->eax = filesize ((int)args[0]);
      break;
    case SYS_TELL:
      extract_arguments (f, args, 1);
      f->eax = tell ((int)args[0]);
      break;
    case SYS_SEEK:
      extract_arguments (f, args, 2);
      seek ((int) args[0], (unsigned) args[1]);
      break;
    case SYS_WAIT:
      extract_arguments (f, args, 1);
      f->eax = wait ((tid_t) args[0]);
      break;
    case SYS_MMAP:
      extract_arguments (f, args, 2);
      f->eax = mmap ((int) args[0], (void *)args[1]);
      break;
    case SYS_MUNMAP:
      extract_arguments (f, args, 1);
      munmap ((mapid_t) args[0]);
      break;
  }
}

/*
  System call handlers
*/

void 
munmap (mapid_t mapping)
{
  if (mapping < 0) return;

  struct list *mmap_list = &thread_current ()->mmap_list;
  struct list_elem *e = list_begin(mmap_list);

  lock_acquire (&file_lock);
  for (e = list_begin (mmap_list); e != list_end (mmap_list); e = list_next (e))
  {
    struct mmap_file *mfile = list_entry (e, struct mmap_file, elem);
    // Find mmap_file entry
    if (mfile->map_id == mapping)
    {
      struct list *vm_entries = &mfile->vm_entries;
      struct list_elem *e2 = list_begin(vm_entries);
      // Iterate all corresponding vm_entries for this mapped file 
      while (e2 != list_end(vm_entries))
      {
        struct virtual_memory_entry *vm_entry = list_entry (e2, struct virtual_memory_entry, list_elem);
        // Save the next element since we will free the current vm_entry's memory
        struct list_elem *next = list_next(e2);
        // Check if page was written to (dirty) and write back to file if it was
        if (pagedir_is_dirty (thread_current ()->pagedir, vm_entry->uaddr))
        {
          file_seek (vm_entry->file, 0);
          file_write_at (vm_entry->file, vm_entry->uaddr, vm_entry->read_bytes, vm_entry->ofs);
          //file_write (vm_entry->file, vm_entry->uaddr, vm_entry->read_bytes);
        }
        // Clear the virtual memory entry and corresponding mmap entry
        clear_vm_entry (vm_entry);
        e2 = next;
      }
      // Clear from memory mapped file list and free
      list_remove (&mfile->elem);
      free (mfile);
      break;
    }
  }
  lock_release (&file_lock);
}

mapid_t
mmap (int fd, void *addr)
{
  // Invalid arguments
  if (addr == (void *)0 || addr == NULL || fd == 1 || fd == 1)
    return -1;
  // Not page aligned or not a user address
  else if (!is_user_vaddr (addr) || (int)addr % PGSIZE != 0)
    return -1;

  lock_acquire (&file_lock);
  struct process_file *pf = get_process_file (fd);
  // File not opened 
  if (pf == NULL)
  {
    lock_release (&file_lock);
    return -1;
  }
  struct file *f = pf->file;
  off_t filesize = file_length(f);

  // File 0 length
  if (filesize <= 0)
  {
    lock_release (&file_lock);
    return -1;
  }
  lock_release (&file_lock);

  // Validate the user address
  // for (unsigned i=0; i < (unsigned) filesize; i++)
  //   if (is_kernel_vaddr (addr+i) || get_user (addr+i) == -1)
  //     return -1; 

  int num_pages = (filesize / PGSIZE) + ((filesize % PGSIZE) != 0); // Ceil of (filesize / PGSIZE)

  // Check if any of the pages at addr are already mapped 
  for (int i=0; i<num_pages; i++)
  {
    void *page = addr + (PGSIZE * i);
    struct virtual_memory_entry *vm_entry = find_vm_entry ((uint8_t *)page);
    if (vm_entry != NULL)
      return -1;
  }

  mapid_t map_id = thread_current ()->map_id++;
  uint32_t read_bytes = filesize;
  uint32_t zero_bytes = filesize <= PGSIZE ? PGSIZE - filesize : PGSIZE - (filesize % PGSIZE);
  off_t ofs = 0;
  zero_bytes = zero_bytes == PGSIZE ? 0 : zero_bytes;

  if (!load_segment (f, ofs, (uint8_t *) addr, read_bytes, zero_bytes, true, map_id))
    return -1;
  else return map_id;
}

int
wait (tid_t tid)
{
  return process_wait(tid);
}

void
seek (int fd, unsigned position)
{
  lock_acquire (&file_lock);
  struct process_file *pf = get_process_file (fd);
  if (!pf)
  {
    lock_release(&file_lock);
    return;
  }
  file_seek(pf->file, position);
  lock_release(&file_lock);
}

unsigned
tell (int fd)
{
  lock_acquire (&file_lock);
  struct process_file *pf = get_process_file (fd);
  if (!pf)
  {
    lock_release(&file_lock);
    return -1;
  }
  off_t offset = file_tell(pf->file);
  lock_release(&file_lock);
  return offset;
}

int 
read (int fd, void *buffer, unsigned size)
{
  if (size <= 0) 
    return size;
  
  //Keyboard input
  if (fd == 0)
  {
    for (unsigned i = 0; i < size; i++)
    {
      validate_user_address (buffer + i);
      *((uint8_t *)buffer + i) = input_getc(); 
    }
    return size;
  }
  else
  {
    lock_acquire (&file_lock);
    struct process_file *pf = get_process_file (fd);
    if (!pf)
    {
      lock_release(&file_lock);
      return -1;
    }
    int bytes = file_read(pf->file, buffer, size);
    lock_release(&file_lock);
    return bytes;
  }
}

int 
filesize (int fd)
{
  lock_acquire (&file_lock);
  struct process_file *pf = get_process_file (fd);
  if (!pf)
  {
    lock_release(&file_lock);
    return -1;
  }
  int filesize = file_length(pf->file);
  lock_release (&file_lock);
  return filesize;
}

void
close (int fd)
{
  if (!lock_held_by_current_thread (&file_lock))
    lock_acquire (&file_lock);
  struct process_file *pf = get_process_file (fd);
  if (!pf)
  {
    lock_release(&file_lock);
    return;
  }
  file_close(pf->file);
  list_remove(&pf->elem);
  free(pf);
  lock_release(&file_lock);
  return;
}

int
open (const char *file)
{
  lock_acquire (&file_lock);
  struct file *open_file = filesys_open(file);
  if (!open_file)
  {
    lock_release (&file_lock);
    return -1;
  }
  struct process_file *pf = malloc(sizeof(struct process_file));
  if (pf == NULL)
  {
    free(open_file);
    lock_release (&file_lock);
    return -1;
  }
  pf->file = open_file;
  pf->fd = thread_current ()->fd_inc++;
  list_push_front (&thread_current()->open_files, &pf->elem);
  lock_release (&file_lock);
  return pf->fd;
}

bool
remove (const char *file)
{
  lock_acquire (&file_lock);
  bool successful = filesys_remove(file);
  lock_release (&file_lock);
  return successful;
}

bool
create (const char *file, unsigned initial_size) 
{
  lock_acquire (&file_lock);
  bool successful = filesys_create(file, initial_size);
  lock_release (&file_lock);
  return successful;
}

int
write (int fd, void *buffer, unsigned size)
{
  if (fd == 1) // Writing to the console
  {  
    unsigned bytes_written = 0;

    while (size > 0) 
    {
      unsigned chunk_size = size < 256 ? size : 256;
      putbuf ((const char *)buffer + bytes_written, chunk_size);
      bytes_written += chunk_size;
      size -= chunk_size;
    }
    return bytes_written;
  }
  else 
  {
    lock_acquire (&file_lock);  
    struct process_file *pf = get_process_file (fd);
    if (!pf)
    {
      lock_release(&file_lock);
      return -1;
    }
    int bytes = file_write(pf->file, buffer, size);
    lock_release (&file_lock);
    return bytes;
  }
}

void
exit (int status)
{
  struct thread *cur = thread_current();
  printf("%s: exit(%d)\n", cur->name, status);
  cur->cp->exit_status = status;
  sema_up (&cur->cp->waiting_sema);
  thread_exit ();
}

void 
halt (void)
{
  shutdown_power_off ();
}

tid_t 
exec (const char *cmd_line) 
{
  tid_t tid = process_execute(cmd_line);
  if (!tid)
    return -1;
  struct child_process *child = get_child_process (tid);
  if (!child) 
    return -1;
  // Block while the process is still loading
  if (child->load_status == LOADING) 
  {
    sema_down (&child->loading_sema);
  }
  enum userprog_loading_status status = child->load_status;
  if (status == LOAD_SUCCESS)
  {
    return tid;
  }
  else 
    return -1;
}

/*
 Utilities / Helpers
*/

void
validate_user_string (char *str) 
{
  int i=0;
  while (get_user ((uint8_t *) str + i) != '\0') 
  {
    validate_user_address ((uint8_t *) str + i);
    i++;
  }
}

void
validate_user_address (uint8_t * addr)
{
  if (is_kernel_vaddr (addr) || get_user (addr) == -1) 
    exit (-1);
}

void 
extract_arguments (struct intr_frame *f, int *buf, int count) 
{
  uint32_t *user_ptr = (uint32_t *)f->esp + 1;

  // Validate each address
  for (int i = 0; i < count; i++) 
  {
    uint32_t *current_addr = user_ptr + i;
    validate_user_address ((void *)current_addr);
    buf[i] = *user_ptr;
    user_ptr++;
  }
}

struct process_file*
get_process_file (int fd)
{
  struct thread *t = thread_current();
  struct list_elem* next;
  
  for (struct list_elem* e = list_begin(&t->open_files); e != list_end(&t->open_files); e = next)
  {
    next = list_next(e);
    struct process_file *pf = list_entry(e, struct process_file, elem);
    if (fd == pf->fd)
    {
      return pf;
    }
  }
  return NULL;
}