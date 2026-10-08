// win_memory.cpp - virtual memory and Win32 heaps over the wasm heap (web build).
//
// wasm memory cannot be reserved without being committed, so VirtualAlloc(MEM_RESERVE) allocates the whole range at
// once (64 KB aligned, zeroed). Commit / decommit only change the per-page bookkeeping that VirtualQuery reports
// (universal/com_memory.cpp walks it to account committed bytes); pages committed again after a decommit are zeroed,
// as Windows hands out zero pages. MEM_RELEASE frees the range. Addresses passed to VirtualAlloc that are not inside a
// range this file allocated fail (the engine never asks for fixed addresses).
#include "bo1_win_internal.h"
#include <psapi.h>
#include <emscripten/heap.h>
#include <malloc.h>
#include <unistd.h>
#include <map>
#include <mutex>
#include <vector>

namespace
{
constexpr uintptr_t PAGE = 4096;
constexpr uintptr_t GRANULARITY = 65536;

enum PageState : uint8_t
{
    PAGE_RESERVED = 0,      // never committed (zero)
    PAGE_COMMITTED = 1,
    PAGE_DECOMMITTED = 2,   // reserved again; zeroed when committed again
};

struct Region
{
    uintptr_t base;
    size_t size;
    DWORD allocProtect;
    std::vector<uint8_t> pages;
};

std::mutex s_lock;
std::map<uintptr_t, Region> *s_regions;   // by base

Region *FindRegion(uintptr_t addr)
{
    if (!s_regions || s_regions->empty())
        return nullptr;
    auto it = s_regions->upper_bound(addr);
    if (it == s_regions->begin())
        return nullptr;
    --it;
    Region &r = it->second;
    return addr < r.base + r.size ? &r : nullptr;
}

void Commit(Region &r, uintptr_t from, uintptr_t to)
{
    for (uintptr_t a = from; a < to; a += PAGE)
    {
        uint8_t &s = r.pages[(a - r.base) / PAGE];
        if (s == PAGE_DECOMMITTED)
            memset((void *)a, 0, PAGE);
        s = PAGE_COMMITTED;
    }
}
}

extern "C" {

LPVOID VirtualAlloc(LPVOID address, SIZE_T size, DWORD type, DWORD protect)
{
    if (!size)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(s_lock);
    if (!s_regions)
        s_regions = new std::map<uintptr_t, Region>();
    if (address)
    {
        // commit (or re-reserve) inside a range this file allocated
        const uintptr_t from = (uintptr_t)address & ~(PAGE - 1);
        const uintptr_t to = ((uintptr_t)address + size + PAGE - 1) & ~(PAGE - 1);
        Region *r = FindRegion(from);
        if (!r || to > r->base + r->size)
        {
            SetLastError(ERROR_INVALID_ADDRESS);
            return nullptr;
        }
        if (type & MEM_COMMIT)
            Commit(*r, from, to);
        return (LPVOID)from;
    }
    const size_t total = (size + GRANULARITY - 1) & ~(GRANULARITY - 1);
    void *p = aligned_alloc(GRANULARITY, total);
    if (!p)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return nullptr;
    }
    memset(p, 0, total);
    Region r;
    r.base = (uintptr_t)p;
    r.size = total;
    r.allocProtect = protect;
    r.pages.assign(total / PAGE, (type & MEM_COMMIT) ? PAGE_COMMITTED : PAGE_RESERVED);
    (*s_regions)[r.base] = std::move(r);
    return p;
}

