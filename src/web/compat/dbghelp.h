// dbghelp.h - symbol / stack walk API: types only (web: LoadLibraryA("dbghelp.dll") fails, so nothing is called).
#pragma once
#include "windows.h"
typedef struct _SYMBOL_INFO {
    ULONG SizeOfStruct;
    ULONG TypeIndex;
    ULONG64 Reserved[2];
    ULONG Index;
    ULONG Size;
    ULONG64 ModBase;
    ULONG Flags;
    ULONG64 Value;
    ULONG64 Address;
    ULONG Register;
    ULONG Scope;
    ULONG Tag;
    ULONG NameLen;
    ULONG MaxNameLen;
    CHAR Name[1];
} SYMBOL_INFO, *PSYMBOL_INFO;
typedef struct _IMAGEHLP_LINE { DWORD SizeOfStruct; PVOID Key; DWORD LineNumber; PCHAR FileName; DWORD Address; } IMAGEHLP_LINE, *PIMAGEHLP_LINE;
typedef struct _IMAGEHLP_LINE64 { DWORD SizeOfStruct; PVOID Key; DWORD LineNumber; PCHAR FileName; DWORD64 Address; } IMAGEHLP_LINE64, *PIMAGEHLP_LINE64;
typedef enum { AddrMode1616, AddrMode1632, AddrModeReal, AddrModeFlat } ADDRESS_MODE;
typedef struct _tagADDRESS64 { DWORD64 Offset; WORD Segment; ADDRESS_MODE Mode; } ADDRESS64, *LPADDRESS64;
typedef struct _tagSTACKFRAME64 {
    ADDRESS64 AddrPC, AddrReturn, AddrFrame, AddrStack, AddrBStore;
    PVOID FuncTableEntry;
    DWORD64 Params[4];
    BOOL Far, Virtual;
    DWORD64 Reserved[3];
    char KdHelp[0x100];
} STACKFRAME64, *LPSTACKFRAME64;
typedef BOOL(CALLBACK *PREAD_PROCESS_MEMORY_ROUTINE64)(HANDLE, DWORD64, PVOID, DWORD, LPDWORD);
typedef PVOID(CALLBACK *PFUNCTION_TABLE_ACCESS_ROUTINE64)(HANDLE, DWORD64);
typedef DWORD64(CALLBACK *PGET_MODULE_BASE_ROUTINE64)(HANDLE, DWORD64);
typedef DWORD64(CALLBACK *PTRANSLATE_ADDRESS_ROUTINE64)(HANDLE, HANDLE, LPADDRESS64);
#define IMAGE_FILE_MACHINE_I386 0x014c
#define SYMOPT_UNDNAME 0x00000002
#define SYMOPT_DEFERRED_LOADS 0x00000004
#define SYMOPT_LOAD_LINES 0x00000010
#define MAX_SYM_NAME 2000
typedef enum _MINIDUMP_TYPE { MiniDumpNormal = 0, MiniDumpWithDataSegs = 1, MiniDumpWithFullMemory = 2, MiniDumpWithHandleData = 4 } MINIDUMP_TYPE;
