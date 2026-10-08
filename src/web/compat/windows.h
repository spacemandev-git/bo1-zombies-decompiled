// windows.h - Win32 API emulation for the web build (src/web/compat/, first on the include path; cmake/web.cmake).
//
// The engine's shared code (threads.cpp, db_file_load.cpp, jobqueue, com_memory.cpp, ...) is compiled unchanged against
// this header. It declares the types, constants and functions of the Win32 subset the engine uses; the functions are
// implemented over pthreads / musl / Emscripten in src/web/compat/*.cpp:
//
//   win_sync.cpp     critical sections, events, mutexes, semaphores, waits, threads, TLS, APCs (ReadFileEx)
//   win_file.cpp     CreateFileA / ReadFile(Ex) / FindFirstFileA / GetFileAttributesA ... (case-insensitive paths)
//   win_memory.cpp   VirtualAlloc / VirtualFree / VirtualQuery, Global*, Heap*, GlobalMemoryStatus
//   win_misc.cpp     time, errors, system info, strings, process, CRT extras
//   win_gui.cpp      windows, messages, cursor, clipboard, monitors: stubs (the window is the page's canvas)
//
// Differences from Windows are documented at each declaration ("web:").
#pragma once
#ifndef BO1_WEB_WINDOWS_H
#define BO1_WEB_WINDOWS_H

#include "bo1_web_prelude.h"
#include <wchar.h>
#include "sal.h"

// ===================================================================================================================
// Base types (x86 Win32: long is 32-bit, as on wasm32)
// ===================================================================================================================
#define WINAPI
#define WINAPIV
#define APIENTRY
#define CALLBACK
#define PASCAL
#define NTAPI
#define WINBASEAPI
#define WINUSERAPI
#define WINGDIAPI
#define WINMMAPI
#define DECLSPEC_IMPORT
#define DECLSPEC_NORETURN __attribute__((noreturn))
#define DECLSPEC_ALIGN(x) __attribute__((aligned(x)))
#define DECLSPEC_NOVTABLE
#define DECLSPEC_SELECTANY __attribute__((weak))
#define FORCEINLINE inline __attribute__((always_inline))
#define CONST const
#define VOID void
#define NEAR
#define FAR
#define far
#define near
#ifndef IN
#define IN
#endif
#ifndef OUT
#define OUT
#endif
#ifndef OPTIONAL
#define OPTIONAL
#endif
#ifndef UNREFERENCED_PARAMETER
#define UNREFERENCED_PARAMETER(p) ((void)(p))
#endif

typedef int BOOL;
typedef unsigned char BOOLEAN;
typedef unsigned char BYTE;
typedef char CHAR;
typedef signed char INT8;
typedef unsigned char UCHAR, UINT8;
typedef unsigned short WORD, USHORT, UINT16;
typedef short SHORT, INT16;
typedef unsigned long DWORD, ULONG;
typedef long LONG;
typedef int INT, INT32;
typedef unsigned int UINT, UINT32;
typedef float FLOAT;
typedef double DOUBLE;
typedef long long LONGLONG, INT64, LONG64;
typedef unsigned long long ULONGLONG, UINT64, DWORD64, DWORDLONG, ULONG64;
typedef int INT_PTR, *PINT_PTR;
typedef unsigned int UINT_PTR, *PUINT_PTR;
typedef long LONG_PTR, *PLONG_PTR;
typedef unsigned long ULONG_PTR, *PULONG_PTR;
typedef ULONG_PTR DWORD_PTR, *PDWORD_PTR;
typedef ULONG_PTR SIZE_T, *PSIZE_T;
typedef LONG_PTR SSIZE_T, *PSSIZE_T;
typedef wchar_t WCHAR;            // web: 32-bit (Windows: 16-bit); only used with the conversion functions below
typedef char TCHAR;
typedef unsigned char TBYTE;
typedef void *PVOID, *LPVOID;
typedef const void *LPCVOID, *PCVOID;
typedef void *HANDLE, **PHANDLE, **LPHANDLE;
typedef char *LPSTR, *PSTR, *LPTSTR, *PTSTR;
typedef const char *LPCSTR, *PCSTR, *LPCTSTR, *PCTSTR;
typedef WCHAR *LPWSTR, *PWSTR;
typedef const WCHAR *LPCWSTR, *PCWSTR;
typedef BYTE *PBYTE, *LPBYTE;
typedef BOOL *PBOOL, *LPBOOL;
typedef WORD *PWORD, *LPWORD;
typedef DWORD *PDWORD, *LPDWORD;
typedef LONG *PLONG, *LPLONG;
typedef ULONG *PULONG;
typedef UINT *PUINT, *LPUINT;
typedef INT *PINT, *LPINT;
typedef SHORT *PSHORT;
typedef USHORT *PUSHORT;
typedef UCHAR *PUCHAR;
typedef CHAR *PCHAR;
typedef FLOAT *PFLOAT;
typedef LONGLONG *PLONGLONG;
typedef ULONGLONG *PULONGLONG;
typedef DWORD64 *PDWORD64;
typedef ULONG64 *PULONG64;
typedef LONG64 *PLONG64;
typedef long HRESULT;
typedef UINT_PTR WPARAM;
typedef LONG_PTR LPARAM;
typedef LONG_PTR LRESULT;
typedef WORD ATOM;
typedef DWORD COLORREF, *LPCOLORREF;
typedef DWORD LCID;
typedef WORD LANGID;
typedef DWORD ACCESS_MASK;
typedef PVOID PSID;
typedef BYTE FCHAR;
typedef WORD FSHORT;
typedef DWORD FLONG;

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef NULL
#define NULL 0
#endif

#define DECLARE_HANDLE(name) struct name##__ { int unused; }; typedef struct name##__ *name
DECLARE_HANDLE(HWND);
DECLARE_HANDLE(HINSTANCE);
DECLARE_HANDLE(HDC);
DECLARE_HANDLE(HICON);
DECLARE_HANDLE(HBRUSH);
DECLARE_HANDLE(HFONT);
DECLARE_HANDLE(HMENU);
DECLARE_HANDLE(HMONITOR);
DECLARE_HANDLE(HKEY);
DECLARE_HANDLE(HBITMAP);
DECLARE_HANDLE(HPEN);
DECLARE_HANDLE(HRGN);
DECLARE_HANDLE(HPALETTE);
DECLARE_HANDLE(HKL);
DECLARE_HANDLE(HHOOK);
DECLARE_HANDLE(HGLRC);
DECLARE_HANDLE(HRAWINPUT);
DECLARE_HANDLE(HDESK);
DECLARE_HANDLE(HWINSTA);
DECLARE_HANDLE(HWINEVENTHOOK);
DECLARE_HANDLE(HTASK);
DECLARE_HANDLE(HDROP);
DECLARE_HANDLE(DPI_AWARENESS_CONTEXT);
typedef HINSTANCE HMODULE;
typedef HICON HCURSOR;
typedef HANDLE HGLOBAL, HLOCAL, HGDIOBJ, HRSRC, GLOBALHANDLE, LOCALHANDLE;
typedef HKEY *PHKEY;
typedef int HFILE;
#define HFILE_ERROR ((HFILE)-1)

// ===================================================================================================================
// Structures
// ===================================================================================================================
typedef union _LARGE_INTEGER {
    struct { DWORD LowPart; LONG HighPart; };
    struct { DWORD LowPart; LONG HighPart; } u;
    LONGLONG QuadPart;
} LARGE_INTEGER, *PLARGE_INTEGER;
typedef union _ULARGE_INTEGER {
    struct { DWORD LowPart; DWORD HighPart; };
    struct { DWORD LowPart; DWORD HighPart; } u;
    ULONGLONG QuadPart;
} ULARGE_INTEGER, *PULARGE_INTEGER;

typedef struct _LUID { DWORD LowPart; LONG HighPart; } LUID, *PLUID;

typedef struct tagRECT { LONG left, top, right, bottom; } RECT, *PRECT, *LPRECT, *NPRECT;
typedef const RECT *LPCRECT;
typedef struct _RECTL { LONG left, top, right, bottom; } RECTL, *PRECTL, *LPRECTL;
typedef struct tagPOINT { LONG x, y; } POINT, *PPOINT, *LPPOINT;
typedef struct _POINTL { LONG x, y; } POINTL, *PPOINTL;
typedef struct tagPOINTS { SHORT x, y; } POINTS, *PPOINTS;
typedef struct tagSIZE { LONG cx, cy; } SIZE, *PSIZE, *LPSIZE;
typedef SIZE SIZEL;

typedef struct tagMSG {
    HWND hwnd;
    UINT message;
    WPARAM wParam;
    LPARAM lParam;
    DWORD time;
    POINT pt;
} MSG, *PMSG, *LPMSG;

typedef LRESULT(CALLBACK *WNDPROC)(HWND, UINT, WPARAM, LPARAM);
typedef INT_PTR(CALLBACK *DLGPROC)(HWND, UINT, WPARAM, LPARAM);
typedef LRESULT(CALLBACK *HOOKPROC)(int, WPARAM, LPARAM);
typedef INT_PTR(WINAPI *FARPROC)();
typedef INT_PTR(WINAPI *NEARPROC)();
typedef INT_PTR(WINAPI *PROC)();

typedef struct tagWNDCLASSA {
    UINT style;
    WNDPROC lpfnWndProc;
    int cbClsExtra;
    int cbWndExtra;
    HINSTANCE hInstance;
    HICON hIcon;
    HCURSOR hCursor;
    HBRUSH hbrBackground;
    LPCSTR lpszMenuName;
    LPCSTR lpszClassName;
} WNDCLASSA, *PWNDCLASSA, *LPWNDCLASSA, WNDCLASS;
typedef struct tagWNDCLASSEXA {
    UINT cbSize;
    UINT style;
    WNDPROC lpfnWndProc;
    int cbClsExtra;
    int cbWndExtra;
    HINSTANCE hInstance;
    HICON hIcon;
    HCURSOR hCursor;
    HBRUSH hbrBackground;
    LPCSTR lpszMenuName;
    LPCSTR lpszClassName;
    HICON hIconSm;
} WNDCLASSEXA, *PWNDCLASSEXA, *LPWNDCLASSEXA, WNDCLASSEX;

typedef struct tagCREATESTRUCTA {
    LPVOID lpCreateParams;
    HINSTANCE hInstance;
    HMENU hMenu;
    HWND hwndParent;
    int cy, cx, y, x;
    LONG style;
    LPCSTR lpszName;
    LPCSTR lpszClass;
    DWORD dwExStyle;
} CREATESTRUCTA, *LPCREATESTRUCTA, CREATESTRUCT, *LPCREATESTRUCT;

typedef struct tagMINMAXINFO { POINT ptReserved, ptMaxSize, ptMaxPosition, ptMinTrackSize, ptMaxTrackSize; } MINMAXINFO, *LPMINMAXINFO;

typedef struct tagWINDOWPLACEMENT {
    UINT length;
    UINT flags;
    UINT showCmd;
    POINT ptMinPosition;
    POINT ptMaxPosition;
    RECT rcNormalPosition;
} WINDOWPLACEMENT, *PWINDOWPLACEMENT, *LPWINDOWPLACEMENT;

typedef struct tagWINDOWPOS { HWND hwnd, hwndInsertAfter; int x, y, cx, cy; UINT flags; } WINDOWPOS, *LPWINDOWPOS, *PWINDOWPOS;

typedef struct tagPAINTSTRUCT {
    HDC hdc;
    BOOL fErase;
    RECT rcPaint;
    BOOL fRestore;
    BOOL fIncUpdate;
    BYTE rgbReserved[32];
} PAINTSTRUCT, *LPPAINTSTRUCT;

typedef struct tagMONITORINFO {
    DWORD cbSize;
    RECT rcMonitor;
    RECT rcWork;
    DWORD dwFlags;
} MONITORINFO, *LPMONITORINFO;
#ifdef __cplusplus
typedef struct tagMONITORINFOEXA : public tagMONITORINFO {
    CHAR szDevice[32];
} MONITORINFOEXA, *LPMONITORINFOEXA, MONITORINFOEX;
#else
typedef struct tagMONITORINFOEXA {
    DWORD cbSize;
    RECT rcMonitor;
    RECT rcWork;
    DWORD dwFlags;
    CHAR szDevice[32];
} MONITORINFOEXA, *LPMONITORINFOEXA, MONITORINFOEX;
#endif
typedef BOOL(CALLBACK *MONITORENUMPROC)(HMONITOR, HDC, LPRECT, LPARAM);
typedef BOOL(CALLBACK *WNDENUMPROC)(HWND, LPARAM);

