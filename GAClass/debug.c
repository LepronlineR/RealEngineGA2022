#include "debug.h"

static uint32_t s_mask = 0xffffffff;

static LONG debug_exception_handler(LPEXCEPTION_POINTERS ExceptionInfo)
{
	debug_print_line(k_print_error, "Caught an exception!\n");
	HANDLE file = CreateFile(L"ga2022-crash.dmp", GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (file != INVALID_HANDLE_VALUE)
	{
		MINIDUMP_EXCEPTION_INFORMATION mini_exception = { 0 };
		mini_exception.ThreadId = GetCurrentThreadId();
		mini_exception.ExceptionPointers = ExceptionInfo;
		mini_exception.ClientPointers = FALSE;

		MiniDumpWriteDump(GetCurrentProcess(),
			GetCurrentProcessId(),
			file,
			MiniDumpWithThreadInfo,
			&mini_exception,
			NULL,
			NULL);

		CloseHandle(file);
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

}

void debug_backtrace(size_t memory_size, unsigned short frames, char** backtrace) {
	debug_print_line(k_print_warning,
		"Memory leak of size %zu bytes with callstack:\n", memory_size);
	for (int x = 0; x < frames; x++) {
		debug_print_line(k_print_warning,
			"[%d] %s\n", x, backtrace[x]);
	}
}
