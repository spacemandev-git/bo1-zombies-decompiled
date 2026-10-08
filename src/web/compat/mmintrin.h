// mmintrin.h - the MMX intrinsics the engine uses (gfx_d3d/r_model_skin_sse.cpp, r_model_skin.cpp), emulated on
// Emscripten's __m64 (an 8-byte vector from its <xmmintrin.h>). wasm has no MMX; these are scalar and exact.
#pragma once
#ifndef BO1_WEB_MMINTRIN_H
#define BO1_WEB_MMINTRIN_H
#include <xmmintrin.h>
#include <stdint.h>
#include <string.h>

typedef union bo1_m64 { __m64 m; uint8_t ub[8]; int8_t sb[8]; uint16_t uw[4]; int16_t sw[4]; int32_t sd[2]; uint64_t q; } bo1_m64;

static __inline__ __m64 __attribute__((__always_inline__)) bo1_m64_out(bo1_m64 v) { return v.m; }
static __inline__ bo1_m64 __attribute__((__always_inline__)) bo1_m64_in(__m64 m) { bo1_m64 v; v.m = m; return v; }

static __inline__ void __attribute__((__always_inline__)) _mm_empty(void) {}
#define _m_empty _mm_empty
static __inline__ __m64 __attribute__((__always_inline__)) _mm_setzero_si64(void) { bo1_m64 v; v.q = 0; return v.m; }
static __inline__ __m64 __attribute__((__always_inline__)) _mm_cvtsi32_si64(int i) { bo1_m64 v; v.q = 0; v.sd[0] = i; return v.m; }
static __inline__ int __attribute__((__always_inline__)) _mm_cvtsi64_si32(__m64 m) { return bo1_m64_in(m).sd[0]; }
#define _m_from_int _mm_cvtsi32_si64
#define _m_to_int _mm_cvtsi64_si32
static __inline__ __m64 __attribute__((__always_inline__)) _mm_unpacklo_pi8(__m64 a, __m64 b)
{
    bo1_m64 x = bo1_m64_in(a), y = bo1_m64_in(b), r;
    for (int i = 0; i < 4; ++i) { r.ub[2 * i] = x.ub[i]; r.ub[2 * i + 1] = y.ub[i]; }
    return r.m;
}
#define _m_punpcklbw _mm_unpacklo_pi8
static __inline__ __m64 __attribute__((__always_inline__)) _mm_unpacklo_pi16(__m64 a, __m64 b)
{
    bo1_m64 x = bo1_m64_in(a), y = bo1_m64_in(b), r;
    r.uw[0] = x.uw[0]; r.uw[1] = y.uw[0]; r.uw[2] = x.uw[1]; r.uw[3] = y.uw[1];
    return r.m;
}
#define _m_punpcklwd _mm_unpacklo_pi16
static __inline__ __m64 __attribute__((__always_inline__)) _mm_packs_pu16(__m64 a, __m64 b)
{
    bo1_m64 x = bo1_m64_in(a), y = bo1_m64_in(b), r;
    for (int i = 0; i < 4; ++i) {
        int16_t s = x.sw[i];
        r.ub[i] = (uint8_t)(s < 0 ? 0 : s > 255 ? 255 : s);
        s = y.sw[i];
        r.ub[4 + i] = (uint8_t)(s < 0 ? 0 : s > 255 ? 255 : s);
    }
    return r.m;
}
#define _m_packuswb _mm_packs_pu16
// float conversions (xmmintrin's MMX-dependent ones)
static __inline__ __m128 __attribute__((__always_inline__)) _mm_cvtpu16_ps(__m64 a)
{
    bo1_m64 x = bo1_m64_in(a);
    return _mm_set_ps((float)x.uw[3], (float)x.uw[2], (float)x.uw[1], (float)x.uw[0]);
}
static __inline__ __m128 __attribute__((__always_inline__)) _mm_cvtpi16_ps(__m64 a)
{
    bo1_m64 x = bo1_m64_in(a);
    return _mm_set_ps((float)x.sw[3], (float)x.sw[2], (float)x.sw[1], (float)x.sw[0]);
}
static __inline__ __m128 __attribute__((__always_inline__)) _mm_cvtpu8_ps(__m64 a)
{
    bo1_m64 x = bo1_m64_in(a);
    return _mm_set_ps((float)x.ub[3], (float)x.ub[2], (float)x.ub[1], (float)x.ub[0]);
}
// cvtps2pi: round to nearest even (the default MXCSR mode), out of range -> 0x80000000
static __inline__ int32_t __attribute__((__always_inline__)) bo1_cvt_f2i(float f)
{
    if (!(f >= -2147483648.0f && f < 2147483648.0f)) return (int32_t)0x80000000;
    return (int32_t)__builtin_rintf(f);
}
static __inline__ __m64 __attribute__((__always_inline__)) _mm_cvtps_pi32(__m128 a)
{
    float f[4];
    _mm_storeu_ps(f, a);
    bo1_m64 r;
    r.sd[0] = bo1_cvt_f2i(f[0]);
    r.sd[1] = bo1_cvt_f2i(f[1]);
    return r.m;
}
#define _mm_cvt_ps2pi _mm_cvtps_pi32
static __inline__ void __attribute__((__always_inline__)) _mm_stream_pi(__m64 *p, __m64 a) { memcpy(p, &a, 8); }
#endif