typedef struct _devicemodeA {
    BYTE dmDeviceName[32];
    WORD dmSpecVersion;
    WORD dmDriverVersion;
    WORD dmSize;
    WORD dmDriverExtra;
    DWORD dmFields;
    union {
        struct { short dmOrientation, dmPaperSize, dmPaperLength, dmPaperWidth, dmScale, dmCopies, dmDefaultSource, dmPrintQuality; };
        struct { POINTL dmPosition; DWORD dmDisplayOrientation; DWORD dmDisplayFixedOutput; };
    };
    short dmColor;
    short dmDuplex;
    short dmYResolution;
    short dmTTOption;
    short dmCollate;
    BYTE dmFormName[32];
    WORD dmLogPixels;
    DWORD dmBitsPerPel;
    DWORD dmPelsWidth;
    DWORD dmPelsHeight;
    union { DWORD dmDisplayFlags; DWORD dmNup; };
    DWORD dmDisplayFrequency;
    DWORD dmICMMethod, dmICMIntent, dmMediaType, dmDitherType, dmReserved1, dmReserved2, dmPanningWidth, dmPanningHeight;
} DEVMODEA, *PDEVMODEA, *LPDEVMODEA, DEVMODE, *LPDEVMODE;

typedef struct _DISPLAY_DEVICEA {
    DWORD cb;
    CHAR DeviceName[32];
    CHAR DeviceString[128];
    DWORD StateFlags;
    CHAR DeviceID[128];
    CHAR DeviceKey[128];
} DISPLAY_DEVICEA, *PDISPLAY_DEVICEA, *LPDISPLAY_DEVICEA, DISPLAY_DEVICE;

typedef struct tagPALETTEENTRY { BYTE peRed, peGreen, peBlue, peFlags; } PALETTEENTRY, *PPALETTEENTRY, *LPPALETTEENTRY;
typedef struct _RGNDATAHEADER { DWORD dwSize, iType, nCount, nRgnSize; RECT rcBound; } RGNDATAHEADER, *PRGNDATAHEADER;
typedef struct _RGNDATA { RGNDATAHEADER rdh; char Buffer[1]; } RGNDATA, *PRGNDATA, *LPRGNDATA;
typedef struct tagRGBQUAD { BYTE rgbBlue, rgbGreen, rgbRed, rgbReserved; } RGBQUAD;
typedef struct tagBITMAPINFOHEADER {
    DWORD biSize;
    LONG biWidth;
    LONG biHeight;
    WORD biPlanes;
    WORD biBitCount;
    DWORD biCompression;
    DWORD biSizeImage;
    LONG biXPelsPerMeter;
    LONG biYPelsPerMeter;
    DWORD biClrUsed;
    DWORD biClrImportant;
} BITMAPINFOHEADER, *PBITMAPINFOHEADER, *LPBITMAPINFOHEADER;
typedef struct tagBITMAPINFO { BITMAPINFOHEADER bmiHeader; RGBQUAD bmiColors[1]; } BITMAPINFO, *PBITMAPINFO, *LPBITMAPINFO;
#pragma pack(push, 2)
typedef struct tagBITMAPFILEHEADER { WORD bfType; DWORD bfSize; WORD bfReserved1, bfReserved2; DWORD bfOffBits; } BITMAPFILEHEADER;
#pragma pack(pop)
typedef struct tagLOGFONTA {
    LONG lfHeight, lfWidth, lfEscapement, lfOrientation, lfWeight;
    BYTE lfItalic, lfUnderline, lfStrikeOut, lfCharSet, lfOutPrecision, lfClipPrecision, lfQuality, lfPitchAndFamily;
    CHAR lfFaceName[32];
} LOGFONTA, *LPLOGFONTA, LOGFONT;
typedef struct tagTEXTMETRICA {
    LONG tmHeight, tmAscent, tmDescent, tmInternalLeading, tmExternalLeading, tmAveCharWidth, tmMaxCharWidth, tmWeight,
        tmOverhang, tmDigitizedAspectX, tmDigitizedAspectY;
    BYTE tmFirstChar, tmLastChar, tmDefaultChar, tmBreakChar, tmItalic, tmUnderlined, tmStruckOut, tmPitchAndFamily, tmCharSet;
} TEXTMETRICA, TEXTMETRIC;

typedef struct _FILETIME { DWORD dwLowDateTime; DWORD dwHighDateTime; } FILETIME, *PFILETIME, *LPFILETIME;
typedef struct _SYSTEMTIME {
    WORD wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds;
} SYSTEMTIME, *PSYSTEMTIME, *LPSYSTEMTIME;
typedef struct _TIME_ZONE_INFORMATION {
    LONG Bias;
    WCHAR StandardName[32];
    SYSTEMTIME StandardDate;
    LONG StandardBias;
    WCHAR DaylightName[32];
    SYSTEMTIME DaylightDate;
    LONG DaylightBias;
} TIME_ZONE_INFORMATION, *LPTIME_ZONE_INFORMATION;

typedef struct _OVERLAPPED {
    ULONG_PTR Internal;
    ULONG_PTR InternalHigh;
    union {
        struct { DWORD Offset; DWORD OffsetHigh; };
        PVOID Pointer;
    };
    HANDLE hEvent;
} OVERLAPPED, *LPOVERLAPPED;
typedef void(CALLBACK *LPOVERLAPPED_COMPLETION_ROUTINE)(DWORD dwErrorCode, DWORD dwNumberOfBytesTransfered, LPOVERLAPPED lpOverlapped);
typedef void(CALLBACK *PAPCFUNC)(ULONG_PTR);

typedef struct _SECURITY_ATTRIBUTES { DWORD nLength; LPVOID lpSecurityDescriptor; BOOL bInheritHandle; } SECURITY_ATTRIBUTES, *PSECURITY_ATTRIBUTES, *LPSECURITY_ATTRIBUTES;

// web: a recursive pthread mutex (win_sync.cpp). The Win32 fields are kept so code that reads them compiles; only
// LockCount/RecursionCount/OwningThread are maintained (diagnostics).
typedef struct _RTL_CRITICAL_SECTION {
    void *DebugInfo;
    LONG LockCount;
    LONG RecursionCount;
    HANDLE OwningThread;
    HANDLE LockSemaphore;
    ULONG_PTR SpinCount;
    unsigned int bo1_mutex[16];   // pthread_mutex_t storage (Emscripten's is 24-28 bytes; checked in win_sync.cpp)
} RTL_CRITICAL_SECTION, *PRTL_CRITICAL_SECTION, CRITICAL_SECTION, *PCRITICAL_SECTION, *LPCRITICAL_SECTION;
typedef struct _RTL_SRWLOCK { PVOID Ptr; } RTL_SRWLOCK, SRWLOCK, *PSRWLOCK;
typedef struct _RTL_CONDITION_VARIABLE { PVOID Ptr; } RTL_CONDITION_VARIABLE, CONDITION_VARIABLE, *PCONDITION_VARIABLE;
#define SRWLOCK_INIT { 0 }

typedef DWORD(WINAPI *PTHREAD_START_ROUTINE)(LPVOID lpThreadParameter);
typedef PTHREAD_START_ROUTINE LPTHREAD_START_ROUTINE;

typedef struct _WIN32_FIND_DATAA {
    DWORD dwFileAttributes;
    FILETIME ftCreationTime;
    FILETIME ftLastAccessTime;
    FILETIME ftLastWriteTime;
    DWORD nFileSizeHigh;
    DWORD nFileSizeLow;
    DWORD dwReserved0;
    DWORD dwReserved1;
    CHAR cFileName[260];
    CHAR cAlternateFileName[14];
} WIN32_FIND_DATAA, *PWIN32_FIND_DATAA, *LPWIN32_FIND_DATAA, WIN32_FIND_DATA, *LPWIN32_FIND_DATA;
typedef struct _WIN32_FILE_ATTRIBUTE_DATA {
    DWORD dwFileAttributes;
    FILETIME ftCreationTime;
    FILETIME ftLastAccessTime;
    FILETIME ftLastWriteTime;
    DWORD nFileSizeHigh;
    DWORD nFileSizeLow;
} WIN32_FILE_ATTRIBUTE_DATA, *LPWIN32_FILE_ATTRIBUTE_DATA;
typedef enum _GET_FILEEX_INFO_LEVELS { GetFileExInfoStandard, GetFileExMaxInfoLevel } GET_FILEEX_INFO_LEVELS;
typedef struct _BY_HANDLE_FILE_INFORMATION {
    DWORD dwFileAttributes;
    FILETIME ftCreationTime, ftLastAccessTime, ftLastWriteTime;
    DWORD dwVolumeSerialNumber, nFileSizeHigh, nFileSizeLow, nNumberOfLinks, nFileIndexHigh, nFileIndexLow;
} BY_HANDLE_FILE_INFORMATION, *LPBY_HANDLE_FILE_INFORMATION;

typedef struct _MEMORY_BASIC_INFORMATION {
    PVOID BaseAddress;
    PVOID AllocationBase;
    DWORD AllocationProtect;
    SIZE_T RegionSize;
    DWORD State;
    DWORD Protect;
    DWORD Type;
} MEMORY_BASIC_INFORMATION, *PMEMORY_BASIC_INFORMATION;
typedef struct _MEMORYSTATUS {
    DWORD dwLength;
    DWORD dwMemoryLoad;
    SIZE_T dwTotalPhys;
    SIZE_T dwAvailPhys;
    SIZE_T dwTotalPageFile;
    SIZE_T dwAvailPageFile;
    SIZE_T dwTotalVirtual;
    SIZE_T dwAvailVirtual;
} MEMORYSTATUS, *LPMEMORYSTATUS;
typedef struct _MEMORYSTATUSEX {
    DWORD dwLength;
    DWORD dwMemoryLoad;
    DWORDLONG ullTotalPhys;
    DWORDLONG ullAvailPhys;
    DWORDLONG ullTotalPageFile;
    DWORDLONG ullAvailPageFile;
    DWORDLONG ullTotalVirtual;
    DWORDLONG ullAvailVirtual;
    DWORDLONG ullAvailExtendedVirtual;
} MEMORYSTATUSEX, *LPMEMORYSTATUSEX;

typedef struct _SYSTEM_INFO {
    union {
        DWORD dwOemId;
        struct { WORD wProcessorArchitecture; WORD wReserved; };
    };
    DWORD dwPageSize;
    LPVOID lpMinimumApplicationAddress;
    LPVOID lpMaximumApplicationAddress;
    DWORD_PTR dwActiveProcessorMask;
    DWORD dwNumberOfProcessors;
    DWORD dwProcessorType;
    DWORD dwAllocationGranularity;
    WORD wProcessorLevel;
    WORD wProcessorRevision;
} SYSTEM_INFO, *LPSYSTEM_INFO;

typedef struct _OSVERSIONINFOA {
    DWORD dwOSVersionInfoSize;
    DWORD dwMajorVersion;
    DWORD dwMinorVersion;
    DWORD dwBuildNumber;
    DWORD dwPlatformId;
    CHAR szCSDVersion[128];
} OSVERSIONINFOA, *POSVERSIONINFOA, *LPOSVERSIONINFOA, OSVERSIONINFO, *LPOSVERSIONINFO;
typedef struct _OSVERSIONINFOEXA {
    DWORD dwOSVersionInfoSize, dwMajorVersion, dwMinorVersion, dwBuildNumber, dwPlatformId;
    CHAR szCSDVersion[128];
    WORD wServicePackMajor, wServicePackMinor, wSuiteMask;
    BYTE wProductType, wReserved;
} OSVERSIONINFOEXA, *LPOSVERSIONINFOEXA, OSVERSIONINFOEX;

typedef struct _STARTUPINFOA {
    DWORD cb;
    LPSTR lpReserved, lpDesktop, lpTitle;
    DWORD dwX, dwY, dwXSize, dwYSize, dwXCountChars, dwYCountChars, dwFillAttribute, dwFlags;
    WORD wShowWindow, cbReserved2;
    LPBYTE lpReserved2;
    HANDLE hStdInput, hStdOutput, hStdError;
} STARTUPINFOA, *LPSTARTUPINFOA, STARTUPINFO;
typedef struct _PROCESS_INFORMATION { HANDLE hProcess, hThread; DWORD dwProcessId, dwThreadId; } PROCESS_INFORMATION, *LPPROCESS_INFORMATION;

