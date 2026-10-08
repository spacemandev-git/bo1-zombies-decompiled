// win_misc.cpp - kernel32 odds and ends for the web build: errors, time, system information, locale and string
// conversion, modules, processes, Toolhelp, Winsock stubs.
#include "bo1_win_internal.h"
#include <tlhelp32.h>
#include <winsock2.h>
#include <emscripten.h>
#include <emscripten/threading.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

namespace
{
thread_local DWORD t_lastError;
char s_commandLine[4096] = "BO1Zombies.exe";
char s_exeDir[MAX_PATH] = "/opfs/bo1/game";
LPTOP_LEVEL_EXCEPTION_FILTER s_exceptionFilter;
UINT s_errorMode;
int s_moduleAnchor;   // GetModuleHandleA(NULL) points here

int CoreCount()
{
    int n = emscripten_num_logical_cores();
    if (n < 2)
        n = 2;
    if (n > 8)
        n = 8;   // the engine's worker counts are tuned for <= 8 cores (Sys_GetDefaultWorkerThreadsCount)
    return n;
}

constexpr uint64_t FILETIME_UNIX_EPOCH = 116444736000000000ull;   // 1601-01-01 -> 1970-01-01 in 100 ns units

void FileTimeSplit(const FILETIME *ft, int64_t *sec, int *ms)
{
    const uint64_t v = ((uint64_t)ft->dwHighDateTime << 32) | ft->dwLowDateTime;
    const int64_t units = (int64_t)(v - FILETIME_UNIX_EPOCH);
    *sec = units / 10000000;
    *ms = (int)((units % 10000000) / 10000);
}

void ToSystemTime(const struct tm &tm, int ms, LPSYSTEMTIME st)
{
    st->wYear = (WORD)(tm.tm_year + 1900);
    st->wMonth = (WORD)(tm.tm_mon + 1);
    st->wDayOfWeek = (WORD)tm.tm_wday;
    st->wDay = (WORD)tm.tm_mday;
    st->wHour = (WORD)tm.tm_hour;
    st->wMinute = (WORD)tm.tm_min;
    st->wSecond = (WORD)tm.tm_sec;
    st->wMilliseconds = (WORD)ms;
}
}

uint64_t bo1w::NowNs()
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

uint64_t bo1w::UnixNsToFileTime(int64_t sec, int64_t nsec)
{
    return FILETIME_UNIX_EPOCH + (uint64_t)sec * 10000000ull + (uint64_t)(nsec / 100);
}

DWORD bo1w::ErrnoToWin32(int e)
{
    switch (e)
    {
    case 0:
        return ERROR_SUCCESS;
    case ENOENT:
        return ERROR_FILE_NOT_FOUND;
    case ENOTDIR:
        return ERROR_PATH_NOT_FOUND;
    case EACCES:
    case EPERM:
    case EISDIR:
    case EROFS:
        return ERROR_ACCESS_DENIED;
    case EEXIST:
        return ERROR_FILE_EXISTS;
    case EBADF:
        return ERROR_INVALID_HANDLE;
    case ENOMEM:
        return ERROR_NOT_ENOUGH_MEMORY;
    case ENOSPC:
        return ERROR_DISK_FULL;
    case EMFILE:
    case ENFILE:
        return ERROR_TOO_MANY_OPEN_FILES;
    case ENOTEMPTY:
        return ERROR_DIR_NOT_EMPTY;
    case EINVAL:
        return ERROR_INVALID_PARAMETER;
    case ENAMETOOLONG:
        return ERROR_INVALID_NAME;
    default:
        return ERROR_INVALID_FUNCTION;
    }
}

