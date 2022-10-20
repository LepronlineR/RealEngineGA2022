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

/* A trace, defines a trace structure that can be use to trace processes

A trace contains a path to the file, a heap, a definition for when the trace process begins, and a mutex
- the first trace event element to add every recorded trace event
- a trace event queue to push and pop elements
*/
typedef struct trace_t {
	bool started;
	const char* path;
	heap_t* heap;
	trace_event_t* trace_event_head;
	queue_t* trace_event_queue;
	mutex_t* mutex;
} trace_t;

/* A trace event, in a single list format :

A trace event contains:
- name of function
- process ID
- thread ID
- time
- event type (supports B: Begin and E: End for now)
- next trace event
*/
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
	trace->trace_event_head = NULL;
	trace->trace_event_queue = queue_create(heap, event_capacity);
	trace->mutex = mutex_create();
	return trace;
}

void trace_destroy(trace_t* trace) {
	queue_destroy(trace->trace_event_queue);
	mutex_destroy(trace->mutex);
	heap_free(trace->heap, trace);
}

void trace_duration_push(trace_t* trace, const char* name) {
	if (trace == NULL || !trace->started) // trace has not started or null
		return;

	mutex_lock(trace->mutex);

	// create a new trace event that begins with the current name
	trace_event_t* trace_event = heap_alloc(trace->heap, sizeof(trace_event_t), 8);
	trace_event->name = name;
	trace_event->pid = getpid();
	trace_event->event_type = 'B';
	trace_event->tid = GetCurrentThreadId();
	trace_event->ts = timer_ticks_to_ms(timer_get_ticks());
	trace_event->next = NULL;
	
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

	mutex_lock(trace->mutex);

	// pop first from the queue (active thread in that queue), 
	// copy it to a list to then parse into json later
	trace_event_t* first_event = queue_pop(trace->trace_event_queue);
	trace_event_t* first_event_copy = heap_alloc(trace->heap, sizeof(trace_event_t), 8);

	first_event_copy->name = first_event->name;
	first_event_copy->pid = first_event->pid;
	first_event_copy->event_type = 'E';
	first_event_copy->tid = first_event->tid;
	first_event_copy->ts = timer_ticks_to_ms(timer_get_ticks());
	first_event_copy->next = NULL;

	// add trace object to the end of trace
	trace_event_t* last_event = trace->trace_event_head;
	if (last_event == NULL) { // no trace objects are in trace_obj yet
		trace->trace_event_head = first_event_copy;
	}
	else {
		trace_event_t* event_tracker = trace->trace_event_head;
		while (event_tracker->next != NULL) { // loop through the obj to find the last trace object
			event_tracker = event_tracker->next;
		}
		event_tracker->next = first_event_copy; // add it to the end of the list
	}

	mutex_unlock(trace->mutex);
}

void trace_capture_start(trace_t* trace, const char* path) {
	trace->started = true;
	trace->path = path;
}

// stops recording the trace events, begin writing the trace events into the JSON file
void trace_capture_stop(trace_t* trace) {

	mutex_lock(trace->mutex);

	// clean most of the trace
	trace->started = false;
	trace_event_t* trace_event = trace->trace_event_head;

	// =======================================================================================
	//									   WRITE TO JSON
	// =======================================================================================

	// write to JSON format for each saved trace event in the trace event list starting from head
	wchar_t wide_path[1024];
	if (MultiByteToWideChar(CP_UTF8, 0, trace->path, -1, wide_path, sizeof(wide_path)) <= 0) {
		debug_print_line(k_print_error, "In 'trace_capture_stop' creating wide_path is invalid.\n");
		return;
	}
	HANDLE handle = CreateFile(wide_path, GENERIC_WRITE, FILE_SHARE_WRITE, NULL,
		CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

	if (handle == INVALID_HANDLE_VALUE) {
		debug_print_line(k_print_error, "In 'trace_capture_stop' creating the handle is invalid.\n");
		return;
	}

	// write the first starting two lines to the file
	char start_of_json_lines[47] = "{\n\t\"displayTimeUnit\": \"ns\", \"traceEvents\": [\n";

	if (!WriteFile(handle, (LPVOID)start_of_json_lines, strlen(start_of_json_lines), NULL, NULL)) {
		debug_print_line(k_print_error, "In 'trace_capture_stop' unable to write to json file.\n");
		CloseHandle(handle);
		return;
	}

	while (trace_event != NULL) { // for every trace event write into the file
		char event_str[2048];
		if (trace_event->next != NULL) { 
			// events from start to last - 1
			snprintf(event_str, sizeof(event_str),
				"\t\t{\"name\":\"%s\",\"ph\":\"%c\",\"pid\":%d,\"tid\":\"%lu\",\"ts\":\"%d\"},\n",
				trace_event->name, trace_event->event_type, trace_event->pid, trace_event->tid, trace_event->ts);
		} else { 
			// last event remove the , from the event str
			snprintf(event_str, sizeof(event_str),
				"\t\t{\"name\":\"%s\",\"ph\":\"%c\",\"pid\":%d,\"tid\":\"%lu\",\"ts\":\"%d\"}\n",
				trace_event->name, trace_event->event_type, trace_event->pid, trace_event->tid, trace_event->ts);
		}

		if (!WriteFile(handle, (LPVOID)event_str, strlen(event_str), NULL, NULL)) {
			debug_print_line(k_print_error, "In 'trace_capture_stop' unable to write to json file.\n");
			CloseHandle(handle);
			return;
		}
		
		
		trace_event_t* temp = trace_event;
		trace_event = trace_event->next;
		heap_free(trace->heap, temp);
	}

	// write the last two lines
	char end_of_json_lines[6] = "\t]\n}";

	if (!WriteFile(handle, (LPVOID)end_of_json_lines, strlen(end_of_json_lines), NULL, NULL)) {
		debug_print_line(k_print_error, "In 'trace_capture_stop' unable to write to json file.\n");
		CloseHandle(handle);
		return;
	}

	mutex_unlock(trace->mutex);

	CloseHandle(handle);
	
}
