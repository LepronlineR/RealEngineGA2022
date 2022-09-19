#include "heap.h"

#define MAIN_STRING_NAME "main"

allocation_list_t* initialize_allocation_list() {
	allocation_list_t* list = VirtualAlloc(NULL, sizeof(allocation_list_t),
		MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	list->tail = NULL;
	list->head = NULL;
	list->size = 0;
	return list;
}

void insert_to_list(void* address, size_t memory_size, unsigned short frames, char** backtrace, allocation_list_t* list) {
	alloc_node_t* n = VirtualAlloc(NULL, sizeof(alloc_node_t),
		MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	n->next = NULL;
	n->prev = NULL;
	n->frames = frames;
	n->address = address;
	n->backtrace = backtrace;
	n->memory_size = memory_size;
	if (list->size == 0) {  // (N)
		list->head = n;     //      <-     <-
	} else {                // <- X -> (N) -> Z ->
		alloc_node_t* tail = list->tail;
		n->next = tail->next;
		tail->next = n;
		n->prev = tail;
		if (n->next != NULL) {
			n->next->prev = n;
		}
	}
	list->tail = n;
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
	heap->allocation = initialize_allocation_list();
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
			debug_print_line(k_print_error, "System is out of memory");
			return NULL;
		}
		// assign the current arena and set the next arena
		arena->pool = tlsf_add_pool(heap->tlsf, arena+1, arena_size);
		arena->next = heap->arena;
		heap->arena = arena;
		address = tlsf_memalign(heap->tlsf, alignment, size);
	}

	// Records the back trace and stores it into the allocation node
	unsigned short frames = 0;
	void* stack[128];
	HANDLE process = GetCurrentProcess();
	SymInitialize(process, NULL, TRUE);
	frames = CaptureStackBackTrace(1, 128, stack, NULL);

	char** backtrace = VirtualAlloc(NULL, frames * sizeof(char*),
		MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

	char buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)];
	SYMBOL_INFO* symbol = (SYMBOL_INFO*) buffer;	
	symbol->MaxNameLen = 255;
	symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
	if (symbol) {
		for (unsigned int x = 0; x < frames; x++) {
			SymFromAddr(process, (DWORD64)(stack[x]), 0, symbol);
			backtrace[x] = VirtualAlloc(NULL, 128 * sizeof(char),
				MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
			strcpy_s(backtrace[x], 128, symbol->Name);
			// manually check for main in order to stop the call stack
			if (strcmp(MAIN_STRING_NAME, symbol->Name) == 0) {
				frames = x + 1;
				break;
			}
		}
	}
	SymCleanup(process);

	insert_to_list(address, size, frames, backtrace, heap->allocation);
	
	return address;
}

void heap_free(heap_t* heap, void* address) {
	remove_from_list(heap->allocation, address);
	tlsf_free(heap->tlsf, address);
}

void heap_destroy(heap_t* heap) {
	// Check for any leaked memory
	if (heap->allocation->head != NULL && heap->allocation->size > 0) {
		alloc_node_t* node = heap->allocation->head;
		alloc_node_t* save_node = NULL;
		for (int x = 0; x < heap->allocation->size; x++) {
			debug_backtrace(node);
			save_node = node->next;
			VirtualFree(node, 0, MEM_RELEASE);
			node = save_node;
		}
	}
	// Free the tlsf
	tlsf_destroy(heap->tlsf);
	// Free the arena
	arena_t* arena = heap->arena;
	while (arena) {
		arena_t* next = arena->next;
		VirtualFree(arena, 0, MEM_RELEASE);
		arena = next;
	}
	VirtualFree(heap, 0, MEM_RELEASE);
}