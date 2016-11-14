#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include <stdbool.h>

void syscall_init (void);
void exit (int);
inline bool is_valid_usrptr (const void*);

#endif /* userprog/syscall.h */
