#pragma once
// Host stand-in: the display code only logs these numbers.
#include <cstddef>
#include <cstdint>
#define MALLOC_CAP_INTERNAL (1u << 11)
#define MALLOC_CAP_DMA (1u << 3)
inline size_t heap_caps_get_free_size(uint32_t) { return 0; }
inline size_t heap_caps_get_largest_free_block(uint32_t) { return 0; }