// ----- exceptions (SEH does not exist on wasm; the types exist so filters and handlers compile) -----
#define EXCEPTION_MAXIMUM_PARAMETERS 15
typedef struct _EXCEPTION_RECORD {
    DWORD ExceptionCode;
    DWORD ExceptionFlags;
    struct _EXCEPTION_RECORD *ExceptionRecord;
    PVOID ExceptionAddress;
    DWORD NumberParameters;
    ULONG_PTR ExceptionInformation[EXCEPTION_MAXIMUM_PARAMETERS];
} EXCEPTION_RECORD, *PEXCEPTION_RECORD;
typedef struct _FLOATING_SAVE_AREA {
    DWORD ControlWord, StatusWord, TagWord, ErrorOffset, ErrorSelector, DataOffset, DataSelector;
    BYTE RegisterArea[80];
    DWORD Spare0;
} FLOATING_SAVE_AREA;
typedef struct _CONTEXT {
    DWORD ContextFlags;
    DWORD Dr0, Dr1, Dr2, Dr3, Dr6, Dr7;
    FLOATING_SAVE_AREA FloatSave;
    DWORD SegGs, SegFs, SegEs, SegDs;
    DWORD Edi, Esi, Ebx, Edx, Ecx, Eax;
    DWORD Ebp, Eip, SegCs, EFlags, Esp, SegSs;
    BYTE ExtendedRegisters[512];
} CONTEXT, *PCONTEXT, *LPCONTEXT;
typedef struct _EXCEPTION_POINTERS { PEXCEPTION_RECORD ExceptionRecord; PCONTEXT ContextRecord; } EXCEPTION_POINTERS, *PEXCEPTION_POINTERS, *LPEXCEPTION_POINTERS;
typedef LONG(WINAPI *PTOP_LEVEL_EXCEPTION_FILTER)(struct _EXCEPTION_POINTERS *ExceptionInfo);
typedef PTOP_LEVEL_EXCEPTION_FILTER LPTOP_LEVEL_EXCEPTION_FILTER;
typedef struct _LUID_AND_ATTRIBUTES { LUID Luid; DWORD Attributes; } LUID_AND_ATTRIBUTES;
typedef struct _TOKEN_PRIVILEGES { DWORD PrivilegeCount; LUID_AND_ATTRIBUTES Privileges[1]; } TOKEN_PRIVILEGES, *PTOKEN_PRIVILEGES;
#define EXCEPTION_EXECUTE_HANDLER 1
#define EXCEPTION_CONTINUE_SEARCH 0
#define EXCEPTION_CONTINUE_EXECUTION (-1)
#define EXCEPTION_ACCESS_VIOLATION 0xC0000005L
#define EXCEPTION_BREAKPOINT 0x80000003L
#define EXCEPTION_STACK_OVERFLOW 0xC00000FDL
#define EXCEPTION_NONCONTINUABLE 0x1
// web: SEH blocks. __try runs its body; __except / __finally bodies never run (nothing raises SEH exceptions).
#define __try if (1)
#define __except(filter) else if (0)
#define __finally if (1)
#define __leave

// ----- GUID / COM base -----
#ifndef GUID_DEFINED
#define GUID_DEFINED
typedef struct _GUID {
    unsigned long Data1;
    unsigned short Data2;
    unsigned short Data3;
    unsigned char Data4[8];
} GUID;
#endif
typedef GUID IID, CLSID, FMTID;
typedef GUID *LPGUID, *LPIID, *LPCLSID;
typedef const GUID *LPCGUID;
#ifdef __cplusplus
#define REFGUID const GUID &
#define REFIID const IID &
#define REFCLSID const IID &
#define REFFMTID const IID &
#else
#define REFGUID const GUID *
#define REFIID const IID *
#define REFCLSID const IID *
#define REFFMTID const IID *
#endif
#ifdef __cplusplus
inline bool IsEqualGUID(REFGUID a, REFGUID b) { return memcmp(&a, &b, sizeof(GUID)) == 0; }
inline bool operator==(REFGUID a, REFGUID b) { return IsEqualGUID(a, b); }
inline bool operator!=(REFGUID a, REFGUID b) { return !IsEqualGUID(a, b); }
#define IsEqualIID(a, b) IsEqualGUID(a, b)
#define IsEqualCLSID(a, b) IsEqualGUID(a, b)
#define BO1_GUID_EXTERN extern "C"
#else
#define IsEqualGUID(a, b) (!memcmp((a), (b), sizeof(GUID)))
#define IsEqualIID IsEqualGUID
#define BO1_GUID_EXTERN extern
#endif
#ifdef INITGUID
#define DEFINE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
    BO1_GUID_EXTERN const GUID name __attribute__((weak)) = { l, w1, w2, { b1, b2, b3, b4, b5, b6, b7, b8 } }
#else
#define DEFINE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) BO1_GUID_EXTERN const GUID name
#endif
#define DECLSPEC_UUID(x)
#define MIDL_INTERFACE(x) struct

#define S_OK ((HRESULT)0L)
#define S_FALSE ((HRESULT)1L)
#define E_FAIL ((HRESULT)0x80004005L)
#define E_NOTIMPL ((HRESULT)0x80004001L)
#define E_NOINTERFACE ((HRESULT)0x80004002L)
#define E_POINTER ((HRESULT)0x80004003L)
#define E_ABORT ((HRESULT)0x80004004L)
#define E_UNEXPECTED ((HRESULT)0x8000FFFFL)
#define E_OUTOFMEMORY ((HRESULT)0x8007000EL)
#define E_INVALIDARG ((HRESULT)0x80070057L)
#define E_ACCESSDENIED ((HRESULT)0x80070005L)
#define E_HANDLE ((HRESULT)0x80070006L)
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#define FAILED(hr) (((HRESULT)(hr)) < 0)
#define HRESULT_CODE(hr) ((hr) & 0xFFFF)
#define HRESULT_FACILITY(hr) (((hr) >> 16) & 0x1fff)
#define SCODE_CODE(sc) ((sc) & 0xFFFF)
#define MAKE_HRESULT(sev, fac, code) ((HRESULT)(((unsigned long)(sev) << 31) | ((unsigned long)(fac) << 16) | ((unsigned long)(code))))
#define MAKE_SCODE(sev, fac, code) MAKE_HRESULT(sev, fac, code)
#define FACILITY_WIN32 7
#define HRESULT_FROM_WIN32(x) ((HRESULT)(x) <= 0 ? ((HRESULT)(x)) : ((HRESULT)(((x) & 0x0000FFFF) | (FACILITY_WIN32 << 16) | 0x80000000)))
#define SEVERITY_SUCCESS 0
#define SEVERITY_ERROR 1

#define STDMETHODCALLTYPE
#define STDMETHODVCALLTYPE
#define STDAPICALLTYPE
#define STDAPI extern "C" HRESULT
#define STDAPI_(type) extern "C" type
#define STDMETHODIMP HRESULT
#define STDMETHODIMP_(type) type
#ifdef __cplusplus
#ifndef interface
#define interface struct
#endif
#define STDMETHOD(method) virtual HRESULT method
#define STDMETHOD_(type, method) virtual type method
#define PURE = 0
#define THIS_
#define THIS void
#define DECLARE_INTERFACE(iface) struct iface
#define DECLARE_INTERFACE_(iface, baseiface) struct iface : public baseiface
#define DECLARE_INTERFACE_IID_(iface, baseiface, iid) struct iface : public baseiface
struct __declspec(uuid("00000000-0000-0000-C000-000000000046")) IUnknown
{
    virtual HRESULT QueryInterface(REFIID riid, void **ppvObject) = 0;
    virtual ULONG AddRef() = 0;
    virtual ULONG Release() = 0;
};
typedef IUnknown *LPUNKNOWN;
#endif

// ===================================================================================================================
// Constants
// ===================================================================================================================
#define MAX_PATH 260
#define INFINITE 0xFFFFFFFF
#define WAIT_OBJECT_0 0x00000000L
#define WAIT_ABANDONED 0x00000080L
#define WAIT_ABANDONED_0 WAIT_ABANDONED
#define WAIT_IO_COMPLETION 0x000000C0L
#define WAIT_TIMEOUT 0x00000102L
#define WAIT_FAILED ((DWORD)0xFFFFFFFF)
#define MAXIMUM_WAIT_OBJECTS 64
#define STATUS_WAIT_0 ((DWORD)0x00000000L)
#define STILL_ACTIVE 259
#define INVALID_HANDLE_VALUE ((HANDLE)(LONG_PTR)-1)
#define INVALID_FILE_SIZE ((DWORD)0xFFFFFFFF)
#define INVALID_SET_FILE_POINTER ((DWORD)-1)
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#define CREATE_SUSPENDED 0x00000004
#define STACK_SIZE_PARAM_IS_A_RESERVATION 0x00010000
#define THREAD_PRIORITY_LOWEST (-2)
#define THREAD_PRIORITY_BELOW_NORMAL (-1)
#define THREAD_PRIORITY_NORMAL 0
#define THREAD_PRIORITY_ABOVE_NORMAL 1
#define THREAD_PRIORITY_HIGHEST 2
#define THREAD_PRIORITY_TIME_CRITICAL 15
#define THREAD_PRIORITY_IDLE (-15)
#define THREAD_PRIORITY_ERROR_RETURN (0x7fffffff)
#define NORMAL_PRIORITY_CLASS 0x00000020
#define HIGH_PRIORITY_CLASS 0x00000080
#define ABOVE_NORMAL_PRIORITY_CLASS 0x00008000
#define DUPLICATE_CLOSE_SOURCE 0x00000001
#define DUPLICATE_SAME_ACCESS 0x00000002
#define SYNCHRONIZE 0x00100000L
#define STANDARD_RIGHTS_REQUIRED 0x000F0000L
#define EVENT_ALL_ACCESS 0x1F0003
#define EVENT_MODIFY_STATE 0x0002
#define MUTEX_ALL_ACCESS 0x1F0001
#define PROCESS_ALL_ACCESS 0x1F0FFF
#define PROCESS_QUERY_INFORMATION 0x0400
#define PROCESS_VM_READ 0x0010
#define THREAD_ALL_ACCESS 0x1F03FF
#define THREAD_SUSPEND_RESUME 0x0002
#define THREAD_GET_CONTEXT 0x0008
#define CONTEXT_i386 0x00010000L
#define CONTEXT_CONTROL (CONTEXT_i386 | 0x00000001L)
#define CONTEXT_INTEGER (CONTEXT_i386 | 0x00000002L)
#define CONTEXT_SEGMENTS (CONTEXT_i386 | 0x00000004L)
#define CONTEXT_FULL (CONTEXT_CONTROL | CONTEXT_INTEGER | CONTEXT_SEGMENTS)

#define MEM_COMMIT 0x00001000
#define MEM_RESERVE 0x00002000
#define MEM_DECOMMIT 0x00004000
#define MEM_RELEASE 0x00008000
#define MEM_FREE 0x00010000
#define MEM_PRIVATE 0x00020000
#define MEM_MAPPED 0x00040000
#define MEM_RESET 0x00080000
#define MEM_TOP_DOWN 0x00100000
#define MEM_WRITE_WATCH 0x00200000
#define MEM_PHYSICAL 0x00400000
#define MEM_LARGE_PAGES 0x20000000
#define MEM_IMAGE 0x01000000
#define PAGE_NOACCESS 0x01
#define PAGE_READONLY 0x02
#define PAGE_READWRITE 0x04
#define PAGE_WRITECOPY 0x08
#define PAGE_EXECUTE 0x10
#define PAGE_EXECUTE_READ 0x20
#define PAGE_EXECUTE_READWRITE 0x40
#define PAGE_EXECUTE_WRITECOPY 0x80
#define PAGE_GUARD 0x100
#define PAGE_NOCACHE 0x200
#define PAGE_WRITECOMBINE 0x400
#define GMEM_FIXED 0x0000
#define GMEM_MOVEABLE 0x0002
#define GMEM_ZEROINIT 0x0040
#define GMEM_DDESHARE 0x2000
#define GHND (GMEM_MOVEABLE | GMEM_ZEROINIT)
#define GPTR (GMEM_FIXED | GMEM_ZEROINIT)
#define LMEM_FIXED 0x0000
#define LMEM_ZEROINIT 0x0040
#define LPTR (LMEM_FIXED | LMEM_ZEROINIT)
#define HEAP_ZERO_MEMORY 0x00000008
#define HEAP_NO_SERIALIZE 0x00000001
#define HEAP_GENERATE_EXCEPTIONS 0x00000004

