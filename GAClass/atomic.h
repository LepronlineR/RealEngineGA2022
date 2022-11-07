//#ifndef __ATOMIC_H__
//#define __ATOMIC_H__
#pragma once

// Increment a number atomically.
// Returns the old value of the number.
// Performs the following operation atomically:
int atomic_increment(int* address);

// Decrement a number atomically.
// Returns the old value of the number.
// Performs the following operation atomically:
int atomic_decrement(int* address);

// Compare two numbers atomically and assign if equal.
// Returns the old value of the number.
// Performs the following operation atomically:
int atomic_compare_and_exchange(int* dest, int compare, int exchange);

// Reads an integer from an address.
// All writes that occurred before the last atomic_store to this address are flushed.
int atomic_load(int* address);

// Writes an integer.
// Paired with an atomic_load, can guarantee ordering and visibility.
void atomic_store(int* address, int value);

//#endif