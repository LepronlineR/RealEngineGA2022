#pragma once

typedef struct trace_t trace_t;

void homework3_slower_function(trace_t* trace);
void homework3_slow_function(trace_t* trace);
int homework3_test_func(void* data);
void homework3_test();

// for testing

void test_function_2(trace_t* trace);
void test_function_3(trace_t* trace);
void test_function_4(trace_t* trace);
void test_function_5(trace_t* trace);
int trace_test_func(void* data);
void trace_test();