BOOL VirtualFree(LPVOID address, SIZE_T size, DWORD type)
{
    std::lock_guard<std::mutex> lock(s_lock);
    Region *r = FindRegion((uintptr_t)address);
    if (!r)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (type & MEM_RELEASE)
    {
        if ((uintptr_t)address != r->base || size)
        {
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        free((void *)r->base);
        s_regions->erase(r->base);
        return TRUE;
    }
    if (type & MEM_DECOMMIT)
    {
        const uintptr_t from = (uintptr_t)address & ~(PAGE - 1);
        const uintptr_t to = size ? ((uintptr_t)address + size + PAGE - 1) & ~(PAGE - 1) : r->base + r->size;
        for (uintptr_t a = from; a < to && a < r->base + r->size; a += PAGE)
        {
            uint8_t &s = r->pages[(a - r->base) / PAGE];
            if (s == PAGE_COMMITTED)
                s = PAGE_DECOMMITTED;
        }
        return TRUE;
    }
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
}

SIZE_T VirtualQuery(LPCVOID address, PMEMORY_BASIC_INFORMATION info, SIZE_T length)
{
    if (!info || length < sizeof(MEMORY_BASIC_INFORMATION))
        return 0;
    const uintptr_t addr = (uintptr_t)address & ~(PAGE - 1);
    const uintptr_t memSize = (uintptr_t)emscripten_get_heap_size();
    if (addr >= memSize)
        return 0;
    std::lock_guard<std::mutex> lock(s_lock);
    memset(info, 0, sizeof(*info));
    info->BaseAddress = (PVOID)addr;
    Region *r = FindRegion(addr);
    if (!r)
    {
        // not ours: report the gap up to the next range as free
        uintptr_t next = memSize;
        if (s_regions)
        {
            auto it = s_regions->upper_bound(addr);
            if (it != s_regions->end())
                next = it->first;
        }
        info->AllocationBase = nullptr;
        info->RegionSize = next - addr;
        info->State = MEM_FREE;
        info->Protect = PAGE_NOACCESS;
        return sizeof(MEMORY_BASIC_INFORMATION);
    }
    size_t index = (addr - r->base) / PAGE;
    const bool committed = r->pages[index] == PAGE_COMMITTED;
    size_t end = index;
    while (end < r->pages.size() && (r->pages[end] == PAGE_COMMITTED) == committed)
        ++end;
    info->AllocationBase = (PVOID)r->base;
    info->AllocationProtect = r->allocProtect;
    info->RegionSize = (end - index) * PAGE;
    info->State = committed ? MEM_COMMIT : MEM_RESERVE;
    info->Protect = committed ? PAGE_READWRITE : 0;
    info->Type = MEM_PRIVATE;
    return sizeof(MEMORY_BASIC_INFORMATION);
}

BOOL VirtualProtect(LPVOID address, SIZE_T size, DWORD newProtect, PDWORD oldProtect)
{
    if (oldProtect)
        *oldProtect = PAGE_READWRITE;
    return TRUE;   // web: no page protection in wasm
}

BOOL VirtualLock(LPVOID address, SIZE_T size)
{
    return TRUE;
}

BOOL VirtualUnlock(LPVOID address, SIZE_T size)
{
    return TRUE;
}

BOOL FlushInstructionCache(HANDLE process, LPCVOID address, SIZE_T size)
{
    return TRUE;
}

// ----- memory status: the wasm memory is the machine -----
// total: the wasm memory; used: the top of the sbrk heap (allocators take their memory from there)
static void HeapNumbers(uint64_t *total, uint64_t *avail)
{
    *total = emscripten_get_heap_max();
    const uint64_t used = (uint64_t)(uintptr_t)sbrk(0);
    *avail = *total > used ? *total - used : 0;
}

void GlobalMemoryStatus(LPMEMORYSTATUS status)
{
    uint64_t total, avail;
    HeapNumbers(&total, &avail);
    memset(status, 0, sizeof(*status));
    status->dwLength = sizeof(*status);
    status->dwMemoryLoad = total ? (DWORD)(100 - avail * 100 / total) : 0;
    status->dwTotalPhys = (SIZE_T)total;
    status->dwAvailPhys = (SIZE_T)avail;
    status->dwTotalPageFile = (SIZE_T)total;
    status->dwAvailPageFile = (SIZE_T)avail;
    status->dwTotalVirtual = (SIZE_T)total;
    status->dwAvailVirtual = (SIZE_T)avail;
}

BOOL GlobalMemoryStatusEx(LPMEMORYSTATUSEX status)
{
    uint64_t total, avail;
    HeapNumbers(&total, &avail);
    status->dwMemoryLoad = total ? (DWORD)(100 - avail * 100 / total) : 0;
    status->ullTotalPhys = total;
    status->ullAvailPhys = avail;
    status->ullTotalPageFile = total;
    status->ullAvailPageFile = avail;
    status->ullTotalVirtual = total;
    status->ullAvailVirtual = avail;
    status->ullAvailExtendedVirtual = 0;
    return TRUE;
}

BOOL GetProcessMemoryInfo(HANDLE process, PPROCESS_MEMORY_COUNTERS counters, DWORD cb)
{
    if (!counters || cb < sizeof(PROCESS_MEMORY_COUNTERS))
        return FALSE;
    uint64_t total, avail;
    HeapNumbers(&total, &avail);
    memset(counters, 0, cb);
    counters->cb = cb;
    counters->WorkingSetSize = counters->PeakWorkingSetSize = (SIZE_T)(total - avail);
    counters->PagefileUsage = counters->PeakPagefileUsage = (SIZE_T)(total - avail);
    if (cb >= sizeof(PROCESS_MEMORY_COUNTERS_EX))
        ((PROCESS_MEMORY_COUNTERS_EX *)counters)->PrivateUsage = (SIZE_T)(total - avail);
    return TRUE;
}

// ----- Global* / Local* (clipboard buffers, FormatMessage): malloc with the size in front -----
static void *SizedAlloc(size_t bytes, bool zero)
{
    size_t *p = (size_t *)(zero ? calloc(1, bytes + 16) : malloc(bytes + 16));
    if (!p)
        return nullptr;
    p[0] = bytes;
    return (char *)p + 16;
}

static void SizedFree(void *mem)
{
    if (mem)
        free((char *)mem - 16);
}

static size_t SizedSize(void *mem)
{
    return mem ? *(size_t *)((char *)mem - 16) : 0;
}

HGLOBAL GlobalAlloc(UINT flags, SIZE_T bytes)
{
    return SizedAlloc(bytes, (flags & GMEM_ZEROINIT) != 0);
}

LPVOID GlobalLock(HGLOBAL mem)
{
    return mem;
}

BOOL GlobalUnlock(HGLOBAL mem)
{
    return TRUE;
}

HGLOBAL GlobalFree(HGLOBAL mem)
{
    SizedFree(mem);
    return nullptr;
}

SIZE_T GlobalSize(HGLOBAL mem)
{
    return SizedSize(mem);
}

HLOCAL LocalAlloc(UINT flags, SIZE_T size)
{
    return SizedAlloc(size, (flags & LMEM_ZEROINIT) != 0);
}

HLOCAL LocalFree(HLOCAL mem)
{
    SizedFree(mem);
    return nullptr;
}

// ----- Heap*: one process heap, malloc -----
HANDLE GetProcessHeap(void)
{
    return (HANDLE)(uintptr_t)0x48454150;   // 'HEAP'
}

HANDLE HeapCreate(DWORD options, SIZE_T initial, SIZE_T maximum)
{
    return GetProcessHeap();
}

BOOL HeapDestroy(HANDLE heap)
{
    return TRUE;
}

LPVOID HeapAlloc(HANDLE heap, DWORD flags, SIZE_T bytes)
{
    return (flags & HEAP_ZERO_MEMORY) ? calloc(1, bytes ? bytes : 1) : malloc(bytes ? bytes : 1);
}

LPVOID HeapReAlloc(HANDLE heap, DWORD flags, LPVOID mem, SIZE_T bytes)
{
    const size_t old = mem ? malloc_usable_size(mem) : 0;
    void *p = realloc(mem, bytes ? bytes : 1);
    if (p && (flags & HEAP_ZERO_MEMORY) && bytes > old)
        memset((char *)p + old, 0, bytes - old);
    return p;
}

BOOL HeapFree(HANDLE heap, DWORD flags, LPVOID mem)
{
    free(mem);
    return TRUE;
}

SIZE_T HeapSize(HANDLE heap, DWORD flags, LPCVOID mem)
{
    return mem ? malloc_usable_size((void *)mem) : (SIZE_T)-1;
}

BOOL HeapValidate(HANDLE heap, DWORD flags, LPCVOID mem)
{
    return TRUE;
}

} // extern "C"
