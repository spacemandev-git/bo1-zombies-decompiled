// bo1_web_prelude.h - force-included (-include) into every translation unit of the web build (cmake/web.cmake).
//
// Provides what MSVC gives the engine without an explicit include: the MSVC C runtime extensions (_stricmp,
// _snprintf, sprintf_s, _itoa, _aligned_malloc, _time64, ...) and the compiler intrinsics (__debugbreak, _Interlocked*,
// _BitScanReverse, __rdtsc, __readfsdword, _ReturnAddress, ...), implemented over musl / clang builtins.
// C and C++. The Win32 API itself is in windows.h (same folder).
//
// Semantics that differ from Windows are marked "web:".
#pragma once
#ifndef BO1_WEB_PRELUDE_H
#define BO1_WEB_PRELUDE_H

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <errno.h>
#include <alloca.h>
#include <ctype.h>
#include <float.h>
#include <limits.h>
#include <math.h>

#ifdef __cplusplus
#define BO1_EXTERN_C extern "C"
#define BO1_EXTERN_C_BEGIN extern "C" {
#define BO1_EXTERN_C_END }
#else
#define BO1_EXTERN_C extern
#define BO1_EXTERN_C_BEGIN
#define BO1_EXTERN_C_END
#endif

#define BO1_INLINE static inline __attribute__((always_inline, unused))

// ----- compiler keywords -----
// __cdecl / __stdcall / __fastcall / __thiscall, __int8..__int64, __forceinline, __declspec: clang -fms-extensions
// -fdeclspec (calling conventions are ignored on wasm). The rest:
#ifndef _MSC_EXTENSIONS
#define _MSC_EXTENSIONS 1
#endif
#define __w64
#ifndef __FUNCSIG__
#define __FUNCSIG__ __PRETTY_FUNCTION__
#endif
#define _CRT_ALIGN(x) __attribute__((aligned(x)))
#ifndef _countof
#define _countof(a) (sizeof(a) / sizeof((a)[0]))
#endif
#ifndef _MAX_PATH
#define _MAX_PATH 260
#define _MAX_DRIVE 3
#define _MAX_DIR 256
#define _MAX_FNAME 256
#define _MAX_EXT 256
#endif
// emscripten's <xmmintrin.h> names a struct __unaligned, an MSVC keyword under -fms-extensions (the engine does not
// use the qualifier)
#define __unaligned __bo1_unaligned
// MSVC's FILE is struct _iobuf (decompiled signatures spell it)
#define _iobuf _IO_FILE
#define _errno() (&errno)
#ifdef __cplusplus
// universal/com_math.h declares double random(): POSIX has long random(void) in <stdlib.h>
#define random bo1_engine_random
#endif

BO1_EXTERN_C_BEGIN

// ----- __debugbreak / fatal -----
// web: there is no debugger to break into. Windows without a debugger crashes on it (an assert's last step, see
// Assert_MyHandler); the web build prints the location and a stack trace, tells the page (Module.onEngineError) and
// aborts the runtime.
__attribute__((noreturn)) void bo1_web_debugbreak(const char *file, int line);
#define __debugbreak() bo1_web_debugbreak(__FILE__, __LINE__)
#define DebugBreak() bo1_web_debugbreak(__FILE__, __LINE__)

// ----- strings (MSVC CRT names) -----
#define _stricmp strcasecmp
#define _strnicmp strncasecmp
#define stricmp strcasecmp
#define strnicmp strncasecmp
#define _strcmpi strcasecmp
#define strcmpi strcasecmp
#define _memicmp bo1_memicmp
#define _strdup strdup
// MSVC semantics (not C99): on truncation nothing is terminated and the result is -1; an exact fit is not terminated
int bo1_msvc_snprintf(char *buf, size_t count, const char *fmt, ...);
int bo1_msvc_vsnprintf(char *buf, size_t count, const char *fmt, va_list ap);
#define _snprintf bo1_msvc_snprintf
#define _vsnprintf bo1_msvc_vsnprintf
#define _vscprintf(fmt, ap) vsnprintf(NULL, 0, fmt, ap)
#define _snscanf(buf, n, ...) sscanf(buf, __VA_ARGS__)
int bo1_memicmp(const void *a, const void *b, size_t n);
char *_strlwr(char *s);
char *_strupr(char *s);
#define strlwr _strlwr
#define strupr _strupr
char *_strrev(char *s);
char *_itoa(int value, char *buf, int radix);
char *_ltoa(long value, char *buf, int radix);
char *_ultoa(unsigned long value, char *buf, int radix);
char *_i64toa(long long value, char *buf, int radix);
char *_ui64toa(unsigned long long value, char *buf, int radix);
#define itoa _itoa
#define _atoi64(s) strtoll((s), NULL, 10)
#define _strtoi64 strtoll
#define _strtoui64 strtoull
#define _wtoi(s) ((int)wcstol((s), NULL, 10))

