#include "ecs.h"

#include "heap.h"
#include <stdint.h>

enum {
	k_max_component_types = 64,
	k_max_entities = 512,
};

typedef struct ecs_t {
	heap_t* heap;
	int global_sequence;
	void* components[k_max_component_types];
	int sequences[k_max_entities];
} ecs_t;

ecs_t* ecs_create(heap_t* heap) {
	ecs_t* ecs = heap_alloc(heap, sizeof(heap), 8);
	ecs->heap = heap;
	ecs->global_sequence = 0;
	for (int x = 0; x < _countof(ecs->sequences); x++) {
		ecs->sequences[x] = 0xffff;
	}
	return ecs;
}

void ecs_destroy(ecs_t* ecs) {
	heap_free(ecs->heap, ecs);
}

int ecs_entity_add(ecs_t* ecs, ecs_component_t* component, int component_count) {
	return 0xffff;
}

void ecs_entity_remove(ecs_t* ecs, int entity) {

}

int ecs_register_component_type(ecs_t* ecs, ecs_component_t* component, int size_per_component) {
	for (int x = 0; x < _countof(ecs->components); x++) {
		if (ecs->components[x] == NULL) {
			heap_alloc(ecs->heap, size_per_component * k_max_entities, 8);
			return x;
		}
	}
}

void ecs_register_event_type(ecs_component_t* component) {

}