#define GENERIC_READ 0x80000000L
#define GENERIC_WRITE 0x40000000L
#define GENERIC_EXECUTE 0x20000000L
#define GENERIC_ALL 0x10000000L
#define FILE_READ_DATA 0x0001
#define FILE_WRITE_DATA 0x0002
#define FILE_APPEND_DATA 0x0004
#define FILE_READ_ATTRIBUTES 0x0080
#define FILE_SHARE_READ 0x00000001
#define FILE_SHARE_WRITE 0x00000002
#define FILE_SHARE_DELETE 0x00000004
#define CREATE_NEW 1
#define CREATE_ALWAYS 2
#define OPEN_EXISTING 3
#define OPEN_ALWAYS 4
#define TRUNCATE_EXISTING 5
#define FILE_ATTRIBUTE_READONLY 0x00000001
#define FILE_ATTRIBUTE_HIDDEN 0x00000002
#define FILE_ATTRIBUTE_SYSTEM 0x00000004
#define FILE_ATTRIBUTE_DIRECTORY 0x00000010
#define FILE_ATTRIBUTE_ARCHIVE 0x00000020
#define FILE_ATTRIBUTE_DEVICE 0x00000040
#define FILE_ATTRIBUTE_NORMAL 0x00000080
#define FILE_ATTRIBUTE_TEMPORARY 0x00000100
#define FILE_FLAG_WRITE_THROUGH 0x80000000
#define FILE_FLAG_OVERLAPPED 0x40000000
#define FILE_FLAG_NO_BUFFERING 0x20000000
#define FILE_FLAG_RANDOM_ACCESS 0x10000000
#define FILE_FLAG_SEQUENTIAL_SCAN 0x08000000
#define FILE_FLAG_DELETE_ON_CLOSE 0x04000000
#define FILE_FLAG_BACKUP_SEMANTICS 0x02000000
#define FILE_BEGIN 0
#define FILE_CURRENT 1
#define FILE_END 2
#define MOVEFILE_REPLACE_EXISTING 0x00000001
#define MOVEFILE_COPY_ALLOWED 0x00000002
#define MOVEFILE_WRITE_THROUGH 0x00000008
#define DRIVE_UNKNOWN 0
#define DRIVE_NO_ROOT_DIR 1
#define DRIVE_REMOVABLE 2
#define DRIVE_FIXED 3
#define DRIVE_REMOTE 4
#define DRIVE_CDROM 5
#define DRIVE_RAMDISK 6
#define STD_INPUT_HANDLE ((DWORD)-10)
#define STD_OUTPUT_HANDLE ((DWORD)-11)
#define STD_ERROR_HANDLE ((DWORD)-12)

#define ERROR_SUCCESS 0L
#define NO_ERROR 0L
#define ERROR_INVALID_FUNCTION 1L
#define ERROR_FILE_NOT_FOUND 2L
#define ERROR_PATH_NOT_FOUND 3L
#define ERROR_TOO_MANY_OPEN_FILES 4L
#define ERROR_ACCESS_DENIED 5L
#define ERROR_INVALID_HANDLE 6L
#define ERROR_NOT_ENOUGH_MEMORY 8L
#define ERROR_OUTOFMEMORY 14L
#define ERROR_NO_MORE_FILES 18L
#define ERROR_WRITE_PROTECT 19L
#define ERROR_NOT_READY 21L
#define ERROR_SHARING_VIOLATION 32L
#define ERROR_LOCK_VIOLATION 33L
#define ERROR_HANDLE_EOF 38L
#define ERROR_HANDLE_DISK_FULL 39L
#define ERROR_NOT_SUPPORTED 50L
#define ERROR_FILE_EXISTS 80L
#define ERROR_INVALID_PARAMETER 87L
#define ERROR_BROKEN_PIPE 109L
#define ERROR_DISK_FULL 112L
#define ERROR_CALL_NOT_IMPLEMENTED 120L
#define ERROR_INSUFFICIENT_BUFFER 122L
#define ERROR_INVALID_NAME 123L
#define ERROR_MOD_NOT_FOUND 126L
#define ERROR_PROC_NOT_FOUND 127L
#define ERROR_DIR_NOT_EMPTY 145L
#define ERROR_ALREADY_EXISTS 183L
#define ERROR_ENVVAR_NOT_FOUND 203L
#define ERROR_MORE_DATA 234L
#define ERROR_NO_MORE_ITEMS 259L
#define ERROR_OPERATION_ABORTED 995L
#define ERROR_IO_INCOMPLETE 996L
#define ERROR_IO_PENDING 997L
#define ERROR_NOACCESS 998L
#define ERROR_TIMEOUT 1460L
#define ERROR_CANCELLED 1223L
#define ERROR_NOT_FOUND 1168L
#define ERROR_INVALID_ADDRESS 487L

#define FORMAT_MESSAGE_ALLOCATE_BUFFER 0x00000100
#define FORMAT_MESSAGE_IGNORE_INSERTS 0x00000200
#define FORMAT_MESSAGE_FROM_STRING 0x00000400
#define FORMAT_MESSAGE_FROM_HMODULE 0x00000800
#define FORMAT_MESSAGE_FROM_SYSTEM 0x00001000
#define FORMAT_MESSAGE_ARGUMENT_ARRAY 0x00002000
#define LANG_NEUTRAL 0x00
#define LANG_ENGLISH 0x09
#define LANG_FRENCH 0x0c
#define LANG_GERMAN 0x07
#define LANG_ITALIAN 0x10
#define LANG_SPANISH 0x0a
#define LANG_JAPANESE 0x11
#define LANG_RUSSIAN 0x19
#define LANG_POLISH 0x15
#define LANG_KOREAN 0x12
#define LANG_CHINESE 0x04
#define SUBLANG_NEUTRAL 0x00
#define SUBLANG_DEFAULT 0x01
#define SUBLANG_SYS_DEFAULT 0x02
#define SUBLANG_ENGLISH_US 0x01
#define MAKELANGID(p, s) ((((WORD)(s)) << 10) | (WORD)(p))
#define PRIMARYLANGID(lgid) ((WORD)(lgid) & 0x3ff)
#define SUBLANGID(lgid) ((WORD)(lgid) >> 10)
#define MAKELCID(lgid, srtid) ((DWORD)((((DWORD)((WORD)(srtid))) << 16) | ((DWORD)((WORD)(lgid)))))
#define SORT_DEFAULT 0x0
#define LANG_USER_DEFAULT (MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT))
#define LOCALE_USER_DEFAULT (MAKELCID(LANG_USER_DEFAULT, SORT_DEFAULT))
#define LOCALE_SYSTEM_DEFAULT (MAKELCID(MAKELANGID(LANG_NEUTRAL, SUBLANG_SYS_DEFAULT), SORT_DEFAULT))
#define LOCALE_SDECIMAL 0x0000000E
#define LOCALE_STHOUSAND 0x0000000F
#define LOCALE_SENGLANGUAGE 0x00001001
#define LOCALE_SISO639LANGNAME 0x00000059
#define LOCALE_SISO3166CTRYNAME 0x0000005A
#define LOCALE_ILANGUAGE 0x00000001
#define CP_ACP 0
#define CP_OEMCP 1
#define CP_UTF8 65001
#define MB_PRECOMPOSED 0x00000001
#define MB_ERR_INVALID_CHARS 0x00000008
#define WC_NO_BEST_FIT_CHARS 0x00000400

#define SEM_FAILCRITICALERRORS 0x0001
#define SEM_NOGPFAULTERRORBOX 0x0002
#define SEM_NOALIGNMENTFAULTEXCEPT 0x0004
#define SEM_NOOPENFILEERRORBOX 0x8000
#define ES_SYSTEM_REQUIRED ((DWORD)0x00000001)
#define ES_DISPLAY_REQUIRED ((DWORD)0x00000002)
#define ES_CONTINUOUS ((DWORD)0x80000000)
#define PROCESSOR_ARCHITECTURE_INTEL 0
#define PROCESSOR_INTEL_PENTIUM 586
#define VER_PLATFORM_WIN32_NT 2

