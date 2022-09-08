#include <stddef.h>
#include <stdio.h>
#include "heap.h"
#include "tlsf.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>

typedef struct arena_t {
	pool_t pool;
	struct arena_t* next;
} arena_t;

typedef struct heap_t {
	tlsf_t tlsf;
	size_t grow_increment;
	size_t total_allocated_memory;
	arena_t* arena;
} heap_t;

// initialize the heap
heap_t* heap_create(size_t grow_increment) {
	heap_t* heap = VirtualAlloc(NULL, sizeof(heap_t) + tlsf_size(),
		MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (!heap) { // system out of memory
		printf("OOM\n");
		return NULL;
	}
	heap->grow_increment = grow_increment;
	heap->tlsf = tlsf_create(heap + 1);
	heap->arena = NULL;
	return heap;
}

void* heap_alloc(heap_t* heap, size_t size, size_t alignment) {
	void* address = tlsf_memalign(heap->tlsf, alignment, size);
	if (!address) { // memory has not been allocated yet
		// create more virtual memory to store the arena
		size_t arena_size = __max(heap->grow_increment, size * 2) + sizeof(arena_t);
		arena_t* arena = VirtualAlloc(NULL, arena_size + tlsf_pool_overhead(),
			MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		if (!heap) { // system out of memory
			printf("OOM\n");
			return NULL;
		}
		// assign the current arena and set the next arena
		arena->pool = tlsf_add_pool(heap->tlsf, arena+1, arena_size);
		arena->next = heap->arena;
		heap->arena = arena;
		address = tlsf_memalign(heap->tlsf, alignment, size);
	}
	// track the allocation count
	heap->total_allocated_memory += size;
	return address;
}

void heap_free(heap_t* heap, void* address) {
	tlsf_free(heap->tlsf, address);
}

void heap_destroy(heap_t* heap) {
	// tlsf_walk_pool and report any block still allocated
	
	// Free the arena
	arena_t* arena = heap->arena;
	while (arena) {
		arena_t* next = arena->next;
		VirtualFree(arena, 0, MEM_RELEASE);
		arena = next;
	}
	// Free the tlsf
	tlsf_destroy(heap->tlsf);
	VirtualFree(heap, 0, MEM_RELEASE);
}