// secure CRT (C11 Annex K, MSVC flavor): size-checked, truncating, always terminated
int sprintf_s(char *buf, size_t size, const char *fmt, ...);
int vsprintf_s(char *buf, size_t size, const char *fmt, va_list ap);
int _snprintf_s(char *buf, size_t size, size_t count, const char *fmt, ...);
int _vsnprintf_s(char *buf, size_t size, size_t count, const char *fmt, va_list ap);
int strcpy_s(char *dst, size_t size, const char *src);
int strncpy_s(char *dst, size_t size, const char *src, size_t count);
int strcat_s(char *dst, size_t size, const char *src);
int strncat_s(char *dst, size_t size, const char *src, size_t count);
int memcpy_s(void *dst, size_t size, const void *src, size_t count);
int memmove_s(void *dst, size_t size, const void *src, size_t count);
#define _TRUNCATE ((size_t)-1)
#define sscanf_s sscanf            // web: the engine's sscanf_s calls use no %s/%c (they would need a size argument)
#define strtok_s strtok_r
#define _strtok_s strtok_r
int fopen_s(FILE **fp, const char *name, const char *mode);
int _dupenv_s(char **buf, size_t *len, const char *name);
#define _putenv putenv
typedef int (*_CoreCrtNonSecureSearchSortCompareFunction)(const void *, const void *);
typedef int (*_CoreCrtSecureSearchSortCompareFunction)(void *, const void *, const void *);

// ----- time (MSVC 64-bit time API: time_t is already 64-bit in Emscripten) -----
typedef time_t __time64_t;
typedef time_t __time32_t;
#define _time64 time
#define _mktime64 mktime
#define _difftime64 difftime
#define _localtime64 localtime
#define _gmtime64 gmtime
#define _ctime64 ctime
BO1_INLINE int localtime_s(struct tm *out, const time_t *t) { return localtime_r(t, out) ? 0 : EINVAL; }
BO1_INLINE int gmtime_s(struct tm *out, const time_t *t) { return gmtime_r(t, out) ? 0 : EINVAL; }
#define _localtime64_s localtime_s
#define _gmtime64_s gmtime_s
char *_strtime(char *buf);
char *_strdate(char *buf);
struct __timeb64 { __time64_t time; unsigned short millitm; short timezone; short dstflag; };
#define _timeb __timeb64
void _ftime64(struct __timeb64 *tb);
#define _ftime _ftime64

// ----- memory -----
// renamed: mimalloc (-sMALLOC=mimalloc) exports its own _aligned_malloc without the matching _aligned_free
void *bo1_aligned_malloc(size_t size, size_t alignment);
void *bo1_aligned_realloc(void *p, size_t size, size_t alignment);
void bo1_aligned_free(void *p);
size_t bo1_aligned_msize(void *p, size_t alignment, size_t offset);
#define _aligned_malloc bo1_aligned_malloc
#define _aligned_realloc bo1_aligned_realloc
#define _aligned_free bo1_aligned_free
#define _aligned_msize bo1_aligned_msize
size_t _msize(void *p);
#define _alloca __builtin_alloca
#define _malloca __builtin_alloca
#define _freea(p) ((void)0)

