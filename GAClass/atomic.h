#ifndef __ATOMIC_H__
#define __ATOMIC_H__

// Increment a number atomically.
// Returns the old value of the number.
// Performs the following operation atomically:
int atomic_inc(int* address);

// Decrement a number atomically.
// Returns the old value of the number.
// Performs the following operation atomically:
int atomic_dec(int* address);

// Compare two numbers atomically and assign if equal.
// Returns the old value of the number.
// Performs the following operation atomically:
int atomic_compare_and_exchange(int* dest, int compare, int exchange);

#endif