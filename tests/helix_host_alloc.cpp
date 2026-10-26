#include <cstdlib>
unsigned helixHostAllocations=0;
extern "C" void* helix_malloc(int n){++helixHostAllocations;return malloc(n);}
extern "C" void helix_free(void* p){free(p);}