extern "C" {

// ----- web build glue -----
void bo1_web_set_command_line(const char *cmdline)
{
    snprintf(s_commandLine, sizeof(s_commandLine), "%s", cmdline ? cmdline : "");
}

void bo1_web_set_exe_dir(const char *dir)
{
    snprintf(s_exeDir, sizeof(s_exeDir), "%s", dir ? dir : "/");
}

// ----- errors / debug -----
DWORD GetLastError(void)
{
    return t_lastError;
}

void SetLastError(DWORD err)
{
    t_lastError = err;
}

void OutputDebugStringA(LPCSTR s)
{
    if (s)
        fputs(s, stderr);
}

BOOL IsDebuggerPresent(void)
{
    return FALSE;
}

LPTOP_LEVEL_EXCEPTION_FILTER SetUnhandledExceptionFilter(LPTOP_LEVEL_EXCEPTION_FILTER filter)
{
    LPTOP_LEVEL_EXCEPTION_FILTER previous = s_exceptionFilter;
    s_exceptionFilter = filter;
    return previous;
}

UINT SetErrorMode(UINT mode)
{
    const UINT previous = s_errorMode;
    s_errorMode = mode;
    return previous;
}

DWORD FormatMessageA(DWORD flags, LPCVOID source, DWORD messageId, DWORD languageId, LPSTR buffer, DWORD size, va_list *args)
{
    char text[128];
    if (flags & FORMAT_MESSAGE_FROM_STRING)
        snprintf(text, sizeof(text), "%s", source ? (const char *)source : "");
    else
        snprintf(text, sizeof(text), "Win32 error %lu (web build)", (unsigned long)messageId);
    const DWORD len = (DWORD)strlen(text);
    if (flags & FORMAT_MESSAGE_ALLOCATE_BUFFER)
    {
        char *p = (char *)LocalAlloc(0, len + 1);
        if (!p)
            return 0;
        memcpy(p, text, len + 1);
        *(char **)buffer = p;
        return len;
    }
    if (!buffer || size == 0)
        return 0;
    snprintf(buffer, size, "%s", text);
    return (DWORD)strlen(buffer);
}

// web: never blocks. Printed to the console; answers "no" to yes/no questions (keep the current settings) and "OK"
// otherwise.
int MessageBoxA(HWND hWnd, LPCSTR text, LPCSTR caption, UINT type)
{
    fprintf(stderr, "[MessageBox] %s: %s\n", caption ? caption : "", text ? text : "");
    switch (type & 0xF)
    {
    case MB_YESNO:
    case MB_YESNOCANCEL:
        return IDNO;
    case MB_ABORTRETRYIGNORE:
        return IDIGNORE;
    case MB_RETRYCANCEL:
        return IDCANCEL;
    default:
        return IDOK;
    }
}

void ExitProcess(UINT code)
{
    bo1_web_exit((int)code);
}

BOOL TerminateProcess(HANDLE process, UINT code)
{
    if ((uintptr_t)process == (uintptr_t)-1)
        bo1_web_exit((int)code);
    return FALSE;
}

// ----- system information -----
void GetSystemInfo(LPSYSTEM_INFO info)
{
    memset(info, 0, sizeof(*info));
    info->wProcessorArchitecture = PROCESSOR_ARCHITECTURE_INTEL;
    info->dwPageSize = 4096;
    info->lpMinimumApplicationAddress = (LPVOID)0x10000;
    info->lpMaximumApplicationAddress = (LPVOID)0xFFFEFFFF;
    const int n = CoreCount();
    info->dwActiveProcessorMask = (DWORD_PTR)((1u << n) - 1);
    info->dwNumberOfProcessors = (DWORD)n;
    info->dwProcessorType = PROCESSOR_INTEL_PENTIUM;
    info->dwAllocationGranularity = 65536;
    info->wProcessorLevel = 6;
}

void GetNativeSystemInfo(LPSYSTEM_INFO info)
{
    GetSystemInfo(info);
}

BOOL GetVersionExA(LPOSVERSIONINFOA info)
{
    if (!info)
        return FALSE;
    info->dwMajorVersion = 6;
    info->dwMinorVersion = 1;
    info->dwBuildNumber = 7601;
    info->dwPlatformId = VER_PLATFORM_WIN32_NT;
    snprintf(info->szCSDVersion, sizeof(info->szCSDVersion), "web");
    return TRUE;
}

DWORD GetVersion(void)
{
    return (7601u << 16) | (1u << 8) | 6u;
}

LPSTR GetCommandLineA(void)
{
    return s_commandLine;
}

DWORD GetEnvironmentVariableA(LPCSTR name, LPSTR buffer, DWORD size)
{
    const char *v = name ? getenv(name) : nullptr;
    if (!v)
    {
        SetLastError(ERROR_ENVVAR_NOT_FOUND);
        return 0;
    }
    const DWORD len = (DWORD)strlen(v);
    if (!buffer || size <= len)
        return len + 1;
    memcpy(buffer, v, len + 1);
    return len;
}

BOOL SetEnvironmentVariableA(LPCSTR name, LPCSTR value)
{
    if (!name)
        return FALSE;
    return (value ? setenv(name, value, 1) : unsetenv(name)) == 0;
}

BOOL GetUserNameA(LPSTR buffer, LPDWORD size)
{
    const char *name = "player";
    if (!buffer || !size || *size <= strlen(name))
    {
        if (size)
            *size = (DWORD)strlen(name) + 1;
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    strcpy(buffer, name);
    *size = (DWORD)strlen(name) + 1;
    return TRUE;
}

BOOL GetComputerNameA(LPSTR buffer, LPDWORD size)
{
    const char *name = "BROWSER";
    if (!buffer || !size || *size <= strlen(name))
    {
        if (size)
            *size = (DWORD)strlen(name) + 1;
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    strcpy(buffer, name);
    *size = (DWORD)strlen(name);
    return TRUE;
}

// web: en-US. The game language comes from localization.txt (win_localize.cpp), not from the OS locale.
LCID GetUserDefaultLCID(void)
{
    return 0x409;
}

LANGID GetUserDefaultLangID(void)
{
    return 0x409;
}

LANGID GetSystemDefaultLangID(void)
{
    return 0x409;
}

LANGID GetUserDefaultUILanguage(void)
{
    return 0x409;
}

LCID GetThreadLocale(void)
{
    return 0x409;
}

int GetLocaleInfoA(LCID locale, DWORD type, LPSTR data, int size)
{
    const char *v;
    switch (type & 0xFFFF)
    {
    case LOCALE_SDECIMAL:
        v = ".";
        break;
    case LOCALE_STHOUSAND:
        v = ",";
        break;
    case LOCALE_SENGLANGUAGE:
        v = "English";
        break;
    case LOCALE_SISO639LANGNAME:
        v = "en";
        break;
    case LOCALE_SISO3166CTRYNAME:
        v = "US";
        break;
    case LOCALE_ILANGUAGE:
        v = "0409";
        break;
    default:
        v = "";
        break;
    }
    const int len = (int)strlen(v) + 1;
    if (!data || size == 0)
        return len;
    if (size < len)
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    memcpy(data, v, (size_t)len);
    return len;
}

// CP_UTF8: UTF-8; every other code page: Latin-1 (cp1252's printable range below 0x80 and 0xA0..0xFF match)
int MultiByteToWideChar(UINT codePage, DWORD flags, LPCSTR src, int srcLen, LPWSTR dst, int dstLen)
{
    if (!src)
        return 0;
    if (srcLen < 0)
        srcLen = (int)strlen(src) + 1;
    int n = 0;
    const unsigned char *s = (const unsigned char *)src;
    const unsigned char *end = s + srcLen;
    while (s < end)
    {
        uint32_t c = *s++;
        if (codePage == CP_UTF8 && c >= 0x80)
        {
            int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
            c &= extra == 3 ? 0x07 : extra == 2 ? 0x0F : 0x1F;
            while (extra-- && s < end)
                c = (c << 6) | (*s++ & 0x3F);
        }
        if (dstLen)
        {
            if (n >= dstLen)
            {
                SetLastError(ERROR_INSUFFICIENT_BUFFER);
                return 0;
            }
            dst[n] = (WCHAR)c;
        }
        ++n;
    }
    return n;
}

int WideCharToMultiByte(UINT codePage, DWORD flags, LPCWSTR src, int srcLen, LPSTR dst, int dstLen, LPCSTR defaultChar, LPBOOL usedDefault)
{
    if (!src)
        return 0;
    if (srcLen < 0)
        srcLen = (int)wcslen(src) + 1;
    int n = 0;
    if (usedDefault)
        *usedDefault = FALSE;
    for (int i = 0; i < srcLen; ++i)
    {
        uint32_t c = (uint32_t)src[i];
        char buf[4];
        int len = 0;
        if (codePage == CP_UTF8)
        {
            if (c < 0x80)
                buf[len++] = (char)c;
            else if (c < 0x800)
            {
                buf[len++] = (char)(0xC0 | (c >> 6));
                buf[len++] = (char)(0x80 | (c & 0x3F));
            }
            else if (c < 0x10000)
            {
                buf[len++] = (char)(0xE0 | (c >> 12));
                buf[len++] = (char)(0x80 | ((c >> 6) & 0x3F));
                buf[len++] = (char)(0x80 | (c & 0x3F));
            }
            else
            {
                buf[len++] = (char)(0xF0 | (c >> 18));
                buf[len++] = (char)(0x80 | ((c >> 12) & 0x3F));
                buf[len++] = (char)(0x80 | ((c >> 6) & 0x3F));
                buf[len++] = (char)(0x80 | (c & 0x3F));
            }
        }
        else if (c < 0x100)
            buf[len++] = (char)c;
        else
        {
            buf[len++] = defaultChar ? *defaultChar : '?';
            if (usedDefault)
                *usedDefault = TRUE;
        }
        if (dstLen)
        {
            if (n + len > dstLen)
            {
                SetLastError(ERROR_INSUFFICIENT_BUFFER);
                return 0;
            }
            memcpy(dst + n, buf, (size_t)len);
        }
        n += len;
    }
    return n;
}

// ----- modules: no DLLs on the web -----
HMODULE LoadLibraryA(LPCSTR name)
{
    SetLastError(ERROR_MOD_NOT_FOUND);
    return nullptr;
}

HMODULE LoadLibraryExA(LPCSTR name, HANDLE file, DWORD flags)
{
    return LoadLibraryA(name);
}

FARPROC GetProcAddress(HMODULE module, LPCSTR name)
{
    SetLastError(ERROR_PROC_NOT_FOUND);
    return nullptr;
}

BOOL FreeLibrary(HMODULE module)
{
    return TRUE;
}

HMODULE GetModuleHandleA(LPCSTR name)
{
    if (!name)
        return (HMODULE)&s_moduleAnchor;
    SetLastError(ERROR_MOD_NOT_FOUND);
    return nullptr;
}

BOOL GetModuleHandleExA(DWORD flags, LPCSTR name, HMODULE *module)
{
    if (module)
        *module = nullptr;
    SetLastError(ERROR_MOD_NOT_FOUND);
    return FALSE;
}

DWORD GetModuleFileNameA(HMODULE module, LPSTR name, DWORD size)
{
    if (!name || !size)
        return 0;
    char path[MAX_PATH + 32];
    snprintf(path, sizeof(path), "%s/BO1Zombies.exe", s_exeDir);
    const DWORD len = (DWORD)strlen(path);
    if (len >= size)
    {
        memcpy(name, path, size - 1);
        name[size - 1] = 0;
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return size;
    }
    memcpy(name, path, len + 1);
    return len;
}

HANDLE GetStdHandle(DWORD which)
{
    return INVALID_HANDLE_VALUE;
}

BOOL SetConsoleTitleA(LPCSTR title)
{
    return TRUE;
}

BOOL AllocConsole(void)
{
    return FALSE;
}

BOOL FreeConsole(void)
{
    return TRUE;
}

DWORD SetThreadExecutionState(DWORD flags)
{
    return ES_CONTINUOUS;
}

BOOL ShellExecuteSucceeded(void)
{
    return FALSE;
}

HINSTANCE ShellExecuteA(HWND hwnd, LPCSTR op, LPCSTR file, LPCSTR params, LPCSTR dir, INT show)
{
    fprintf(stderr, "[ShellExecute] %s %s (not available in the browser)\n", op ? op : "", file ? file : "");
    return (HINSTANCE)(uintptr_t)2;   // <= 32: failure (file not found)
}

BOOL CreateProcessA(LPCSTR app, LPSTR cmdline, LPSECURITY_ATTRIBUTES pa, LPSECURITY_ATTRIBUTES ta, BOOL inherit, DWORD flags,
    LPVOID env, LPCSTR dir, LPSTARTUPINFOA si, LPPROCESS_INFORMATION pi)
{
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

HANDLE OpenProcess(DWORD access, BOOL inherit, DWORD pid)
{
    SetLastError(ERROR_INVALID_PARAMETER);
    return nullptr;
}

BOOL GetExitCodeProcess(HANDLE process, LPDWORD code)
{
    if (code)
        *code = STILL_ACTIVE;
    return TRUE;
}

BOOL SetPriorityClass(HANDLE process, DWORD cls)
{
    return TRUE;
}

DWORD GetPriorityClass(HANDLE process)
{
    return NORMAL_PRIORITY_CLASS;
}

BOOL SetProcessAffinityMask(HANDLE process, DWORD_PTR mask)
{
    return TRUE;
}

BOOL GetProcessAffinityMask(HANDLE process, PDWORD_PTR processMask, PDWORD_PTR systemMask)
{
    const DWORD_PTR mask = (DWORD_PTR)((1u << CoreCount()) - 1);
    if (processMask)
        *processMask = mask;
    if (systemMask)
        *systemMask = mask;
    return TRUE;
}

BOOL OpenProcessToken(HANDLE process, DWORD access, PHANDLE token)
{
    return FALSE;
}

BOOL LookupPrivilegeValueA(LPCSTR system, LPCSTR name, PLUID luid)
{
    return FALSE;
}

BOOL AdjustTokenPrivileges(HANDLE token, BOOL disableAll, PTOKEN_PRIVILEGES newState, DWORD len, PTOKEN_PRIVILEGES prev, PDWORD retLen)
{
    return FALSE;
}

BOOL IsProcessDPIAware(void)
{
    return TRUE;
}

BOOL SetProcessDPIAware(void)
{
    return TRUE;
}

BOOL GetProcessHandleCount(HANDLE process, PDWORD count)
{
    if (count)
        *count = 64;
    return TRUE;
}

static void NsToFileTimeSpan(uint64_t ns, LPFILETIME ft)
{
    if (!ft)
        return;
    const uint64_t v = ns / 100;
    ft->dwLowDateTime = (DWORD)v;
    ft->dwHighDateTime = (DWORD)(v >> 32);
}

BOOL GetSystemTimes(LPFILETIME idle, LPFILETIME kernel, LPFILETIME user)
{
    const uint64_t now = bo1w::NowNs();
    NsToFileTimeSpan(0, idle);
    NsToFileTimeSpan(0, kernel);
    NsToFileTimeSpan(now, user);
    return TRUE;
}

BOOL GetThreadTimes(HANDLE thread, LPFILETIME creation, LPFILETIME exit, LPFILETIME kernel, LPFILETIME user)
{
    NsToFileTimeSpan(0, creation);
    NsToFileTimeSpan(0, exit);
    NsToFileTimeSpan(0, kernel);
    NsToFileTimeSpan(bo1w::NowNs(), user);
    return TRUE;
}

BOOL GetProcessTimes(HANDLE process, LPFILETIME creation, LPFILETIME exit, LPFILETIME kernel, LPFILETIME user)
{
    return GetThreadTimes(nullptr, creation, exit, kernel, user);
}

BOOL GetSystemCpuSetInformation(PSYSTEM_CPU_SET_INFORMATION info, ULONG length, PULONG returned, HANDLE process, ULONG flags)
{
    if (returned)
        *returned = 0;
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

BOOL GetLogicalProcessorInformation(PSYSTEM_LOGICAL_PROCESSOR_INFORMATION buffer, PDWORD length)
{
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

// ----- time -----
BOOL QueryPerformanceCounter(LARGE_INTEGER *count)
{
    count->QuadPart = (LONGLONG)(bo1w::NowNs() / 1000);
    return TRUE;
}

BOOL QueryPerformanceFrequency(LARGE_INTEGER *freq)
{
    freq->QuadPart = 1000000;
    return TRUE;
}

DWORD GetTickCount(void)
{
    return (DWORD)(bo1w::NowNs() / 1000000);
}

ULONGLONG GetTickCount64(void)
{
    return bo1w::NowNs() / 1000000;
}

DWORD timeGetTime(void)
{
    return (DWORD)(bo1w::NowNs() / 1000000);
}

UINT timeBeginPeriod(UINT period)
{
    return TIMERR_NOERROR;
}

UINT timeEndPeriod(UINT period)
{
    return TIMERR_NOERROR;
}

void GetSystemTime(LPSYSTEMTIME st)
{
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    struct tm tm;
    const time_t t = tv.tv_sec;
    gmtime_r(&t, &tm);
    ToSystemTime(tm, (int)(tv.tv_usec / 1000), st);
}

void GetLocalTime(LPSYSTEMTIME st)
{
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    struct tm tm;
    const time_t t = tv.tv_sec;
    localtime_r(&t, &tm);
    ToSystemTime(tm, (int)(tv.tv_usec / 1000), st);
}

void GetSystemTimeAsFileTime(LPFILETIME ft)
{
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    const uint64_t v = bo1w::UnixNsToFileTime(tv.tv_sec, (int64_t)tv.tv_usec * 1000);
    ft->dwLowDateTime = (DWORD)v;
    ft->dwHighDateTime = (DWORD)(v >> 32);
}

BOOL FileTimeToSystemTime(const FILETIME *ft, LPSYSTEMTIME st)
{
    int64_t sec;
    int ms;
    FileTimeSplit(ft, &sec, &ms);
    struct tm tm;
    const time_t t = (time_t)sec;
    if (!gmtime_r(&t, &tm))
        return FALSE;
    ToSystemTime(tm, ms, st);
    return TRUE;
}

BOOL SystemTimeToFileTime(const SYSTEMTIME *st, LPFILETIME ft)
{
    struct tm tm = {};
    tm.tm_year = st->wYear - 1900;
    tm.tm_mon = st->wMonth - 1;
    tm.tm_mday = st->wDay;
    tm.tm_hour = st->wHour;
    tm.tm_min = st->wMinute;
    tm.tm_sec = st->wSecond;
    const time_t t = timegm(&tm);
    const uint64_t v = bo1w::UnixNsToFileTime(t, (int64_t)st->wMilliseconds * 1000000);
    ft->dwLowDateTime = (DWORD)v;
    ft->dwHighDateTime = (DWORD)(v >> 32);
    return TRUE;
}

static int64_t LocalOffsetSeconds()
{
    const time_t now = time(nullptr);
    struct tm tm;
    localtime_r(&now, &tm);
    return tm.tm_gmtoff;
}

BOOL FileTimeToLocalFileTime(const FILETIME *ft, LPFILETIME local)
{
    uint64_t v = ((uint64_t)ft->dwHighDateTime << 32) | ft->dwLowDateTime;
    v += (uint64_t)(LocalOffsetSeconds() * 10000000);
    local->dwLowDateTime = (DWORD)v;
    local->dwHighDateTime = (DWORD)(v >> 32);
    return TRUE;
}

BOOL LocalFileTimeToFileTime(const FILETIME *local, LPFILETIME ft)
{
    uint64_t v = ((uint64_t)local->dwHighDateTime << 32) | local->dwLowDateTime;
    v -= (uint64_t)(LocalOffsetSeconds() * 10000000);
    ft->dwLowDateTime = (DWORD)v;
    ft->dwHighDateTime = (DWORD)(v >> 32);
    return TRUE;
}

LONG CompareFileTime(const FILETIME *a, const FILETIME *b)
{
    const uint64_t x = ((uint64_t)a->dwHighDateTime << 32) | a->dwLowDateTime;
    const uint64_t y = ((uint64_t)b->dwHighDateTime << 32) | b->dwLowDateTime;
    return x < y ? -1 : x > y ? 1 : 0;
}

DWORD GetTimeZoneInformation(LPTIME_ZONE_INFORMATION tz)
{
    memset(tz, 0, sizeof(*tz));
    tz->Bias = (LONG)(-LocalOffsetSeconds() / 60);
    return 0;   // TIME_ZONE_ID_UNKNOWN
}

// ----- Toolhelp: no process / thread / module snapshots -----
HANDLE CreateToolhelp32Snapshot(DWORD flags, DWORD pid)
{
    SetLastError(ERROR_NOT_SUPPORTED);
    return INVALID_HANDLE_VALUE;
}

BOOL Module32First(HANDLE snapshot, LPMODULEENTRY32 me)
{
    return FALSE;
}

BOOL Module32Next(HANDLE snapshot, LPMODULEENTRY32 me)
{
    return FALSE;
}

BOOL Thread32First(HANDLE snapshot, LPTHREADENTRY32 te)
{
    return FALSE;
}

BOOL Thread32Next(HANDLE snapshot, LPTHREADENTRY32 te)
{
    return FALSE;
}

BOOL Process32First(HANDLE snapshot, LPPROCESSENTRY32 pe)
{
    return FALSE;
}

BOOL Process32Next(HANDLE snapshot, LPPROCESSENTRY32 pe)
{
    return FALSE;
}

// ----- Winsock: no sockets (web_net.cpp carries the engine's packets) -----
static thread_local int t_wsaError;

int WSAStartup(WORD version, LPWSADATA data)
{
    if (data)
    {
        memset(data, 0, sizeof(*data));
        data->wVersion = version;
        data->wHighVersion = 0x0202;
        snprintf(data->szDescription, sizeof(data->szDescription), "web build (no sockets)");
    }
    return 0;
}

int WSACleanup(void)
{
    return 0;
}

int WSAGetLastError(void)
{
    return t_wsaError ? t_wsaError : WSAENETDOWN;
}

void WSASetLastError(int err)
{
    t_wsaError = err;
}

int closesocket(SOCKET s)
{
    return 0;
}

int ioctlsocket(SOCKET s, long cmd, unsigned long *arg)
{
    t_wsaError = WSAENETDOWN;
    return SOCKET_ERROR;
}

} // extern "C"