// ----- user32 / gdi32 -----
#define WM_NULL 0x0000
#define WM_CREATE 0x0001
#define WM_DESTROY 0x0002
#define WM_MOVE 0x0003
#define WM_SIZE 0x0005
#define WM_ACTIVATE 0x0006
#define WM_SETFOCUS 0x0007
#define WM_KILLFOCUS 0x0008
#define WM_ENABLE 0x000A
#define WM_SETTEXT 0x000C
#define WM_GETTEXT 0x000D
#define WM_PAINT 0x000F
#define WM_CLOSE 0x0010
#define WM_QUIT 0x0012
#define WM_ERASEBKGND 0x0014
#define WM_SHOWWINDOW 0x0018
#define WM_ACTIVATEAPP 0x001C
#define WM_SETCURSOR 0x0020
#define WM_MOUSEACTIVATE 0x0021
#define WM_GETMINMAXINFO 0x0024
#define WM_WINDOWPOSCHANGING 0x0046
#define WM_WINDOWPOSCHANGED 0x0047
#define WM_DISPLAYCHANGE 0x007E
#define WM_NCCREATE 0x0081
#define WM_NCDESTROY 0x0082
#define WM_NCHITTEST 0x0084
#define WM_NCACTIVATE 0x0086
#define WM_INPUT 0x00FF
#define WM_KEYFIRST 0x0100
#define WM_KEYDOWN 0x0100
#define WM_KEYUP 0x0101
#define WM_CHAR 0x0102
#define WM_DEADCHAR 0x0103
#define WM_SYSKEYDOWN 0x0104
#define WM_SYSKEYUP 0x0105
#define WM_SYSCHAR 0x0106
#define WM_KEYLAST 0x0109
#define WM_IME_STARTCOMPOSITION 0x010D
#define WM_IME_ENDCOMPOSITION 0x010E
#define WM_IME_COMPOSITION 0x010F
#define WM_INITDIALOG 0x0110
#define WM_COMMAND 0x0111
#define WM_SYSCOMMAND 0x0112
#define WM_TIMER 0x0113
#define WM_HSCROLL 0x0114
#define WM_VSCROLL 0x0115
#define WM_CTLCOLOREDIT 0x0133
#define WM_CTLCOLORSTATIC 0x0138
#define WM_MOUSEFIRST 0x0200
#define WM_MOUSEMOVE 0x0200
#define WM_LBUTTONDOWN 0x0201
#define WM_LBUTTONUP 0x0202
#define WM_LBUTTONDBLCLK 0x0203
#define WM_RBUTTONDOWN 0x0204
#define WM_RBUTTONUP 0x0205
#define WM_RBUTTONDBLCLK 0x0206
#define WM_MBUTTONDOWN 0x0207
#define WM_MBUTTONUP 0x0208
#define WM_MBUTTONDBLCLK 0x0209
#define WM_MOUSEWHEEL 0x020A
#define WM_XBUTTONDOWN 0x020B
#define WM_XBUTTONUP 0x020C
#define WM_XBUTTONDBLCLK 0x020D
#define WM_MOUSEHWHEEL 0x020E
#define WM_MOUSELAST 0x020E
#define WM_ENTERSIZEMOVE 0x0231
#define WM_EXITSIZEMOVE 0x0232
#define WM_IME_SETCONTEXT 0x0281
#define WM_IME_NOTIFY 0x0282
#define WM_POWERBROADCAST 0x0218
#define WM_DEVICECHANGE 0x0219
#define WM_HOTKEY 0x0312
#define WM_USER 0x0400
#define WM_APP 0x8000
#define WA_INACTIVE 0
#define WA_ACTIVE 1
#define WA_CLICKACTIVE 2
#define SIZE_RESTORED 0
#define SIZE_MINIMIZED 1
#define SIZE_MAXIMIZED 2
#define SC_SIZE 0xF000
#define SC_MOVE 0xF010
#define SC_MINIMIZE 0xF020
#define SC_MAXIMIZE 0xF030
#define SC_CLOSE 0xF060
#define SC_KEYMENU 0xF100
#define SC_SCREENSAVE 0xF140
#define SC_MONITORPOWER 0xF170
#define MK_LBUTTON 0x0001
#define MK_RBUTTON 0x0002
#define MK_SHIFT 0x0004
#define MK_CONTROL 0x0008
#define MK_MBUTTON 0x0010
#define MK_XBUTTON1 0x0020
#define MK_XBUTTON2 0x0040
#define WHEEL_DELTA 120
#define GET_WHEEL_DELTA_WPARAM(wParam) ((short)HIWORD_WIN(wParam))
#define GET_X_LPARAM(lp) ((int)(short)LOWORD_WIN(lp))
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD_WIN(lp))
#define LOWORD_WIN(l) ((WORD)(((DWORD_PTR)(l)) & 0xffff))
#define HIWORD_WIN(l) ((WORD)((((DWORD_PTR)(l)) >> 16) & 0xffff))
#define PM_NOREMOVE 0x0000
#define PM_REMOVE 0x0001
#define PM_NOYIELD 0x0002
#define CS_VREDRAW 0x0001
#define CS_HREDRAW 0x0002
#define CS_DBLCLKS 0x0008
#define CS_OWNDC 0x0020
#define CS_CLASSDC 0x0040
#define WS_OVERLAPPED 0x00000000L
#define WS_POPUP 0x80000000L
#define WS_CHILD 0x40000000L
#define WS_MINIMIZE 0x20000000L
#define WS_VISIBLE 0x10000000L
#define WS_DISABLED 0x08000000L
#define WS_CLIPSIBLINGS 0x04000000L
#define WS_CLIPCHILDREN 0x02000000L
#define WS_MAXIMIZE 0x01000000L
#define WS_CAPTION 0x00C00000L
#define WS_BORDER 0x00800000L
#define WS_DLGFRAME 0x00400000L
#define WS_VSCROLL 0x00200000L
#define WS_HSCROLL 0x00100000L
#define WS_SYSMENU 0x00080000L
#define WS_THICKFRAME 0x00040000L
#define WS_GROUP 0x00020000L
#define WS_TABSTOP 0x00010000L
#define WS_MINIMIZEBOX 0x00020000L
#define WS_MAXIMIZEBOX 0x00010000L
#define WS_OVERLAPPEDWINDOW (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)
#define WS_EX_DLGMODALFRAME 0x00000001L
#define WS_EX_TOPMOST 0x00000008L
#define WS_EX_ACCEPTFILES 0x00000010L
#define WS_EX_TRANSPARENT 0x00000020L
#define WS_EX_TOOLWINDOW 0x00000080L
#define WS_EX_WINDOWEDGE 0x00000100L
#define WS_EX_CLIENTEDGE 0x00000200L
#define WS_EX_APPWINDOW 0x00040000L
#define WS_EX_LAYERED 0x00080000L
#define ES_LEFT 0x0000L
#define ES_MULTILINE 0x0004L
#define ES_AUTOVSCROLL 0x0040L
#define ES_AUTOHSCROLL 0x0080L
#define ES_READONLY 0x0800L
#define SS_BITMAP 0x0000000EL
#define BS_PUSHBUTTON 0x00000000L
#define GWL_WNDPROC (-4)
#define GWL_HINSTANCE (-6)
#define GWL_ID (-12)
#define GWL_STYLE (-16)
#define GWL_EXSTYLE (-20)
#define GWL_USERDATA (-21)
#define GWLP_WNDPROC (-4)
#define GWLP_USERDATA (-21)
#define HWND_TOP ((HWND)0)
#define HWND_BOTTOM ((HWND)1)
#define HWND_TOPMOST ((HWND)-1)
#define HWND_NOTOPMOST ((HWND)-2)
#define HWND_DESKTOP ((HWND)0)
#define SWP_NOSIZE 0x0001
#define SWP_NOMOVE 0x0002
#define SWP_NOZORDER 0x0004
#define SWP_NOREDRAW 0x0008
#define SWP_NOACTIVATE 0x0010
#define SWP_FRAMECHANGED 0x0020
#define SWP_SHOWWINDOW 0x0040
#define SWP_HIDEWINDOW 0x0080
#define SWP_NOCOPYBITS 0x0100
#define SWP_NOOWNERZORDER 0x0200
#define SWP_NOSENDCHANGING 0x0400
#define SWP_ASYNCWINDOWPOS 0x4000
#define SW_HIDE 0
#define SW_SHOWNORMAL 1
#define SW_NORMAL 1
#define SW_SHOWMINIMIZED 2
#define SW_SHOWMAXIMIZED 3
#define SW_MAXIMIZE 3
#define SW_SHOWNOACTIVATE 4
#define SW_SHOW 5
#define SW_MINIMIZE 6
#define SW_SHOWMINNOACTIVE 7
#define SW_SHOWNA 8
#define SW_RESTORE 9
#define SW_SHOWDEFAULT 10
#define SM_CXSCREEN 0
#define SM_CYSCREEN 1
#define SM_CXFRAME 32
#define SM_CYFRAME 33
#define SM_CYCAPTION 4
#define SM_CXFIXEDFRAME 7
#define SM_CYFIXEDFRAME 8
#define SM_CMONITORS 80
#define SM_XVIRTUALSCREEN 76
#define SM_YVIRTUALSCREEN 77
#define SM_CXVIRTUALSCREEN 78
#define SM_CYVIRTUALSCREEN 79
#define SM_REMOTESESSION 0x1000
#define MONITOR_DEFAULTTONULL 0x00000000
#define MONITOR_DEFAULTTOPRIMARY 0x00000001
#define MONITOR_DEFAULTTONEAREST 0x00000002
#define MONITORINFOF_PRIMARY 0x00000001
#define ENUM_CURRENT_SETTINGS ((DWORD)-1)
#define ENUM_REGISTRY_SETTINGS ((DWORD)-2)
#define DM_BITSPERPEL 0x00040000L
#define DM_PELSWIDTH 0x00080000L
#define DM_PELSHEIGHT 0x00100000L
#define DM_DISPLAYFLAGS 0x00200000L
#define DM_DISPLAYFREQUENCY 0x00400000L
#define DM_POSITION 0x00000020L
#define CDS_UPDATEREGISTRY 0x00000001
#define CDS_TEST 0x00000002
#define CDS_FULLSCREEN 0x00000004
#define CDS_RESET 0x40000000
#define DISP_CHANGE_SUCCESSFUL 0
#define DISP_CHANGE_FAILED (-1)
#define DISPLAY_DEVICE_ATTACHED_TO_DESKTOP 0x00000001
#define DISPLAY_DEVICE_PRIMARY_DEVICE 0x00000004
#define MB_OK 0x00000000L
#define MB_OKCANCEL 0x00000001L
#define MB_ABORTRETRYIGNORE 0x00000002L
#define MB_YESNOCANCEL 0x00000003L
#define MB_YESNO 0x00000004L
#define MB_RETRYCANCEL 0x00000005L
#define MB_ICONHAND 0x00000010L
#define MB_ICONERROR 0x00000010L
#define MB_ICONSTOP 0x00000010L
#define MB_ICONQUESTION 0x00000020L
#define MB_ICONEXCLAMATION 0x00000030L
#define MB_ICONWARNING 0x00000030L
#define MB_ICONASTERISK 0x00000040L
#define MB_ICONINFORMATION 0x00000040L
#define MB_DEFBUTTON1 0x00000000L
#define MB_DEFBUTTON2 0x00000100L
#define MB_APPLMODAL 0x00000000L
#define MB_SYSTEMMODAL 0x00001000L
#define MB_TASKMODAL 0x00002000L
#define MB_SETFOREGROUND 0x00010000L
#define MB_TOPMOST 0x00040000L
#define IDOK 1
#define IDCANCEL 2
#define IDABORT 3
#define IDRETRY 4
#define IDIGNORE 5
#define IDYES 6
#define IDNO 7
#define IDC_ARROW ((LPCSTR)32512)
#define IDI_APPLICATION ((LPCSTR)32512)
#define CF_TEXT 1
#define CF_BITMAP 2
#define CF_UNICODETEXT 13
#define WH_KEYBOARD_LL 13
#define WH_MOUSE_LL 14
#define HC_ACTION 0
#define LLKHF_EXTENDED 0x00000001
#define LLKHF_ALTDOWN 0x00000020
#define LLKHF_UP 0x00000080
#define SPI_GETSCREENSAVEACTIVE 0x0010
#define SPI_SETSCREENSAVEACTIVE 0x0011
#define SPI_GETWORKAREA 0x0030
#define SPI_GETSTICKYKEYS 0x003A
#define SPI_SETSTICKYKEYS 0x003B
#define SPI_GETTOGGLEKEYS 0x0034
#define SPI_SETTOGGLEKEYS 0x0035
#define SPI_GETFILTERKEYS 0x0032
#define SPI_SETFILTERKEYS 0x0033
#define SPIF_UPDATEINIFILE 0x0001
#define SPIF_SENDCHANGE 0x0002
#define BLACK_BRUSH 4
#define WHITE_BRUSH 0
#define NULL_BRUSH 5
#define DEFAULT_GUI_FONT 17
#define COLOR_WINDOW 5
#define TRANSPARENT 1
#define OPAQUE 2
#define FW_NORMAL 400
#define FW_BOLD 700
#define DEFAULT_CHARSET 1
#define ANSI_CHARSET 0
#define OUT_DEFAULT_PRECIS 0
#define CLIP_DEFAULT_PRECIS 0
#define DEFAULT_QUALITY 0
#define DEFAULT_PITCH 0
#define FF_DONTCARE (0 << 4)
#define FF_MODERN (3 << 4)
#define FIXED_PITCH 1
#define LOGPIXELSX 88
#define LOGPIXELSY 90
#define HORZRES 8
#define VERTRES 10
#define BITSPIXEL 12
#define VREFRESH 116
#define RGB(r, g, b) ((COLORREF)(((BYTE)(r) | ((WORD)((BYTE)(g)) << 8)) | (((DWORD)(BYTE)(b)) << 16)))
#define GetRValue(rgb) ((BYTE)(rgb))
#define GetGValue(rgb) ((BYTE)(((WORD)(rgb)) >> 8))
#define GetBValue(rgb) ((BYTE)((rgb) >> 16))
#define MAKEINTRESOURCEA(i) ((LPSTR)((ULONG_PTR)((WORD)(i))))
#define MAKEINTRESOURCE MAKEINTRESOURCEA
#define MAKEWORD(a, b) ((WORD)(((BYTE)(((DWORD_PTR)(a)) & 0xff)) | ((WORD)((BYTE)(((DWORD_PTR)(b)) & 0xff))) << 8))
#define MAKELONG(a, b) ((LONG)(((WORD)(((DWORD_PTR)(a)) & 0xffff)) | ((DWORD)((WORD)(((DWORD_PTR)(b)) & 0xffff))) << 16))
#define MAKEWPARAM(l, h) ((WPARAM)(DWORD)MAKELONG(l, h))
#define MAKELPARAM(l, h) ((LPARAM)(DWORD)MAKELONG(l, h))
#define MAKELRESULT(l, h) ((LRESULT)(DWORD)MAKELONG(l, h))
// universal/q_shared.h #undefs and redefines LOWORD / HIWORD / LOBYTE / HIBYTE with the IDA meaning
#define LOWORD(l) ((WORD)(((DWORD_PTR)(l)) & 0xffff))
#define HIWORD(l) ((WORD)((((DWORD_PTR)(l)) >> 16) & 0xffff))
#define LOBYTE(w) ((BYTE)(((DWORD_PTR)(w)) & 0xff))
#define HIBYTE(w) ((BYTE)((((DWORD_PTR)(w)) >> 8) & 0xff))

// virtual keys (win_wndproc's MapKey uses its own table; these exist for the code that names them)
#define VK_LBUTTON 0x01
#define VK_RBUTTON 0x02
#define VK_CANCEL 0x03
#define VK_MBUTTON 0x04
#define VK_XBUTTON1 0x05
#define VK_XBUTTON2 0x06
#define VK_BACK 0x08
#define VK_TAB 0x09
#define VK_CLEAR 0x0C
#define VK_RETURN 0x0D
#define VK_SHIFT 0x10
#define VK_CONTROL 0x11
#define VK_MENU 0x12
#define VK_PAUSE 0x13
#define VK_CAPITAL 0x14
#define VK_ESCAPE 0x1B
#define VK_SPACE 0x20
#define VK_PRIOR 0x21
#define VK_NEXT 0x22
#define VK_END 0x23
#define VK_HOME 0x24
#define VK_LEFT 0x25
#define VK_UP 0x26
#define VK_RIGHT 0x27
#define VK_DOWN 0x28
#define VK_SELECT 0x29
#define VK_PRINT 0x2A
#define VK_SNAPSHOT 0x2C
#define VK_INSERT 0x2D
#define VK_DELETE 0x2E
#define VK_HELP 0x2F
#define VK_LWIN 0x5B
#define VK_RWIN 0x5C
#define VK_APPS 0x5D
#define VK_NUMPAD0 0x60
#define VK_NUMPAD1 0x61
#define VK_NUMPAD2 0x62
#define VK_NUMPAD3 0x63
#define VK_NUMPAD4 0x64
#define VK_NUMPAD5 0x65
#define VK_NUMPAD6 0x66
#define VK_NUMPAD7 0x67
#define VK_NUMPAD8 0x68
#define VK_NUMPAD9 0x69
#define VK_MULTIPLY 0x6A
#define VK_ADD 0x6B
#define VK_SEPARATOR 0x6C
#define VK_SUBTRACT 0x6D
#define VK_DECIMAL 0x6E
#define VK_DIVIDE 0x6F
#define VK_F1 0x70
#define VK_F2 0x71
#define VK_F3 0x72
#define VK_F4 0x73
#define VK_F5 0x74
#define VK_F6 0x75
#define VK_F7 0x76
#define VK_F8 0x77
#define VK_F9 0x78
#define VK_F10 0x79
#define VK_F11 0x7A
#define VK_F12 0x7B
#define VK_NUMLOCK 0x90
#define VK_SCROLL 0x91
#define VK_LSHIFT 0xA0
#define VK_RSHIFT 0xA1
#define VK_LCONTROL 0xA2
#define VK_RCONTROL 0xA3
#define VK_LMENU 0xA4
#define VK_RMENU 0xA5
#define VK_OEM_1 0xBA
#define VK_OEM_PLUS 0xBB
#define VK_OEM_COMMA 0xBC
#define VK_OEM_MINUS 0xBD
#define VK_OEM_PERIOD 0xBE
#define VK_OEM_2 0xBF
#define VK_OEM_3 0xC0
#define VK_OEM_4 0xDB
#define VK_OEM_5 0xDC
#define VK_OEM_6 0xDD
#define VK_OEM_7 0xDE
#define VK_OEM_102 0xE2
#define MAPVK_VK_TO_VSC 0
#define MAPVK_VSC_TO_VK 1
#define MAPVK_VK_TO_CHAR 2