// ----- numbers -----
#define _isnan(x) __builtin_isnan(x)
#define _finite(x) __builtin_isfinite(x)
#define _hypot hypot
#define _hypotf hypotf
#define _copysign copysign
#define _chgsign(x) (-(x))
// _rotl/_rotr/_lrotl/_lrotr/_rotl64/_rotr64, _byteswap_ushort/_ulong/_uint64: clang builtins with -fms-extensions
BO1_INLINE unsigned char bo1_BitScanForward(unsigned long *index, unsigned long mask)
{
    if (!mask) return 0;
    *index = (unsigned long)__builtin_ctz((unsigned int)mask);
    return 1;
}
BO1_INLINE unsigned char bo1_BitScanReverse(unsigned long *index, unsigned long mask)
{
    if (!mask) return 0;
    *index = 31ul - (unsigned long)__builtin_clz((unsigned int)mask);
    return 1;
}
BO1_INLINE unsigned char bo1_BitScanForward64(unsigned long *index, unsigned long long mask)
{
    if (!mask) return 0;
    *index = (unsigned long)__builtin_ctzll(mask);
    return 1;
}
BO1_INLINE unsigned char bo1_BitScanReverse64(unsigned long *index, unsigned long long mask)
{
    if (!mask) return 0;
    *index = 63ul - (unsigned long)__builtin_clzll(mask);
    return 1;
}
// decompiled callers pass DWORD *, unsigned int *, int *: all 32-bit
#define _BitScanForward(index, mask) bo1_BitScanForward((unsigned long *)(index), (unsigned long)(mask))
#define _BitScanReverse(index, mask) bo1_BitScanReverse((unsigned long *)(index), (unsigned long)(mask))
#define _BitScanForward64(index, mask) bo1_BitScanForward64((unsigned long *)(index), (unsigned long long)(mask))
#define _BitScanReverse64(index, mask) bo1_BitScanReverse64((unsigned long *)(index), (unsigned long long)(mask))
#define __popcnt(x) ((unsigned int)__builtin_popcount((unsigned int)(x)))
#define __popcnt16(x) ((unsigned short)__builtin_popcount((unsigned short)(x)))
#define __lzcnt(x) ((unsigned int)((x) ? __builtin_clz((unsigned int)(x)) : 32))

// ----- intrinsics -----
// web: a monotonic nanosecond clock stands in for the time stamp counter (profiling only)
unsigned long long bo1_web_rdtsc(void);
#define __rdtsc() bo1_web_rdtsc()
// __readfsdword(0x24) is the current thread id on x86 Windows (TEB.ClientId.UniqueThread); profile.h uses it
unsigned long GetCurrentThreadId(void);
BO1_INLINE unsigned long __readfsdword(unsigned long offset)
{
    return offset == 0x24 ? GetCurrentThreadId() : 0;
}
#define _ReturnAddress() ((void *)0)          // web: no return addresses in wasm (cg_colltree debug ids only)
#define _AddressOfReturnAddress() ((void *)0)
#define _ReadWriteBarrier() __atomic_signal_fence(__ATOMIC_SEQ_CST)
#define _ReadBarrier() __atomic_signal_fence(__ATOMIC_SEQ_CST)
#define _WriteBarrier() __atomic_signal_fence(__ATOMIC_SEQ_CST)
#define MemoryBarrier() __atomic_thread_fence(__ATOMIC_SEQ_CST)
#define _mm_mfence_compat() __atomic_thread_fence(__ATOMIC_SEQ_CST)
#define YieldProcessor() ((void)0)
#define __nop() ((void)0)
#define __cpuid(info, leaf) bo1_web_cpuid((int *)(info), (leaf))
#define __cpuidex(info, leaf, sub) bo1_web_cpuid((int *)(info), (leaf))
void bo1_web_cpuid(int info[4], int leaf);   // fixed values (web_configure.cpp)

