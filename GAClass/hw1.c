#include "hw1.h"

void* homework1_allocate_1(heap_t* heap){
	return heap_alloc(heap, 16 * 1024, 8);
}

void* homework1_allocate_2(heap_t* heap){
	return heap_alloc(heap, 256, 8);
}

void* homework1_allocate_3(heap_t* heap){
	return heap_alloc(heap, 32 * 1024, 8);
}

void homework1_test(){
	heap_t* heap = heap_create(4096);
	void* block1 = homework1_allocate_1(heap);
	/*leaked*/ homework1_allocate_2(heap);
	/*leaked*/ homework1_allocate_3(heap);
	heap_free(heap, block1);
	heap_destroy(heap);
}