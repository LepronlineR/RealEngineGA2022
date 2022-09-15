#include "debug.h"

static uint32_t s_mask = 0xffffffff;

static LONG debug_exception_handler(LPEXCEPTION_POINTERS POINTER) {
	debug_print_line(k_print_error, "Caught unhandled exception!");
	
	HANDLE file = CreateFile(L"GA2022-crash.dmp", );
	if (file != INVALID_HANDLE_VALUE) {
		MINDUMP_EXCEPTION_INFORMATION mini_exception = {0};
		mini_exception.ThreadId = GetCurrentThreadId();
		mini_exception.ExceptionPointers = ExceptionInfo;
		mini_exception.ClientPointers = FALSE;

		MiniDumpWriteDump(GetCurrentProcess(),
			GetCurrentProcessID(),
			file,
			MiniDumpWithTreadInfo,
			&mini_exception,
			NULL,
			NULL);

		CloseHandle();
	}
	
	return EXCEPTION_EXECUTE_HANDLER;
}

void debug_install_exception_handler() {
	//SetUnhandledExceptionFilter(debug_exception_handler);
	AddVectoredExceptionHandler(TRUE, debug_exception_handler);
}

void debug_set_print_mask(uint32_t mask) {
	s_mask = mask;

}

void debug_print_line(uint32_t type, _Printf_format_string_ const char* format, ...) {
	
	if ((s_mask & type) == 0) {
		return;
	}
	
	va_list args;
	va_start(args, format);
	char buffer[256];
	vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);

	OutputDebugStringA(buffer);

	DWORD bytes = (DWORD)strlen(buffer);
	DWORD written = 0;
	HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
	WriteConsoleA(out, buffer, bytes, &written, NULL);

	/*if (type & k_print_error) {

	}*/

}

int debug_backtrace(void** stack, int stack_capacity) {
	CaptureStackBackTrace(1, stack_capacity, stack, NULL);
}