#ifndef NOMINMAX
#ifndef max
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif
#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#endif
#endif

#define ZeroMemory(dst, len) memset((dst), 0, (len))
#define SecureZeroMemory(dst, len) memset((dst), 0, (len))
#define FillMemory(dst, len, fill) memset((dst), (fill), (len))
#define CopyMemory(dst, src, len) memcpy((dst), (src), (len))
#define MoveMemory(dst, src, len) memmove((dst), (src), (len))
#define RtlZeroMemory ZeroMemory
#define RtlCopyMemory CopyMemory
#define lstrlenA(s) ((int)strlen(s))
#define lstrlen lstrlenA
#define lstrcpyA strcpy
#define lstrcpy strcpy
#define lstrcpynA(d, s, n) (strncpy((d), (s), (n) - 1), (d)[(n) - 1] = 0, (d))
#define lstrcatA strcat
#define lstrcmpA strcmp
#define lstrcmpiA strcasecmp
#define lstrcmpi lstrcmpiA
#define wsprintfA sprintf
#define wsprintf sprintf
#define wvsprintfA vsprintf
#define TEXT(s) s
#define _T(s) s
#define __TEXT(s) s

// ===================================================================================================================
// Functions (src/web/compat/*.cpp)
// ===================================================================================================================
BO1_EXTERN_C_BEGIN

// ----- errors / debug (win_misc.cpp) -----
DWORD GetLastError(void);
void SetLastError(DWORD err);
void OutputDebugStringA(LPCSTR s);          // web: printErr (the browser console)
#define OutputDebugString OutputDebugStringA
BOOL IsDebuggerPresent(void);               // web: FALSE
void RaiseException(DWORD code, DWORD flags, DWORD nargs, const ULONG_PTR *args);   // web: no-op (SEH)
LPTOP_LEVEL_EXCEPTION_FILTER SetUnhandledExceptionFilter(LPTOP_LEVEL_EXCEPTION_FILTER filter);  // web: stored, never called
UINT SetErrorMode(UINT mode);
DWORD FormatMessageA(DWORD flags, LPCVOID source, DWORD messageId, DWORD languageId, LPSTR buffer, DWORD size, va_list *args);
#define FormatMessage FormatMessageA
int MessageBoxA(HWND hWnd, LPCSTR text, LPCSTR caption, UINT type);   // web: printed; returns IDOK / IDYES (never blocks)
#define MessageBox MessageBoxA
DECLSPEC_NORETURN void ExitProcess(UINT code);
BOOL TerminateProcess(HANDLE process, UINT code);   // web: the current process only: exit(code)
HLOCAL LocalFree(HLOCAL mem);
HLOCAL LocalAlloc(UINT flags, SIZE_T size);

// ----- system information (win_misc.cpp) -----
void GetSystemInfo(LPSYSTEM_INFO info);
void GetNativeSystemInfo(LPSYSTEM_INFO info);
BOOL GetVersionExA(LPOSVERSIONINFOA info);           // web: Windows 7 (6.1)
#define GetVersionEx GetVersionExA
DWORD GetVersion(void);
LPSTR GetCommandLineA(void);                         // web: the engine's joined command line (bo1_web_set_command_line)
#define GetCommandLine GetCommandLineA
DWORD GetEnvironmentVariableA(LPCSTR name, LPSTR buffer, DWORD size);
BOOL SetEnvironmentVariableA(LPCSTR name, LPCSTR value);
#define GetEnvironmentVariable GetEnvironmentVariableA
BOOL GetUserNameA(LPSTR buffer, LPDWORD size);       // web: "player"
BOOL GetComputerNameA(LPSTR buffer, LPDWORD size);   // web: "browser"
#define GetUserName GetUserNameA
#define GetComputerName GetComputerNameA
LCID GetUserDefaultLCID(void);
LANGID GetUserDefaultLangID(void);
LANGID GetSystemDefaultLangID(void);
LANGID GetUserDefaultUILanguage(void);
LCID GetThreadLocale(void);
int GetLocaleInfoA(LCID locale, DWORD type, LPSTR data, int size);
#define GetLocaleInfo GetLocaleInfoA
int MultiByteToWideChar(UINT codePage, DWORD flags, LPCSTR src, int srcLen, LPWSTR dst, int dstLen);
int WideCharToMultiByte(UINT codePage, DWORD flags, LPCWSTR src, int srcLen, LPSTR dst, int dstLen, LPCSTR defaultChar, LPBOOL usedDefault);
HMODULE LoadLibraryA(LPCSTR name);                   // web: NULL (no DLLs)
HMODULE LoadLibraryExA(LPCSTR name, HANDLE file, DWORD flags);
#define LoadLibrary LoadLibraryA
FARPROC GetProcAddress(HMODULE module, LPCSTR name); // web: NULL
BOOL FreeLibrary(HMODULE module);
HMODULE GetModuleHandleA(LPCSTR name);               // web: a fixed non-NULL handle for NULL (the exe), else NULL
#define GetModuleHandle GetModuleHandleA
DWORD GetModuleFileNameA(HMODULE module, LPSTR name, DWORD size);  // web: <Sys_DefaultInstallPath>/BlackOps.exe
#define GetModuleFileName GetModuleFileNameA
HANDLE GetStdHandle(DWORD which);
BOOL SetConsoleTitleA(LPCSTR title);
BOOL AllocConsole(void);
BOOL FreeConsole(void);
DWORD SetThreadExecutionState(DWORD flags);
BOOL ShellExecuteSucceeded(void);
HINSTANCE ShellExecuteA(HWND hwnd, LPCSTR op, LPCSTR file, LPCSTR params, LPCSTR dir, INT show);   // web: fails (returns 0)
#define ShellExecute ShellExecuteA
BOOL CreateProcessA(LPCSTR app, LPSTR cmdline, LPSECURITY_ATTRIBUTES pa, LPSECURITY_ATTRIBUTES ta, BOOL inherit,
    DWORD flags, LPVOID env, LPCSTR dir, LPSTARTUPINFOA si, LPPROCESS_INFORMATION pi);              // web: fails
HANDLE OpenProcess(DWORD access, BOOL inherit, DWORD pid);                                         // web: NULL
BOOL GetExitCodeProcess(HANDLE process, LPDWORD code);
BOOL SetPriorityClass(HANDLE process, DWORD cls);
DWORD GetPriorityClass(HANDLE process);
BOOL SetProcessAffinityMask(HANDLE process, DWORD_PTR mask);
BOOL GetProcessAffinityMask(HANDLE process, PDWORD_PTR processMask, PDWORD_PTR systemMask);  // web: one bit per core
BOOL OpenProcessToken(HANDLE process, DWORD access, PHANDLE token);
BOOL LookupPrivilegeValueA(LPCSTR system, LPCSTR name, PLUID luid);
BOOL AdjustTokenPrivileges(HANDLE token, BOOL disableAll, PTOKEN_PRIVILEGES newState, DWORD len, PTOKEN_PRIVILEGES prev, PDWORD retLen);

// ----- time (win_misc.cpp) -----
BOOL QueryPerformanceCounter(LARGE_INTEGER *count);   // web: microseconds of a monotonic clock
BOOL QueryPerformanceFrequency(LARGE_INTEGER *freq);  // web: 1000000
DWORD GetTickCount(void);
ULONGLONG GetTickCount64(void);
DWORD timeGetTime(void);
UINT timeBeginPeriod(UINT period);
UINT timeEndPeriod(UINT period);
#define TIMERR_NOERROR 0
typedef UINT MMRESULT;
void GetSystemTime(LPSYSTEMTIME st);
void GetLocalTime(LPSYSTEMTIME st);
void GetSystemTimeAsFileTime(LPFILETIME ft);
BOOL FileTimeToSystemTime(const FILETIME *ft, LPSYSTEMTIME st);
BOOL SystemTimeToFileTime(const SYSTEMTIME *st, LPFILETIME ft);
BOOL FileTimeToLocalFileTime(const FILETIME *ft, LPFILETIME local);
BOOL LocalFileTimeToFileTime(const FILETIME *local, LPFILETIME ft);
LONG CompareFileTime(const FILETIME *a, const FILETIME *b);
DWORD GetTimeZoneInformation(LPTIME_ZONE_INFORMATION tz);

// ----- synchronization, threads, TLS, APCs (win_sync.cpp) -----
void InitializeCriticalSection(LPCRITICAL_SECTION cs);
BOOL InitializeCriticalSectionAndSpinCount(LPCRITICAL_SECTION cs, DWORD spin);
BOOL InitializeCriticalSectionEx(LPCRITICAL_SECTION cs, DWORD spin, DWORD flags);
void DeleteCriticalSection(LPCRITICAL_SECTION cs);
void EnterCriticalSection(LPCRITICAL_SECTION cs);
void LeaveCriticalSection(LPCRITICAL_SECTION cs);
BOOL TryEnterCriticalSection(LPCRITICAL_SECTION cs);
DWORD SetCriticalSectionSpinCount(LPCRITICAL_SECTION cs, DWORD spin);
void InitializeSRWLock(PSRWLOCK lock);
void AcquireSRWLockExclusive(PSRWLOCK lock);
void ReleaseSRWLockExclusive(PSRWLOCK lock);
void AcquireSRWLockShared(PSRWLOCK lock);
void ReleaseSRWLockShared(PSRWLOCK lock);
HANDLE CreateEventA(LPSECURITY_ATTRIBUTES sa, BOOL manualReset, BOOL initialState, LPCSTR name);
HANDLE CreateEventW(LPSECURITY_ATTRIBUTES sa, BOOL manualReset, BOOL initialState, LPCWSTR name);
#define CreateEvent CreateEventA
HANDLE OpenEventA(DWORD access, BOOL inherit, LPCSTR name);
BOOL SetEvent(HANDLE event);
BOOL ResetEvent(HANDLE event);
BOOL PulseEvent(HANDLE event);
HANDLE CreateMutexA(LPSECURITY_ATTRIBUTES sa, BOOL initialOwner, LPCSTR name);
#define CreateMutex CreateMutexA
HANDLE OpenMutexA(DWORD access, BOOL inherit, LPCSTR name);
BOOL ReleaseMutex(HANDLE mutex);
HANDLE CreateSemaphoreA(LPSECURITY_ATTRIBUTES sa, LONG initialCount, LONG maximumCount, LPCSTR name);
#define CreateSemaphore CreateSemaphoreA
BOOL ReleaseSemaphore(HANDLE semaphore, LONG releaseCount, LPLONG previousCount);
DWORD WaitForSingleObject(HANDLE handle, DWORD milliseconds);
DWORD WaitForSingleObjectEx(HANDLE handle, DWORD milliseconds, BOOL alertable);
DWORD WaitForMultipleObjects(DWORD count, const HANDLE *handles, BOOL waitAll, DWORD milliseconds);
DWORD WaitForMultipleObjectsEx(DWORD count, const HANDLE *handles, BOOL waitAll, DWORD milliseconds, BOOL alertable);
DWORD SignalObjectAndWait(HANDLE toSignal, HANDLE toWaitOn, DWORD milliseconds, BOOL alertable);
BOOL CloseHandle(HANDLE handle);
BOOL DuplicateHandle(HANDLE srcProcess, HANDLE src, HANDLE dstProcess, LPHANDLE dst, DWORD access, BOOL inherit, DWORD options);
void Sleep(DWORD milliseconds);
// web: an alertable wait runs ONE queued APC (the oldest) and returns WAIT_IO_COMPLETION. Windows runs all queued
// APCs; db_file_load.cpp counts one completed read per SleepEx, which one-at-a-time matches exactly.
DWORD SleepEx(DWORD milliseconds, BOOL alertable);
BOOL SwitchToThread(void);
DWORD QueueUserAPC(PAPCFUNC fn, HANDLE thread, ULONG_PTR data);
HANDLE CreateThread(LPSECURITY_ATTRIBUTES sa, SIZE_T stackSize, LPTHREAD_START_ROUTINE start, LPVOID param, DWORD flags, LPDWORD threadId);
DWORD ResumeThread(HANDLE thread);
DWORD SuspendThread(HANDLE thread);       // web: cannot suspend a pthread; counts only (Sys_SuspendOtherThreads is best effort)
DECLSPEC_NORETURN void ExitThread(DWORD code);
BOOL TerminateThread(HANDLE thread, DWORD code);  // web: no-op, returns FALSE
BOOL GetExitCodeThread(HANDLE thread, LPDWORD code);
HANDLE GetCurrentThread(void);            // pseudo handle (-2), like Windows
HANDLE GetCurrentProcess(void);           // pseudo handle (-1)
DWORD GetCurrentProcessId(void);
DWORD GetThreadId(HANDLE thread);
BOOL SetThreadPriority(HANDLE thread, int priority);    // web: recorded only
int GetThreadPriority(HANDLE thread);
BOOL SetThreadPriorityBoost(HANDLE thread, BOOL disable);
DWORD_PTR SetThreadAffinityMask(HANDLE thread, DWORD_PTR mask);  // web: recorded only (returns the previous mask)
DWORD SetThreadIdealProcessor(HANDLE thread, DWORD processor);
BOOL GetThreadContext(HANDLE thread, LPCONTEXT context);         // web: FALSE
BOOL SetThreadDescription(HANDLE thread, LPCWSTR name);
DWORD TlsAlloc(void);
LPVOID TlsGetValue(DWORD index);
BOOL TlsSetValue(DWORD index, LPVOID value);
BOOL TlsFree(DWORD index);
#define TLS_OUT_OF_INDEXES ((DWORD)0xFFFFFFFF)
// web: name the next thread CreateThread makes in the browser's devtools, and transfer the page canvas to it
// (OffscreenCanvas, PROXY_TO_PTHREAD) - the render thread owns the WebGL context (see docs/web-port.md).
void bo1_web_next_thread_takes_canvas(int takeCanvas);

