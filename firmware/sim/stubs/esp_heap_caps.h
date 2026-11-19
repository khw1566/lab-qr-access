#pragma once
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 0
#define heap_caps_calloc(n, s, c) calloc(n, s)
