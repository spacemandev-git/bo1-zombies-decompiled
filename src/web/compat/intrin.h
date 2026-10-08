// intrin.h - MSVC intrinsics. The intrinsics themselves (_Interlocked*, _BitScan*, __rdtsc, __debugbreak, ...) are in
// bo1_web_prelude.h (force-included); this header adds the SSE ones, which <intrin.h> also brings in on MSVC.
#pragma once
#include "bo1_web_prelude.h"
#include <xmmintrin.h>
#include <emmintrin.h>
