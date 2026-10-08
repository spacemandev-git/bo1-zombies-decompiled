// bo1_win_internal.h - shared internals of the Win32 emulation (src/web/compat/*.cpp). Not for engine code.
#pragma once
#include "windows.h"
#include <pthread.h>
#include <atomic>
#include <stdint.h>
#include <string>

// win_crt.cpp: the page's lifecycle hooks (docs/web-engine-interface.md section 5)
extern "C" __attribute__((noreturn)) void bo1_web_exit(int code);
extern "C" void bo1_web_report_error(const char *message);

namespace bo1w
{
// Every kernel object handle the emulation returns points at one of these (HANDLE == Object *).
enum Kind : uint32_t
{
    KIND_EVENT = 1,
    KIND_MUTEX,
    KIND_SEMAPHORE,
    KIND_THREAD,
    KIND_FILE,
    KIND_FIND,
};
constexpr uint32_t OBJECT_MAGIC = 0xB01B0A1Du;

struct Object
{
    uint32_t magic = OBJECT_MAGIC;
    Kind kind;
    std::atomic<int> refs{1};
    explicit Object(Kind k) : kind(k) {}
    virtual ~Object() { magic = 0; }
};

// NULL, INVALID_HANDLE_VALUE, pseudo handles and foreign pointers -> nullptr
Object *ToObject(HANDLE h);
template <class T> T *As(HANDLE h, Kind kind)
{
    Object *o = ToObject(h);
    return o && o->kind == kind ? static_cast<T *>(o) : nullptr;
}
void AddRef(Object *o);
void Release(Object *o);

// win_sync.cpp: queue an I/O completion routine (ReadFileEx / WriteFileEx) as an APC of the calling thread
void QueueIoCompletion(LPOVERLAPPED_COMPLETION_ROUTINE fn, DWORD error, DWORD bytes, LPOVERLAPPED ov);

// win_misc.cpp
DWORD ErrnoToWin32(int e);
uint64_t NowNs();                 // monotonic
uint64_t UnixNsToFileTime(int64_t sec, int64_t nsec);

// web_path.cpp: '\' or '/', relative to the cwd, resolved case-insensitively against what exists. Returns true if
// the whole path exists; otherwise out holds the existing prefix (real case) + the rest as given (for creation).
bool ResolvePath(const char *in, char *out, size_t outSize);
// forget cached listings of the directory containing path (after creating / deleting / renaming in it)
void InvalidateDirOf(const char *resolvedPath);
void InvalidateAllDirs();
// the tracked current directory (_getcwd, _chdir, Get/SetCurrentDirectoryA)
std::string GetCwd();
bool SetCwd(const char *path);
// case-insensitive wildcard match (* and ?), Windows style ("*.*" matches names without a dot too)
bool WildcardMatch(const char *pattern, const char *name);
}
