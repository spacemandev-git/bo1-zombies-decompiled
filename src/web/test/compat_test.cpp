// compat_test.cpp - checks of the Win32 emulation (src/web/compat) the engine relies on. Node build only:
//
//   cmake --preset web-node && cmake --build --preset web-node --target bo1_compat_test
//   cd / && node <repo>/build/web-node/bin/bo1_compat_test.js /tmp/some-empty-dir
//
// Prints one line per check and exits 1 on the first failure.
#include <windows.h>
#include <io.h>
#include <direct.h>
#include <sys/stat.h>
#include <string>

static int s_failures;
#define CHECK(cond)                                                                 \
    do                                                                              \
    {                                                                               \
        if (cond)                                                                   \
            printf("ok   %s\n", #cond);                                             \
        else                                                                        \
        {                                                                           \
            printf("FAIL %s (%s:%d)\n", #cond, __FILE__, __LINE__);                 \
            ++s_failures;                                                           \
        }                                                                           \
    } while (0)

static HANDLE s_event;
static volatile LONG s_counter;
static DWORD WINAPI Worker(LPVOID param)
{
    InterlockedIncrement(&s_counter);
    WaitForSingleObject(s_event, INFINITE);
    InterlockedExchangeAdd(&s_counter, (LONG)(intptr_t)param);
    return 7;
}

static int s_apcRuns;
static DWORD s_apcBytes;
static void CALLBACK Done(DWORD err, DWORD bytes, LPOVERLAPPED ov)
{
    ++s_apcRuns;
    s_apcBytes = bytes;
}

static void TestSync()
{
    // events
    HANDLE manual = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    HANDLE autoEv = CreateEventA(nullptr, FALSE, TRUE, nullptr);
    CHECK(WaitForSingleObject(manual, 0) == WAIT_TIMEOUT);
    CHECK(WaitForSingleObject(autoEv, 0) == WAIT_OBJECT_0);
    CHECK(WaitForSingleObject(autoEv, 0) == WAIT_TIMEOUT);   // auto-reset consumed
    SetEvent(manual);
    CHECK(WaitForSingleObject(manual, 0) == WAIT_OBJECT_0);
    CHECK(WaitForSingleObject(manual, 0) == WAIT_OBJECT_0);  // manual stays set
    HANDLE both[2] = {autoEv, manual};
    CHECK(WaitForMultipleObjects(2, both, FALSE, 0) == WAIT_OBJECT_0 + 1);
    CHECK(WaitForMultipleObjects(2, both, TRUE, 10) == WAIT_TIMEOUT);
    SetEvent(autoEv);
    CHECK(WaitForMultipleObjects(2, both, TRUE, 10) == WAIT_OBJECT_0);
    const DWORD t0 = GetTickCount();
    CHECK(WaitForSingleObject(autoEv, 50) == WAIT_TIMEOUT);
    CHECK(GetTickCount() - t0 >= 45);

    // threads: CREATE_SUSPENDED, ResumeThread, join through the handle, exit code
    s_event = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    DWORD id = 0;
    HANDLE t = CreateThread(nullptr, 0, Worker, (LPVOID)(intptr_t)100, CREATE_SUSPENDED, &id);
    CHECK(t != nullptr && id != 0);
    Sleep(50);
    CHECK(s_counter == 0);   // still suspended
    CHECK(ResumeThread(t) == 1);
    while (s_counter == 0)
        Sleep(1);
    CHECK(WaitForSingleObject(t, 20) == WAIT_TIMEOUT);
    SetEvent(s_event);
    CHECK(WaitForSingleObject(t, 5000) == WAIT_OBJECT_0);
    DWORD code = 0;
    CHECK(GetExitCodeThread(t, &code) && code == 7);
    CHECK(s_counter == 101);
    CloseHandle(t);

    // critical sections are recursive
    CRITICAL_SECTION cs;
    InitializeCriticalSection(&cs);
    EnterCriticalSection(&cs);
    EnterCriticalSection(&cs);
    CHECK(TryEnterCriticalSection(&cs));
    LeaveCriticalSection(&cs);
    LeaveCriticalSection(&cs);
    LeaveCriticalSection(&cs);

    // TLS
    const DWORD tls = TlsAlloc();
    CHECK(tls != TLS_OUT_OF_INDEXES);
    TlsSetValue(tls, (LPVOID)0x1234);
    CHECK(TlsGetValue(tls) == (LPVOID)0x1234);

    // interlocked on the types decompiled code passes
    volatile unsigned int u = 5;
    CHECK(_InterlockedExchangeAdd((volatile unsigned __int32 *)&u, 3) == 5 && u == 8);
    CHECK(_InterlockedCompareExchange(&u, 1, 8) == 8 && u == 1);
    void *volatile ptr = nullptr;
    CHECK(InterlockedCompareExchangePointer(&ptr, (void *)&u, nullptr) == nullptr && ptr == &u);
    unsigned long idx = 0;
    CHECK(_BitScanReverse(&idx, 0x80u) && idx == 7);
}

static void TestFiles(const std::string &root)
{
    // case-insensitive, either separator
    std::string dir = root + "/Zone/English";
    CHECK(CreateDirectoryA((root + "\\Zone").c_str(), nullptr));
    CHECK(_mkdir(dir.c_str()) == 0);
    FILE *f = fopen((root + "/Zone/English/Code_Post_GFX.ff").c_str(), "wb");
    CHECK(f != nullptr);
    std::string payload(600000, 'x');
    for (size_t i = 0; i < payload.size(); ++i)
        payload[i] = (char)(i * 7);
    fwrite(payload.data(), 1, payload.size(), f);
    fclose(f);
    const std::string winPath = root + "\\zone\\english\\code_post_gfx.ff";
    CHECK(GetFileAttributesA(winPath.c_str()) != INVALID_FILE_ATTRIBUTES);
    FILE *r = fopen(winPath.c_str(), "rb");
    CHECK(r != nullptr);
    if (r)
        fclose(r);

    // the fastfile loader's pattern: unbuffered overlapped ReadFileEx in 256 KB chunks, one SleepEx per chunk
    HANDLE h = CreateFileA(winPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED | FILE_FLAG_NO_BUFFERING, nullptr);
    CHECK(h != INVALID_HANDLE_VALUE);
    CHECK(GetFileSize(h, nullptr) == payload.size());
    static char buf[0x80000];
    OVERLAPPED ov = {};
    CHECK(ReadFileEx(h, buf, 0x40000, &ov, Done));
    ov.Offset += 0x40000;
    CHECK(ReadFileEx(h, buf + 0x40000, 0x40000, &ov, Done));
    ov.Offset += 0x40000;
    CHECK(s_apcRuns == 0);   // completions wait for an alertable wait
    CHECK(SleepEx(INFINITE, TRUE) == WAIT_IO_COMPLETION && s_apcRuns == 1 && s_apcBytes == 0x40000);
    CHECK(SleepEx(INFINITE, TRUE) == WAIT_IO_COMPLETION && s_apcRuns == 2);
    CHECK(ReadFileEx(h, buf, 0x40000, &ov, Done));   // the partial last chunk
    CHECK(SleepEx(INFINITE, TRUE) == WAIT_IO_COMPLETION && s_apcBytes == payload.size() - 0x80000);
    ov.Offset += 0x40000;
    CHECK(!ReadFileEx(h, buf, 0x40000, &ov, Done) && GetLastError() == ERROR_HANDLE_EOF);
    CHECK(memcmp(buf, payload.data() + 0x80000, payload.size() - 0x80000) == 0);
    CloseHandle(h);

    // directory listing with wildcards (Sys_ListFiles)
    struct _finddata64i32_t fd;
    const intptr_t fh = _findfirst64i32((root + "\\ZONE\\english\\*.ff").c_str(), &fd);
    CHECK(fh != -1 && !strcmp(fd.name, "Code_Post_GFX.ff") && !(fd.attrib & _A_SUBDIR));
    if (fh != -1)
        _findclose(fh);
    WIN32_FIND_DATAA wfd;
    HANDLE find = FindFirstFileA((root + "/zone/*").c_str(), &wfd);
    CHECK(find != INVALID_HANDLE_VALUE);
    bool sawEnglish = false;
    do
        sawEnglish |= !strcmp(wfd.cFileName, "English") && (wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
    while (FindNextFileA(find, &wfd));
    FindClose(find);
    CHECK(sawEnglish);

    // relative paths and the tracked current directory
    CHECK(SetCurrentDirectoryA((root + "/zone").c_str()));
    CHECK(GetFileAttributesA("ENGLISH\\code_post_gfx.ff") != INVALID_FILE_ATTRIBUTES);
    char cwd[512];
    CHECK(_getcwd(cwd, sizeof(cwd)) && strstr(cwd, "/Zone"));
}

static void TestMemory()
{
    char *p = (char *)VirtualAlloc(nullptr, 1 << 20, MEM_RESERVE, PAGE_READWRITE);
    CHECK(p != nullptr && ((uintptr_t)p & 0xFFFF) == 0);
    CHECK(VirtualAlloc(p + 8192, 4096, MEM_COMMIT, PAGE_READWRITE) == p + 8192);
    MEMORY_BASIC_INFORMATION mbi;
    CHECK(VirtualQuery(p, &mbi, 0x1C) == sizeof(mbi) && mbi.State == MEM_RESERVE && mbi.RegionSize == 8192 && mbi.AllocationBase == p);
    CHECK(VirtualQuery(p + 8192, &mbi, 0x1C) && mbi.State == MEM_COMMIT && mbi.RegionSize == 4096);
    p[8192] = 42;
    CHECK(VirtualFree(p + 8192, 4096, MEM_DECOMMIT));
    CHECK(VirtualAlloc(p + 8192, 4096, MEM_COMMIT, PAGE_READWRITE) && p[8192] == 0);   // re-committed pages are zero
    CHECK(VirtualFree(p, 0, MEM_RELEASE));
    void *a = _aligned_malloc(100, 64);
    CHECK(((uintptr_t)a & 63) == 0);
    a = _aligned_realloc(a, 1000, 64);
    CHECK(((uintptr_t)a & 63) == 0);
    _aligned_free(a);
}

static void TestCrt()
{
    char b[8];
    CHECK(_snprintf(b, sizeof(b), "%s", "123456789") == -1);   // MSVC: truncation is -1, unterminated
    CHECK(_snprintf(b, sizeof(b), "%s", "12345678") == 8 && b[7] == '8');   // an exact fit is not terminated
    CHECK(_snprintf(b, sizeof(b), "%d", 42) == 2 && !strcmp(b, "42"));
    CHECK(sprintf_s(b, "%d", 7) == 1 && !strcmp(b, "7"));
    CHECK(!_stricmp("ABC", "abc") && _strnicmp("abcd", "ABCE", 3) == 0);
    char n[32];
    CHECK(!strcmp(_itoa(-15, n, 10), "-15") && !strcmp(_itoa(255, n, 16), "ff"));
}

int main(int argc, char **argv)
{
    const std::string root = argc > 1 ? argv[1] : "/tmp/bo1_compat_test";
    _mkdir(root.c_str());
    TestSync();
    TestFiles(root);
    TestMemory();
    TestCrt();
    printf("%s: %d failure(s)\n", s_failures ? "FAILED" : "PASSED", s_failures);
    return s_failures ? 1 : 0;
}
