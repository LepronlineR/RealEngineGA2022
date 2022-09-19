#ifndef __DEBUG_H__
#define __DEBUG_H__

#include <stdio.h>	
#include <stdarg.h>
#include <stdint.h>
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <DbgHelp.h>

typedef enum debug_print_t {
	k_print_info = 1 << 0,
	k_print_warning = 1 << 1,
	k_print_error = 1 << 2
} debug_print_t;

// Flags for debug_print().
static LONG debug_exception_handler(LPEXCEPTION_POINTERS POINTER);

// Install unhandled exception handler.
// When unhandled exceptions are caught, will log an error and capture a memory dump.
void debug_install_exception_handler();

// Set mask of which types of prints will actually fire.
// See the debug_print().
void debug_set_print_mask(uint32_t mask);

// Log a message to the console.
// Message may be dropped if type is not in the active mask.
// See debug_set_print_mask.
void debug_print_line(uint32_t type, _Printf_format_string_ const char* format, ...);

// Given an allocation node with a stack backtrace, prints out the memory leak 
// from the memory given
void debug_backtrace(alloc_node_t* allocation_node);

#endif