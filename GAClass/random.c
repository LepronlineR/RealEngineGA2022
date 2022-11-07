#include "random.h"


int random_i(int low, int max) {
	return (rand() % (max - low + 1)) + low;
}