// ----- _Interlocked* (MSVC intrinsics, full barrier) -----
// Decompiled callers pass any 32-bit integer or pointer type (volatile unsigned int *, volatile int *, T **): the
// macros cast like MSVC's lenient intrinsics. Implemented with sequentially consistent __atomic builtins.
BO1_INLINE long bo1_ilk_add(volatile void *p, long v) { return __atomic_fetch_add((volatile long *)p, v, __ATOMIC_SEQ_CST); }
BO1_INLINE long bo1_ilk_xchg(volatile void *p, long v) { return __atomic_exchange_n((volatile long *)p, v, __ATOMIC_SEQ_CST); }
BO1_INLINE long bo1_ilk_cmpxchg(volatile void *p, long xchg, long cmp)
{
    __atomic_compare_exchange_n((volatile long *)p, &cmp, xchg, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return cmp;
}
BO1_INLINE long bo1_ilk_or(volatile void *p, long v) { return __atomic_fetch_or((volatile long *)p, v, __ATOMIC_SEQ_CST); }
BO1_INLINE long bo1_ilk_and(volatile void *p, long v) { return __atomic_fetch_and((volatile long *)p, v, __ATOMIC_SEQ_CST); }
BO1_INLINE long bo1_ilk_xor(volatile void *p, long v) { return __atomic_fetch_xor((volatile long *)p, v, __ATOMIC_SEQ_CST); }
BO1_INLINE short bo1_ilk_add16(volatile void *p, short v) { return __atomic_fetch_add((volatile short *)p, v, __ATOMIC_SEQ_CST); }
BO1_INLINE short bo1_ilk_cmpxchg16(volatile void *p, short xchg, short cmp)
{
    __atomic_compare_exchange_n((volatile short *)p, &cmp, xchg, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return cmp;
}
BO1_INLINE char bo1_ilk_xchg8(volatile void *p, char v) { return __atomic_exchange_n((volatile char *)p, v, __ATOMIC_SEQ_CST); }
BO1_INLINE char bo1_ilk_cmpxchg8(volatile void *p, char xchg, char cmp)
{
    __atomic_compare_exchange_n((volatile char *)p, &cmp, xchg, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return cmp;
}
BO1_INLINE long long bo1_ilk_add64(volatile void *p, long long v) { return __atomic_fetch_add((volatile long long *)p, v, __ATOMIC_SEQ_CST); }
BO1_INLINE long long bo1_ilk_xchg64(volatile void *p, long long v) { return __atomic_exchange_n((volatile long long *)p, v, __ATOMIC_SEQ_CST); }
BO1_INLINE long long bo1_ilk_cmpxchg64(volatile void *p, long long xchg, long long cmp)
{
    __atomic_compare_exchange_n((volatile long long *)p, &cmp, xchg, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return cmp;
}
BO1_INLINE void *bo1_ilk_xchgptr(volatile void *p, void *v) { return __atomic_exchange_n((void *volatile *)p, v, __ATOMIC_SEQ_CST); }
BO1_INLINE void *bo1_ilk_cmpxchgptr(volatile void *p, void *xchg, void *cmp)
{
    __atomic_compare_exchange_n((void *volatile *)p, &cmp, xchg, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return cmp;
}

#define _InterlockedIncrement(p) (bo1_ilk_add((p), 1) + 1)
#define _InterlockedDecrement(p) (bo1_ilk_add((p), -1) - 1)
#define _InterlockedExchangeAdd(p, v) bo1_ilk_add((p), (long)(v))
#define _InterlockedExchange(p, v) bo1_ilk_xchg((p), (long)(v))
#define _InterlockedCompareExchange(p, x, c) bo1_ilk_cmpxchg((p), (long)(x), (long)(c))
#define _InterlockedOr(p, v) bo1_ilk_or((p), (long)(v))
#define _InterlockedAnd(p, v) bo1_ilk_and((p), (long)(v))
#define _InterlockedXor(p, v) bo1_ilk_xor((p), (long)(v))
#define _InterlockedIncrement16(p) ((short)(bo1_ilk_add16((p), 1) + 1))
#define _InterlockedDecrement16(p) ((short)(bo1_ilk_add16((p), -1) - 1))
#define _InterlockedCompareExchange16(p, x, c) bo1_ilk_cmpxchg16((p), (short)(x), (short)(c))
#define _InterlockedExchange8(p, v) bo1_ilk_xchg8((p), (char)(v))
#define _InterlockedCompareExchange8(p, x, c) bo1_ilk_cmpxchg8((p), (char)(x), (char)(c))
#define _InterlockedIncrement64(p) (bo1_ilk_add64((p), 1) + 1)
#define _InterlockedDecrement64(p) (bo1_ilk_add64((p), -1) - 1)
#define _InterlockedExchangeAdd64(p, v) bo1_ilk_add64((p), (long long)(v))
#define _InterlockedExchange64(p, v) bo1_ilk_xchg64((p), (long long)(v))
#define _InterlockedCompareExchange64(p, x, c) bo1_ilk_cmpxchg64((p), (long long)(x), (long long)(c))
#define _InterlockedExchangePointer(p, v) bo1_ilk_xchgptr((p), (void *)(v))
#define _InterlockedCompareExchangePointer(p, x, c) bo1_ilk_cmpxchgptr((p), (void *)(x), (void *)(c))
// winnt.h names
#define InterlockedIncrement _InterlockedIncrement
#define InterlockedDecrement _InterlockedDecrement
#define InterlockedExchangeAdd _InterlockedExchangeAdd
#define InterlockedExchange _InterlockedExchange
#define InterlockedCompareExchange _InterlockedCompareExchange
#define InterlockedOr _InterlockedOr
#define InterlockedAnd _InterlockedAnd
#define InterlockedXor _InterlockedXor
#define InterlockedIncrement16 _InterlockedIncrement16
#define InterlockedDecrement16 _InterlockedDecrement16
#define InterlockedCompareExchange16 _InterlockedCompareExchange16
#define InterlockedIncrement64 _InterlockedIncrement64
#define InterlockedDecrement64 _InterlockedDecrement64
#define InterlockedExchangeAdd64 _InterlockedExchangeAdd64
#define InterlockedExchange64 _InterlockedExchange64
#define InterlockedCompareExchange64 _InterlockedCompareExchange64
#define InterlockedExchangePointer _InterlockedExchangePointer
#define InterlockedCompareExchangePointer _InterlockedCompareExchangePointer
#define InterlockedCompareExchangeAcquire _InterlockedCompareExchange
#define InterlockedCompareExchangeRelease _InterlockedCompareExchange
#define InterlockedIncrementAcquire _InterlockedIncrement
#define InterlockedDecrementRelease _InterlockedDecrement

// ----- io.h / direct.h / process.h (declared here because MSVC's stdio/stdlib reach them) -----
struct _finddata_t;
int _mkdir(const char *path);
char *_getcwd(char *buf, int size);
int _chdir(const char *path);
int _rmdir(const char *path);
int _unlink(const char *path);
int _access(const char *path, int mode);
int _fileno(FILE *fp);
long _filelength(int fd);
long long _filelengthi64(int fd);
int _getpid(void);
#define _getch getchar
#define _fseeki64 fseeko
#define _ftelli64 ftello
#define _O_RDONLY 0x0000
#define _O_WRONLY 0x0001
#define _O_RDWR 0x0002
#define _O_APPEND 0x0008
#define _O_CREAT 0x0100
#define _O_TRUNC 0x0200
#define _O_EXCL 0x0400
#define _O_TEXT 0x4000
#define _O_BINARY 0x8000
#define O_BINARY 0
#define O_TEXT 0

BO1_EXTERN_C_END

#ifdef __cplusplus
// MSVC's template overloads of the secure CRT that take the buffer size from an array
template <size_t N> inline int sprintf_s(char (&buf)[N], const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    const int r = vsprintf_s(buf, N, fmt, ap);
    va_end(ap);
    return r;
}
template <size_t N> inline int vsprintf_s(char (&buf)[N], const char *fmt, va_list ap) { return vsprintf_s(buf, N, fmt, ap); }
template <size_t N> inline int _snprintf_s(char (&buf)[N], size_t count, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    const int r = _vsnprintf_s(buf, N, count, fmt, ap);
    va_end(ap);
    return r;
}
template <size_t N> inline int _vsnprintf_s(char (&buf)[N], size_t count, const char *fmt, va_list ap)
{
    return _vsnprintf_s(buf, N, count, fmt, ap);
}
template <size_t N> inline int strcpy_s(char (&dst)[N], const char *src) { return strcpy_s(dst, N, src); }
template <size_t N> inline int strncpy_s(char (&dst)[N], const char *src, size_t count) { return strncpy_s(dst, N, src, count); }
template <size_t N> inline int strcat_s(char (&dst)[N], const char *src) { return strcat_s(dst, N, src); }
template <size_t N> inline int strncat_s(char (&dst)[N], const char *src, size_t count) { return strncat_s(dst, N, src, count); }
#endif

#ifdef __cplusplus
// MSVC: jmp_buf is int[16] and _setjmp is the setjmp intrinsic; the engine passes int * (Sys_GetValue(2),
// g_com_error[i], g_script_error[...]). Emscripten's wasm setjmp (SUPPORT_LONGJMP=wasm) uses the first 16 bytes of
// the buffer, so the engine's 64-byte buffers are big enough.
#include <setjmp.h>
#undef setjmp
#define setjmp(env) setjmp(*(jmp_buf *)(void *)(env))
#define _setjmp(env) setjmp(env)
[[noreturn]] inline void longjmp(int *env, int value) { longjmp(*(jmp_buf *)(void *)env, value); }
#endif

#endif // BO1_WEB_PRELUDE_H
