#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);

/* System call handlers */
void exit (int status);
int write (int fd, void *buffer, unsigned size);
void halt (void);

#endif /* userprog/syscall.h */
