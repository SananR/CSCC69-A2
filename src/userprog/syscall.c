#include "userprog/syscall.h"
#include <stdio.h>
#include "devices/shutdown.h"
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "../syscall-nr.h"

#define USER_PROCESS_MAXIMUM_ARGUMENTS 5


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
 
/* Writes BYTE to user address UDST.
   UDST must be below PHYS_BASE.
   Returns true if successful, false if a segfault occurred. */
static bool
put_user (uint8_t *udst, uint8_t byte)
{
  int error_code;
  asm ("movl $1f, %0; movb %b2, %1; 1:"
       : "=&a" (error_code), "=m" (*udst) : "q" (byte));
  return error_code != -1;
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
      validate_user_address ((void *) args[1]);
      f->eax = write (args[0], (void *)args[1], args[2]);
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
      //TODO VALIDATE STRING
      f->eax = exec ((char *)args[0]);
      break;
  }
}

/*
  System call handlers
*/

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
    // TODO
  }
  return 0;
}

void
exit (int status)
{
  struct thread *cur = thread_current();
  printf("%s: exit(%d)\n", cur->name, status);
  cur->exit_status = status;
  sema_up (&cur->waiting_sema);
  //f->eax = status;
  thread_exit ();
}

void 
halt (void)
{
  shutdown_power_off ();
}

tid_t 
exec (const char *cmd_line) {
  tid_t tid = process_execute(cmd_line);
  struct thread *child = get_child_thread (tid);
  if (!child) 
    return -1;
  // Block while the process is still loading
  if (child->load_status == LOADING) 
    sema_down (&child->loading_sema);
  // If loading fails, we return -1
  else if (child->load_status == LOAD_FAILED)
  {
    list_remove (&child->child_elem);
    return -1;
  }
  return tid;
}

/*
 Utilities / Helpers
*/

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
  for (int i = 0; i < count; i++) {
    uint32_t *current_addr = user_ptr + i;
    validate_user_address ((void *)current_addr);
    buf[i] = *user_ptr;
    user_ptr++;
  }
}