// ----- memory (win_memory.cpp) -----
// web: wasm memory cannot be reserved without committing it. MEM_RESERVE allocates the whole (aligned) range up front,
// zeroed; MEM_COMMIT of reserved pages zeroes them only if they were decommitted; MEM_DECOMMIT is a no-op apart from
// bookkeeping; MEM_RELEASE frees. VirtualQuery reports the bookkeeping (committed / reserved) per page range.
LPVOID VirtualAlloc(LPVOID address, SIZE_T size, DWORD type, DWORD protect);
BOOL VirtualFree(LPVOID address, SIZE_T size, DWORD type);
SIZE_T VirtualQuery(LPCVOID address, PMEMORY_BASIC_INFORMATION info, SIZE_T length);
BOOL VirtualProtect(LPVOID address, SIZE_T size, DWORD newProtect, PDWORD oldProtect);
BOOL VirtualLock(LPVOID address, SIZE_T size);
BOOL VirtualUnlock(LPVOID address, SIZE_T size);
void GlobalMemoryStatus(LPMEMORYSTATUS status);
BOOL GlobalMemoryStatusEx(LPMEMORYSTATUSEX status);
HGLOBAL GlobalAlloc(UINT flags, SIZE_T bytes);
LPVOID GlobalLock(HGLOBAL mem);
BOOL GlobalUnlock(HGLOBAL mem);
HGLOBAL GlobalFree(HGLOBAL mem);
SIZE_T GlobalSize(HGLOBAL mem);
HANDLE GetProcessHeap(void);
HANDLE HeapCreate(DWORD options, SIZE_T initial, SIZE_T maximum);
BOOL HeapDestroy(HANDLE heap);
LPVOID HeapAlloc(HANDLE heap, DWORD flags, SIZE_T bytes);
LPVOID HeapReAlloc(HANDLE heap, DWORD flags, LPVOID mem, SIZE_T bytes);
BOOL HeapFree(HANDLE heap, DWORD flags, LPVOID mem);
SIZE_T HeapSize(HANDLE heap, DWORD flags, LPCVOID mem);
BOOL HeapValidate(HANDLE heap, DWORD flags, LPCVOID mem);
BOOL FlushInstructionCache(HANDLE process, LPCVOID address, SIZE_T size);

// ----- files (win_file.cpp). Paths: '\' or '/', resolved case-insensitively (web_path.cpp) -----
HANDLE CreateFileA(LPCSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disposition, DWORD flags, HANDLE templ);
#define CreateFile CreateFileA
BOOL ReadFile(HANDLE file, LPVOID buffer, DWORD toRead, LPDWORD read, LPOVERLAPPED overlapped);
// web: performs the read synchronously (pread at overlapped->Offset), then queues the completion routine as an APC of
// the calling thread; it runs in that thread's next alertable wait (SleepEx / WaitFor*Ex), as on Windows.
BOOL ReadFileEx(HANDLE file, LPVOID buffer, DWORD toRead, LPOVERLAPPED overlapped, LPOVERLAPPED_COMPLETION_ROUTINE done);
BOOL WriteFile(HANDLE file, LPCVOID buffer, DWORD toWrite, LPDWORD written, LPOVERLAPPED overlapped);
BOOL WriteFileEx(HANDLE file, LPCVOID buffer, DWORD toWrite, LPOVERLAPPED overlapped, LPOVERLAPPED_COMPLETION_ROUTINE done);
BOOL GetOverlappedResult(HANDLE file, LPOVERLAPPED overlapped, LPDWORD transferred, BOOL wait);
BOOL CancelIo(HANDLE file);
BOOL FlushFileBuffers(HANDLE file);
DWORD GetFileSize(HANDLE file, LPDWORD sizeHigh);
BOOL GetFileSizeEx(HANDLE file, PLARGE_INTEGER size);
DWORD SetFilePointer(HANDLE file, LONG distance, PLONG distanceHigh, DWORD method);
BOOL SetFilePointerEx(HANDLE file, LARGE_INTEGER distance, PLARGE_INTEGER newPointer, DWORD method);
BOOL SetEndOfFile(HANDLE file);
BOOL GetFileTime(HANDLE file, LPFILETIME creation, LPFILETIME access, LPFILETIME write);
BOOL SetFileTime(HANDLE file, const FILETIME *creation, const FILETIME *access, const FILETIME *write);
BOOL GetFileInformationByHandle(HANDLE file, LPBY_HANDLE_FILE_INFORMATION info);
DWORD GetFileType(HANDLE file);
BOOL DeleteFileA(LPCSTR name);
#define DeleteFile DeleteFileA
BOOL MoveFileA(LPCSTR from, LPCSTR to);
BOOL MoveFileExA(LPCSTR from, LPCSTR to, DWORD flags);
#define MoveFile MoveFileA
BOOL CopyFileA(LPCSTR from, LPCSTR to, BOOL failIfExists);
#define CopyFile CopyFileA
BOOL CreateDirectoryA(LPCSTR path, LPSECURITY_ATTRIBUTES sa);
#define CreateDirectory CreateDirectoryA
BOOL RemoveDirectoryA(LPCSTR path);
DWORD GetFileAttributesA(LPCSTR name);
#define GetFileAttributes GetFileAttributesA
BOOL GetFileAttributesExA(LPCSTR name, GET_FILEEX_INFO_LEVELS level, LPVOID info);
BOOL SetFileAttributesA(LPCSTR name, DWORD attributes);
HANDLE FindFirstFileA(LPCSTR pattern, LPWIN32_FIND_DATAA data);
BOOL FindNextFileA(HANDLE find, LPWIN32_FIND_DATAA data);
BOOL FindClose(HANDLE find);
#define FindFirstFile FindFirstFileA
#define FindNextFile FindNextFileA
DWORD GetCurrentDirectoryA(DWORD size, LPSTR buffer);
BOOL SetCurrentDirectoryA(LPCSTR path);
#define GetCurrentDirectory GetCurrentDirectoryA
#define SetCurrentDirectory SetCurrentDirectoryA
DWORD GetFullPathNameA(LPCSTR name, DWORD size, LPSTR buffer, LPSTR *filePart);
DWORD GetTempPathA(DWORD size, LPSTR buffer);
UINT GetTempFileNameA(LPCSTR path, LPCSTR prefix, UINT unique, LPSTR name);
BOOL GetDiskFreeSpaceExA(LPCSTR dir, PULARGE_INTEGER freeToCaller, PULARGE_INTEGER total, PULARGE_INTEGER totalFree);
BOOL GetDiskFreeSpaceA(LPCSTR root, LPDWORD sectorsPerCluster, LPDWORD bytesPerSector, LPDWORD freeClusters, LPDWORD totalClusters);
UINT GetDriveTypeA(LPCSTR root);
DWORD GetLogicalDrives(void);
BOOL GetVolumeInformationA(LPCSTR root, LPSTR name, DWORD nameSize, LPDWORD serial, LPDWORD maxComponent, LPDWORD flags, LPSTR fsName, DWORD fsNameSize);
#define FILE_TYPE_DISK 0x0001
#define FILE_TYPE_CHAR 0x0002
#define FILE_TYPE_UNKNOWN 0x0000

// ----- user32 / gdi32 (win_gui.cpp): the window is the page's canvas -----
// web: one fake top-level window exists (the canvas). CreateWindowExA returns it; its client rect is the canvas size
// (bo1_web_canvas_size). Messages: none are generated (input arrives through web_input.cpp); PeekMessageA returns
// FALSE, PostMessageA(WM_CLOSE / WM_QUIT) asks the engine to quit.
ATOM RegisterClassA(const WNDCLASSA *wc);
ATOM RegisterClassExA(const WNDCLASSEXA *wc);
#define RegisterClass RegisterClassA
#define RegisterClassEx RegisterClassExA
BOOL UnregisterClassA(LPCSTR name, HINSTANCE instance);
HWND CreateWindowExA(DWORD exStyle, LPCSTR className, LPCSTR windowName, DWORD style, int x, int y, int width,
    int height, HWND parent, HMENU menu, HINSTANCE instance, LPVOID param);
#define CreateWindowEx CreateWindowExA
#define CreateWindowA(cls, name, style, x, y, w, h, parent, menu, inst, param) \
    CreateWindowExA(0, cls, name, style, x, y, w, h, parent, menu, inst, param)
