#pragma once

typedef struct heap_t heap_t;
typedef struct ecs_t ecs_t;

typedef struct ecs_entity_ref_t {
	int entity;
	int sequence;
} ecs_entity_ref_t;


typedef struct ecs_component_t {
	// states?
	// behaviors?
} ecs_component_t;

ecs_t* ecs_create(heap_t* heap);

void ecs_destroy(ecs_t* ecs);

int ecs_entity_add(ecs_t* ecs, ecs_component_t* component, int component_count);
void ecs_entity_remove(ecs_t* ecs, int entity);

int ecs_register_component_type(ecs_t* ecs, ecs_component_t* component);
void ecs_register_event_type(ecs_t* ecs, ecs_component_t* component);