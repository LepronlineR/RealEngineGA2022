#include <stdio.h>	
#include <stdarg.h>
#include <stdint.h>
#include <DbgHelp.h>
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

typedef enum debug_print_t {
	k_print_info = 1 << 0,
	k_print_warning = 1 << 1,
	k_print_error = 1 << 2
} debug_print_t;

static LONG debug_exception_handler(LPEXCEPTION_POINTERS POINTER);
void debug_install_exception_handler();
void debug_set_print_mask(uint32_t mask);
void debug_print_line(uint32_t type, _Printf_format_string_ const char* format, ...);