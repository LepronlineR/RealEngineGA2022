#include "trace.h"

#include "heap.h"
#include "timer.h"
#include "timer_object.h"
#include "queue.h"
#include "debug.h"
#include "mutex.h"

#include <stddef.h>
#include <stdbool.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>

typedef struct trace_t {
	bool started;
	const char* path;
	int max_duration;
	heap_t* heap;
	// timer_object_t* root_time;
	trace_event_t* trace_event_head;
	queue_t* trace_event_queue;
	mutex_t* mutex;
} trace_t;

typedef struct trace_event_t {
	const char* name;
	int pid;
	DWORD tid;
	int ts;
	char event_type;
	trace_event_t* next;
} trace_event_t;

trace_t* trace_create(heap_t* heap, int event_capacity) {
	trace_t* trace = heap_alloc(heap, sizeof(trace_t), 8);
	trace->heap = heap;
	trace->started = false;
	trace->max_duration = event_capacity;
	trace->trace_event_head = NULL;
	trace->trace_event_queue = queue_create(heap, event_capacity);
	return trace;
}

void trace_destroy(trace_t* trace) {
	
}

void trace_duration_push(trace_t* trace, const char* name) {
	if (trace == NULL || !trace->started) // trace has not started or null
		return;
	// create a new trace event
	trace_event_t* trace_event = heap_alloc(trace->heap, sizeof(trace_event_t), 8);
	trace_event->name = name;
	trace_event->pid = getpid();
	trace_event->event_type = 'B';
	trace_event->tid = GetCurrentThreadId();
	// trace_event->ts = timer_object_get_ms(trace->root_time);
	trace_event->ts = timer_ticks_to_ms(timer_get_ticks());
	trace_event->next = NULL;

	mutex_lock(trace->mutex);
	
	// push trace into the queue
	queue_push(trace->trace_event_queue, trace_event);

	// add trace object to the end of trace
	trace_event_t* last_event = trace->trace_event_head;
	if (last_event == NULL) { // no trace objects are in trace_obj yet
		trace->trace_event_head = trace_event;
	} else {
		trace_event_t* event_tracker = trace->trace_event_head;
		while (event_tracker->next != NULL) { // loop through the obj to find the last trace object
			event_tracker = event_tracker->next;
		}
		event_tracker->next = trace_event; // add it to the end of the list
	}

	mutex_unlock(trace->mutex);
}

void trace_duration_pop(trace_t* trace) {
	if (trace == NULL || !trace->started) // trace has not started
		return;

	// pop first from the queue (active thread in that queue), copy it to a list to then parse into json
	trace_event_t* first_object = queue_pop(trace->trace_event_queue);
	trace_event_t* first_object_copy = heap_alloc(trace->heap, sizeof(trace_event_t), 8);

	first_object_copy->name = first_object->name;
	first_object_copy->pid = first_object->pid;
	first_object_copy->event_type = 'E';
	first_object_copy->tid = first_object->tid;
	//first_object_copy->ts = timer_object_get_ms(trace->root_time);
	first_object_copy->ts = timer_ticks_to_ms(timer_get_ticks());
	first_object_copy->next = NULL;

	mutex_lock(trace->mutex);

	// add trace object to the end of trace
	trace_event_t* last_event = trace->trace_event_head;
	if (last_event == NULL) { // no trace objects are in trace_obj yet
		trace->trace_event_head = first_object_copy;
	}
	else {
		trace_event_t* event_tracker = trace->trace_event_head;
		while (event_tracker->next != NULL) { // loop through the obj to find the last trace object
			event_tracker = event_tracker->next;
		}
		event_tracker->next = first_object_copy; // add it to the end of the list
	}

	mutex_unlock(trace->mutex);
}

void trace_capture_start(trace_t* trace, const char* path) {
	trace->started = true;
	trace->path = path;
	// trace->root_time = timer_object_create(trace->heap, NULL);
}

// stops recording the trace events, begin writing the trace events into the JSON file
void trace_capture_stop(trace_t* trace) {
	// clean most of the trace
	trace->started = false;
	// timer_object_destroy(trace->root_time);
	trace_event_t* trace_event = trace->trace_event_head;
	while (trace_event != NULL) {
		printf("name: %s, pid: %d, tid: %lu, ts:%d, type:%c\n", 
			trace_event->name, trace_event->pid, trace_event->tid, trace_event->ts, trace_event->event_type);
		trace_event = trace_event->next;
	}
}
