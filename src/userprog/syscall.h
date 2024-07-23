#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "threads/thread.h"

void syscall_init (void);

/* Lock used when interacting with the file system */
struct lock file_lock;

/* System call handlers */

int wait (tid_t tid);
void seek (int fd, unsigned position);
unsigned tell (int fd);
int filesize (int fd);
int read (int fd, void *buffer, unsigned size);
bool remove (const char *file);
int open (const char *file);
void close (int fd);
bool create (const char *file, unsigned initial_size);
void exit (int status);
int write (int fd, void *buffer, unsigned size);
void halt (void);
tid_t exec (const char *cmd_line);
mapid_t mmap (int fd, void *addr);

#endif /* userprog/syscall.h */
