#include "heap.h"

#include "debug.h"
#include "mutex.h"
#include "include/tlsf/tlsf.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#define MAIN_STRING_NAME "main"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>

typedef struct arena_t {
	pool_t pool;
	struct arena_t* next;
} arena_t;

typedef struct alloc_node_t {
	void* address;
	// tracking the stack backtrace and memory size
	char** backtrace;
	unsigned short frames;
	size_t memory_size;
	// the list
	struct alloc_node_t* next;
	struct alloc_node_t* prev;
} alloc_node_t;

typedef struct allocation_list_t {
	struct alloc_node_t* tail;
	struct alloc_node_t* head;
	int size;
} allocation_list_t;

typedef struct heap_t {
	tlsf_t tlsf;
	size_t grow_increment;
	arena_t* arena;
	// allocation_list_t* allocation;
	mutex_t* mutex;
} heap_t;

allocation_list_t* initialize_allocation_list() {
	allocation_list_t* list = VirtualAlloc(NULL, sizeof(allocation_list_t) + tlsf_size(),
		MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	list->tail = NULL;
	list->head = NULL;
	list->size = 0;
	return list;
}

void insert_to_list(void* address, size_t memory_size, unsigned short frames, char** backtrace, allocation_list_t* list) {
	
	if (list == NULL) {
		debug_print_line(k_print_error, "Called an insert to a list that is NULL\n");
		return;
	}
	
	alloc_node_t* n = VirtualAlloc(NULL, sizeof(alloc_node_t) + tlsf_size(),
		MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	n->next = NULL;
	n->prev = NULL;
	n->frames = frames;
	n->address = address;
	n->backtrace = backtrace;
	n->memory_size = memory_size;

	if (list->head == NULL) {
		list->head = n;
	} else { // send it to the back
		alloc_node_t* tail = list->tail;
		if (tail == NULL) {
			list->tail = n;
		} else {
			n->prev = tail;
			tail->next = n;
			list->tail = n;
		}
	}
	list->size += 1;
}

// remove from list given the address to the pointer
void remove_from_list(allocation_list_t* list, void* address) {
	if (list->size <= 0) { // cannot remove from empty list
		debug_print_line(k_print_error, "The allocation list is empty when removing an address\n");
		return;
	}

	alloc_node_t* node = list->head;
	while (node != NULL) { // if this node is not the current address go to next address
		if (node->address == address) { // address has been found and trying to remove from the list
			if (node->prev == NULL && node->next != NULL) { // case that it is the first of the list
				node->next->prev = NULL;
				list->head = node->next;
			} else if (node->next == NULL && node->prev != NULL) { // case that it is in the last of the list
				node->prev->next = NULL;
			} else if (node->next != NULL && node->prev != NULL) { // default case (in the middle of the list)
				node->prev->next = node->next;
				node->next->prev = node->prev;
			}
			// case that there is one node ignores the rest of the cases
			free_node(node);
			list->size -= 1;
			break;
		}
		node = node->next;
	}
}

void free_node(alloc_node_t* node) {
	if (node == NULL) {
		debug_print_line(k_print_error, "Attempting to free a node that is NULL\n");
		return;
	}
	node->next = NULL;
	node->prev = NULL;
	for (unsigned int x = 0; x < node->frames; x++) {
		VirtualFree(node->backtrace[x], 0, MEM_RELEASE);
	}
	VirtualFree(node->backtrace, 0, MEM_RELEASE);
	node->address = NULL;
	VirtualFree(node, 0, MEM_RELEASE);
}

heap_t* heap_create(size_t grow_increment) {
	heap_t* heap = VirtualAlloc(NULL, sizeof(heap_t) + tlsf_size(),
		MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (!heap) { // system out of memory
		debug_print_line(k_print_error, "System is out of memory");
		return NULL;
	}
	//heap->allocation = initialize_allocation_list();
	heap->grow_increment = grow_increment;
	heap->tlsf = tlsf_create(heap + 1);
	heap->arena = NULL;
	heap->mutex = mutex_create();
	return heap;
}

void* heap_alloc(heap_t* heap, size_t size, size_t alignment) {

	mutex_lock(heap->mutex);

	size_t size_plus_callstack = size + 64;

	void* address = tlsf_memalign(heap->tlsf, alignment, size_plus_callstack);
	if (!address) { // memory has not been allocated yet
		// create more virtual memory to store the arena
		size_t arena_size = __max(heap->grow_increment, size * 2) + sizeof(arena_t);
		arena_t* arena = VirtualAlloc(NULL, arena_size + tlsf_pool_overhead(),
			MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		if (!heap) { // system out of memory
			debug_print_line(k_print_error, "System is out of memory");
			return NULL;
		}
		// assign the current arena and set the next arena
		arena->pool = tlsf_add_pool(heap->tlsf, arena+1, arena_size);
		arena->next = heap->arena;
		heap->arena = arena;
		address = tlsf_memalign(heap->tlsf, alignment, size_plus_callstack);
	}

	
	if(address){
		char* callstack = (char*) address + size;
		CaptureStackBackTrace(1, 8, callstack, NULL);
	}

	mutex_unlock(heap->mutex);
	
	return address;
}

void heap_free(heap_t* heap, void* address) {
	mutex_lock(heap->mutex);
	//remove_from_list(heap->allocation, address);
	tlsf_free(heap->tlsf, address);
	mutex_unlock(heap->mutex);
}

static void leak_check(void* ptr, size_t size, int used, void* usert) {
	if (used) {
		void* callstack = (char*)ptr + (size - 64);
		// symbolicate callstack
		HANDLE process = GetCurrentProcess();
		SymInitialize(process, NULL, TRUE);
		
		// print
		debug_print_line(k_print_warning, "%s\n", (char*) callstack);

		SymCleanup(process);
	}
}

void heap_destroy(heap_t* heap) {
	// Free the mutex
	mutex_destroy(heap->mutex);
	// Free the tlsf
	tlsf_destroy(heap->tlsf);
	// Free the arena
	arena_t* arena = heap->arena;
	while (arena) {
		tlsf_walk_pool(arena->pool, leak_check, NULL);
		arena_t* next = arena->next;
		VirtualFree(arena, 0, MEM_RELEASE);
		arena = next;
	}
	VirtualFree(heap, 0, MEM_RELEASE);
}