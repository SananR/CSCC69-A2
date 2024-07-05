#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "threads/thread.h"

void syscall_init (void);

/* System call handlers */
void exit (int status);
int write (int fd, void *buffer, unsigned size);
void halt (void);
tid_t exec (const char *cmd_line);

#endif /* userprog/syscall.h */
