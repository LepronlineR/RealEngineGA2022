#ifndef __THREAD_H__
#define __THREAD_H__

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

typedef struct thread_t thread_t;

// Creates a new thread.
// Thread begins running function with data on return.
thread_t* thread_create(int (*function)(void*), void* data);

// Waits for a thread to complete and destroys it.
// Returns the thread's exit code.
int thread_destroy(thread_t* thread);

#endif