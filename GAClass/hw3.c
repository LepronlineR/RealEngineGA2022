#include "hw3.h"

#include "thread.h"
#include "heap.h"

void homework3_slower_function(trace_t* trace) {
	trace_duration_push(trace, "homework3_slower_function");
	thread_sleep(200);
	trace_duration_pop(trace);
}

void homework3_slow_function(trace_t* trace) {
	trace_duration_push(trace, "homework3_slow_function");
	thread_sleep(100);
	homework3_slower_function(trace);
	trace_duration_pop(trace);
}

int homework3_test_func(void* data) {
	trace_t* trace = data;
	homework3_slow_function(trace);
	return 0;
}

void homework3_test() {
	heap_t* heap = heap_create(4096);

	// Create the tracing system with at least space for 100 *captured* events.
	// Each call to trace_duration_push is an event.
	// Each call to trace_duration_pop is an event.
	// Before trace_capture_start is called, and after trace_capture_stop is called,
	// duration events should not be generated.
	trace_t* trace = trace_create(heap, 100);

	// Capturing has *not* started so these calls can safely be ignored.
	//trace_duration_push(trace, "should be ignored");
	//trace_duration_pop(trace);

	// Start capturing events.
	// Eventually we will want to write events to a file -- "trace.json".
	// However we should *not* write to the file for each call to trace_duration_push or
	// trace_duration_pop. That would be much too slow. Instead, events should be buffered
	// (up to event_capacity) before writing to a file. For purposes of this homework,
	// it is entirely fine if you only capture the first event_capacity count events and
	// ignore any additional events.
	trace_capture_start(trace, "trace.json");

	// Create a thread that will push/pop duration events.
	thread_t* thread = thread_create(homework3_test_func, trace);

	// Call a function that will push/pop duration events.
	homework3_slow_function(trace);

	// Wait for thread to finish.
	thread_destroy(thread);

	// Finish capturing. Write the trace.json file in Chrome tracing format.
	trace_capture_stop(trace);

	trace_destroy(trace);

	heap_destroy(heap);
}
