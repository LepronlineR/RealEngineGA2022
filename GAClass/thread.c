#include "thread.h"
#include "debug.h"

thread_t* thread_create(int (*function)(void*), void* data) {
	HANDLE h = CreateThread(NULL, 0, function, data, CREATE_SUSPENDED, NULL);
	if (h == INVALID_HANDLE_VALUE) { // failed to create thread
		debug_print_line(k_print_warning, "Thread failed to create\n");
		return NULL;
	}
	ResumeThread(h);
	return (thread_t*)h;
}

int thread_destroy(thread_t* thread) {
	// Wait for thread to complete and then destroy the thread (blocking call)
	WaitForSingleObject(thread, INFINITE);
	int code = 0;
	GetExitCodeThread(thread, &code);
	CloseHandle(thread);
	return code;
}