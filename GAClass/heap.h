#ifndef __HEAP_H__
#define __HEAP_H__

#include <stddef.h>
#include <stdio.h>

typedef struct arena_t arena_t;

typedef struct alloc_node_t alloc_node_t;

typedef struct allocation_list_t allocation_list_t;

typedef struct heap_t heap_t;

// ================== HEAP ==================

// Creates a new memory heap
// The grow increment is the default size with which the heap grows.
// Creates a memory allocation tracker in the heap
heap_t* heap_create(size_t grow_increment);

// Allocate memory from a heap.
void* heap_alloc(heap_t* heap, size_t size, size_t alignment);

// Free memory previously allocated from a heap

void heap_free(heap_t* heap, void* address);

// Destroy the given heap, checks for any memory leaks if possible
void heap_destroy(heap_t* heap);

// ================== MEMORY ALLOCATION TRACKING ==================

// The callstack of a memory allocated is stored into a allocation node
// inside of an allocation list (this list is a doubly linked list)
//		i.e.
//		(main, hw_1, hw_1_test1) -> (main, hw_1, hw_1_test2) -> ...
// once a memory is freed, the node associated with the allocated memory is removed
// if the heap is destroyed then the remaining nodes are leaked memory

// initialize a memory allocation tracker list
allocation_list_t* initialize_allocation_list();

// creates a node for the address the memory is allocated in, assigns
// the data for the node (backtrace, memory size and frame size) and 
// inserts the node into the list
void insert_to_list(void* address, size_t memory_size, unsigned short frames, 
	char** backtrace, allocation_list_t* list);

// given an allocation list, finds a node with the given address and removes
// it from the list
void remove_from_list(allocation_list_t* list, void* address);

// free and remove the memory allocated to the allocation node
void free_node(alloc_node_t* node);

#endif