#include "random.h"


int random_i(int low, int max) {
	return (rand() % (max - low + 1)) + low;
}

float random_f(float low, float max) {
	//return (float) (rand() % (max - low + 1)) + low;
	return 0.0f;
}
