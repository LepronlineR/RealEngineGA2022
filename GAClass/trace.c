#include "trace.h"
#include "heap.h"
#include "timer.h"
#include "timer_object.h"
#include "queue.h"

#include <stddef.h>
#include <stdbool.h>

typedef struct trace_t {
	bool started;
	const char* path;
	int max_duration;
	heap_t* heap;
	timer_object_t* root_time;
	trace_object_t* trace_obj;
	queue_t* trace_obj_queue;
} trace_t;

typedef struct trace_object_t {
	const char* name;
	int pid;
	const char* tid;
	int ts;
	char event_type;
	trace_object_t* next;
} trace_object_t;

trace_t* trace_create(heap_t* heap, int event_capacity) {
	trace_t* trace = heap_alloc(heap, sizeof(trace_t), 8);
	trace->heap = heap;
	trace->started = false;
	trace->max_duration = event_capacity;
	trace->trace_obj = NULL;
	trace->trace_obj_queue = queue_create(heap, 256);
}

void trace_destroy(trace_t* trace) {

}

void trace_duration_push(trace_t* trace, const char* name) {
	if (trace == NULL || !trace->started) // trace has not started or null
		return;
	// create a new trace object
	trace_object_t* object = heap_alloc(trace->heap, sizeof(trace_object_t), 8);
	object->name = name;
	object->pid = getpid();
	object->event_type = 'B';
	object->ts = timer_object_get_ms(trace->root_time);
	
	// push trace into the queue
	queue_push(trace->trace_obj_queue, object);

	// add trace object to the end of trace
	trace_object_t* last_object = trace->trace_obj;
	if (last_object == NULL) { // no trace objects are in trace_obj yet
		trace->trace_obj = object;
	} else {
		while (last_object->next != NULL) { // loop through the obj to find the last trace object
			last_object = last_object->next;
		}
		last_object->next = object;
	}
}

void trace_duration_pop(trace_t* trace) {
	if (trace == NULL || !trace->started) // trace has not started
		return;
}

void trace_capture_start(trace_t* trace, const char* path) {
	trace->started = true;
	trace->path = path;
	trace->root_time = timer_object_create(trace->heap, NULL);
}

void trace_capture_stop(trace_t* trace) {
	// stops recording the trace events, begin writing the trace events into the JSON file

}
