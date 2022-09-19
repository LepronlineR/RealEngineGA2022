#include "atomic.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

int atomic_inc(int* address) {
	InterlockedIncrement(address);
}

int atomic_dec(int* address) {
	InterlockedIncrement(address);
}

int atomic_compare_and_exchange(int* dest, int compare, int exchange) {
	return InterlockedCompareExchange(dest, exchange, compare);
}