#define CreateWindow CreateWindowA
BOOL DestroyWindow(HWND hwnd);
BOOL ShowWindow(HWND hwnd, int cmd);
BOOL UpdateWindow(HWND hwnd);
BOOL SetWindowPos(HWND hwnd, HWND after, int x, int y, int cx, int cy, UINT flags);
BOOL MoveWindow(HWND hwnd, int x, int y, int w, int h, BOOL repaint);
BOOL GetWindowRect(HWND hwnd, LPRECT rect);
BOOL GetClientRect(HWND hwnd, LPRECT rect);
BOOL AdjustWindowRect(LPRECT rect, DWORD style, BOOL menu);
BOOL AdjustWindowRectEx(LPRECT rect, DWORD style, BOOL menu, DWORD exStyle);
LONG SetWindowLongA(HWND hwnd, int index, LONG value);
LONG GetWindowLongA(HWND hwnd, int index);
#define SetWindowLong SetWindowLongA
#define GetWindowLong GetWindowLongA
#define SetWindowLongPtrA SetWindowLongA
#define GetWindowLongPtrA GetWindowLongA
#define SetWindowLongPtr SetWindowLongA
#define GetWindowLongPtr GetWindowLongA
BOOL SetWindowTextA(HWND hwnd, LPCSTR text);
#define SetWindowText SetWindowTextA
int GetWindowTextA(HWND hwnd, LPSTR text, int max);
BOOL GetWindowPlacement(HWND hwnd, WINDOWPLACEMENT *wp);
BOOL SetWindowPlacement(HWND hwnd, const WINDOWPLACEMENT *wp);
LRESULT DefWindowProcA(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
#define DefWindowProc DefWindowProcA
LRESULT CallWindowProcA(WNDPROC proc, HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
BOOL PeekMessageA(LPMSG msg, HWND hwnd, UINT filterMin, UINT filterMax, UINT remove);
BOOL GetMessageA(LPMSG msg, HWND hwnd, UINT filterMin, UINT filterMax);   // web: returns 0 (WM_QUIT) at once
#define PeekMessage PeekMessageA
#define GetMessage GetMessageA
BOOL TranslateMessage(const MSG *msg);
LRESULT DispatchMessageA(const MSG *msg);
#define DispatchMessage DispatchMessageA
BOOL PostMessageA(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT SendMessageA(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
#define PostMessage PostMessageA
#define SendMessage SendMessageA
void PostQuitMessage(int code);
BOOL PostThreadMessageA(DWORD threadId, UINT msg, WPARAM wParam, LPARAM lParam);
HICON LoadIconA(HINSTANCE instance, LPCSTR name);
HCURSOR LoadCursorA(HINSTANCE instance, LPCSTR name);
#define LoadIcon LoadIconA
#define LoadCursor LoadCursorA
HANDLE LoadImageA(HINSTANCE instance, LPCSTR name, UINT type, int cx, int cy, UINT flags);
HBRUSH CreateSolidBrush(COLORREF color);
HGDIOBJ GetStockObject(int object);
BOOL DeleteObject(HGDIOBJ object);
HGDIOBJ SelectObject(HDC hdc, HGDIOBJ object);
HFONT CreateFontA(int h, int w, int esc, int orient, int weight, DWORD italic, DWORD underline, DWORD strike,
    DWORD charset, DWORD outPrecision, DWORD clipPrecision, DWORD quality, DWORD pitch, LPCSTR face);
HCURSOR SetCursor(HCURSOR cursor);
int ShowCursor(BOOL show);
BOOL SetCursorPos(int x, int y);
BOOL GetCursorPos(LPPOINT point);
BOOL ClientToScreen(HWND hwnd, LPPOINT point);
BOOL ScreenToClient(HWND hwnd, LPPOINT point);
BOOL ClipCursor(const RECT *rect);
BOOL GetClipCursor(LPRECT rect);
HWND SetCapture(HWND hwnd);
BOOL ReleaseCapture(void);
HWND GetCapture(void);
HWND GetForegroundWindow(void);
BOOL SetForegroundWindow(HWND hwnd);
HWND GetActiveWindow(void);
HWND SetActiveWindow(HWND hwnd);
HWND SetFocus(HWND hwnd);
HWND GetFocus(void);
HWND GetDesktopWindow(void);
HWND GetParent(HWND hwnd);
HWND FindWindowA(LPCSTR cls, LPCSTR name);
BOOL EnumWindows(WNDENUMPROC proc, LPARAM data);
BOOL EnumThreadWindows(DWORD thread, WNDENUMPROC proc, LPARAM data);
DWORD GetWindowThreadProcessId(HWND hwnd, LPDWORD pid);
BOOL IsIconic(HWND hwnd);
BOOL IsZoomed(HWND hwnd);
BOOL IsWindow(HWND hwnd);
BOOL IsWindowVisible(HWND hwnd);
BOOL IsWindowEnabled(HWND hwnd);
BOOL EnableWindow(HWND hwnd, BOOL enable);
BOOL BringWindowToTop(HWND hwnd);
BOOL FlashWindow(HWND hwnd, BOOL invert);
BOOL InvalidateRect(HWND hwnd, const RECT *rect, BOOL erase);
BOOL EqualRect(const RECT *a, const RECT *b);
BOOL SetRect(LPRECT rect, int left, int top, int right, int bottom);
BOOL SetRectEmpty(LPRECT rect);
BOOL IsRectEmpty(const RECT *rect);
BOOL OffsetRect(LPRECT rect, int dx, int dy);
BOOL IntersectRect(LPRECT dst, const RECT *a, const RECT *b);
BOOL PtInRect(const RECT *rect, POINT pt);
BOOL ValidateRect(HWND hwnd, const RECT *rect);
HDC BeginPaint(HWND hwnd, LPPAINTSTRUCT ps);
BOOL EndPaint(HWND hwnd, const PAINTSTRUCT *ps);
int GetSystemMetrics(int index);
BOOL SystemParametersInfoA(UINT action, UINT param, PVOID data, UINT winIni);
#define SystemParametersInfo SystemParametersInfoA
HMONITOR MonitorFromWindow(HWND hwnd, DWORD flags);
HMONITOR MonitorFromPoint(POINT pt, DWORD flags);
HMONITOR MonitorFromRect(LPCRECT rect, DWORD flags);
BOOL GetMonitorInfoA(HMONITOR monitor, LPMONITORINFO info);
#define GetMonitorInfo GetMonitorInfoA
BOOL EnumDisplayMonitors(HDC hdc, LPCRECT clip, MONITORENUMPROC proc, LPARAM data);
BOOL EnumDisplaySettingsA(LPCSTR device, DWORD mode, DEVMODEA *dm);
#define EnumDisplaySettings EnumDisplaySettingsA
BOOL EnumDisplayDevicesA(LPCSTR device, DWORD index, PDISPLAY_DEVICEA dd, DWORD flags);
LONG ChangeDisplaySettingsA(DEVMODEA *dm, DWORD flags);
LONG ChangeDisplaySettingsExA(LPCSTR device, DEVMODEA *dm, HWND hwnd, DWORD flags, LPVOID param);
#define ChangeDisplaySettings ChangeDisplaySettingsA
HDC GetDC(HWND hwnd);
HDC GetWindowDC(HWND hwnd);
int ReleaseDC(HWND hwnd, HDC hdc);
HDC CreateDCA(LPCSTR driver, LPCSTR device, LPCSTR port, const DEVMODEA *dm);
BOOL DeleteDC(HDC hdc);
int GetDeviceCaps(HDC hdc, int index);
BOOL SetDeviceGammaRamp(HDC hdc, LPVOID ramp);   // web: FALSE (no gamma ramps)
BOOL GetDeviceGammaRamp(HDC hdc, LPVOID ramp);
COLORREF SetTextColor(HDC hdc, COLORREF color);
COLORREF SetBkColor(HDC hdc, COLORREF color);
int SetBkMode(HDC hdc, int mode);
BOOL OpenClipboard(HWND owner);                   // web: no clipboard access from the engine (FALSE)
BOOL CloseClipboard(void);
BOOL EmptyClipboard(void);
HANDLE GetClipboardData(UINT format);
HANDLE SetClipboardData(UINT format, HANDLE mem);
BOOL IsClipboardFormatAvailable(UINT format);
SHORT GetAsyncKeyState(int vkey);                 // web: 0 (key state comes from the engine's own key events)
SHORT GetKeyState(int vkey);
BOOL GetKeyboardState(PBYTE state);
UINT MapVirtualKeyA(UINT code, UINT type);
#define MapVirtualKey MapVirtualKeyA
int ToAscii(UINT vkey, UINT scan, const BYTE *state, LPWORD out, UINT flags);
HKL GetKeyboardLayout(DWORD thread);
HHOOK SetWindowsHookExA(int id, HOOKPROC proc, HINSTANCE mod, DWORD thread);
#define SetWindowsHookEx SetWindowsHookExA
BOOL UnhookWindowsHookEx(HHOOK hook);
LRESULT CallNextHookEx(HHOOK hook, int code, WPARAM wParam, LPARAM lParam);
UINT_PTR SetTimer(HWND hwnd, UINT_PTR id, UINT elapse, void *proc);
BOOL KillTimer(HWND hwnd, UINT_PTR id);
BOOL MessageBeep(UINT type);
DWORD GetQueueStatus(UINT flags);
DWORD MsgWaitForMultipleObjects(DWORD count, const HANDLE *handles, BOOL waitAll, DWORD ms, DWORD wakeMask);
HWND CreateDialogParamA(HINSTANCE inst, LPCSTR tmpl, HWND parent, DLGPROC proc, LPARAM init);
INT_PTR DialogBoxParamA(HINSTANCE inst, LPCSTR tmpl, HWND parent, DLGPROC proc, LPARAM init);
BOOL EndDialog(HWND dlg, INT_PTR result);
HWND GetDlgItem(HWND dlg, int id);
BOOL SetDlgItemTextA(HWND dlg, int id, LPCSTR text);
UINT GetDlgItemTextA(HWND dlg, int id, LPSTR text, int max);
#define QS_ALLINPUT 0x04FF
#define IMAGE_BITMAP 0
#define IMAGE_ICON 1
#define LR_LOADFROMFILE 0x00000010
#define LR_DEFAULTSIZE 0x00000040

// web build glue (win_gui.cpp): the page canvas size, and whether the engine was asked to quit through the window
void bo1_web_canvas_size(int *width, int *height);
void bo1_web_set_canvas_size(int width, int height);
void bo1_web_set_command_line(const char *cmdline);
void bo1_web_set_exe_dir(const char *dir);

// mmsystem (winmm): joysticks / waveOut are not used by the web build
#define JOYERR_NOERROR 0
#define JOYERR_UNPLUGGED 167

// ----- misc kernel32 / user32 additions -----
#define ARRAYSIZE(a) (sizeof(a) / sizeof((a)[0]))
#define THREAD_QUERY_INFORMATION 0x0040
#define THREAD_QUERY_LIMITED_INFORMATION 0x0800
#define THREAD_SET_INFORMATION 0x0020
HANDLE OpenThread(DWORD access, BOOL inherit, DWORD threadId);   // web: NULL
BOOL QueryThreadCycleTime(HANDLE thread, PULONG64 cycles);       // web: a monotonic nanosecond clock
BOOL QueryProcessCycleTime(HANDLE process, PULONG64 cycles);
BOOL GetThreadTimes(HANDLE thread, LPFILETIME creation, LPFILETIME exit, LPFILETIME kernel, LPFILETIME user);
BOOL GetProcessTimes(HANDLE process, LPFILETIME creation, LPFILETIME exit, LPFILETIME kernel, LPFILETIME user);
BOOL IsProcessDPIAware(void);
BOOL GetProcessHandleCount(HANDLE process, PDWORD count);
BOOL GetSystemTimes(LPFILETIME idle, LPFILETIME kernel, LPFILETIME user);
DWORD GetCurrentProcessorNumber(void);   // web: 0
#define GET_MODULE_HANDLE_EX_FLAG_PIN 0x00000001
#define GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT 0x00000002
#define GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS 0x00000004
BOOL GetModuleHandleExA(DWORD flags, LPCSTR name, HMODULE *module);   // web: FALSE
BOOL SetProcessDPIAware(void);
typedef enum _CPU_SET_INFORMATION_TYPE { CpuSetInformation } CPU_SET_INFORMATION_TYPE;
typedef struct _SYSTEM_CPU_SET_INFORMATION {
    DWORD Size;
    CPU_SET_INFORMATION_TYPE Type;
    union {
        struct {
            DWORD Id;
            WORD Group;
            BYTE LogicalProcessorIndex;
            BYTE CoreIndex;
            BYTE LastLevelCacheIndex;
            BYTE NumaNodeIndex;
            BYTE EfficiencyClass;
            union { BYTE AllFlags; struct { BYTE Parked : 1; BYTE Allocated : 1; BYTE AllocatedToTargetProcess : 1; BYTE RealTime : 1; BYTE ReservedFlags : 4; }; };
            union { DWORD Reserved; BYTE SchedulingClass; };
            DWORD64 AllocationTag;
        } CpuSet;
    };
} SYSTEM_CPU_SET_INFORMATION, *PSYSTEM_CPU_SET_INFORMATION;
BOOL GetSystemCpuSetInformation(PSYSTEM_CPU_SET_INFORMATION info, ULONG length, PULONG returned, HANDLE process, ULONG flags);  // web: FALSE
typedef enum _LOGICAL_PROCESSOR_RELATIONSHIP { RelationProcessorCore, RelationNumaNode, RelationCache, RelationProcessorPackage, RelationGroup, RelationAll = 0xffff } LOGICAL_PROCESSOR_RELATIONSHIP;
typedef struct _SYSTEM_LOGICAL_PROCESSOR_INFORMATION {
    ULONG_PTR ProcessorMask;
    LOGICAL_PROCESSOR_RELATIONSHIP Relationship;
    union { struct { BYTE Flags; } ProcessorCore; struct { DWORD NodeNumber; } NumaNode; ULONGLONG Reserved[2]; };
} SYSTEM_LOGICAL_PROCESSOR_INFORMATION, *PSYSTEM_LOGICAL_PROCESSOR_INFORMATION;
BOOL GetLogicalProcessorInformation(PSYSTEM_LOGICAL_PROCESSOR_INFORMATION buffer, PDWORD length);  // web: FALSE

BO1_EXTERN_C_END

// Winsock 1 comes with <windows.h> (as it does without WIN32_LEAN_AND_MEAN)
#include "winsock2.h"

#endif // BO1_WEB_WINDOWS_H
