// tlhelp32.h - process / thread snapshots. web: CreateToolhelp32Snapshot fails (INVALID_HANDLE_VALUE).
#pragma once
#include "windows.h"
#define TH32CS_SNAPHEAPLIST 0x00000001
#define TH32CS_SNAPPROCESS 0x00000002
#define TH32CS_SNAPTHREAD 0x00000004
#define TH32CS_SNAPMODULE 0x00000008
#define MAX_MODULE_NAME32 255
typedef struct tagMODULEENTRY32 {
    DWORD dwSize, th32ModuleID, th32ProcessID, GlblcntUsage, ProccntUsage;
    BYTE *modBaseAddr;
    DWORD modBaseSize;
    HMODULE hModule;
    char szModule[MAX_MODULE_NAME32 + 1];
    char szExePath[MAX_PATH];
} MODULEENTRY32, *PMODULEENTRY32, *LPMODULEENTRY32;
typedef struct tagTHREADENTRY32 { DWORD dwSize, cntUsage, th32ThreadID, th32OwnerProcessID; LONG tpBasePri, tpDeltaPri; DWORD dwFlags; } THREADENTRY32, *LPTHREADENTRY32;
typedef struct tagPROCESSENTRY32 {
    DWORD dwSize, cntUsage, th32ProcessID;
    ULONG_PTR th32DefaultHeapID;
    DWORD th32ModuleID, cntThreads, th32ParentProcessID;
    LONG pcPriClassBase;
    DWORD dwFlags;
    CHAR szExeFile[MAX_PATH];
} PROCESSENTRY32, *LPPROCESSENTRY32;
BO1_EXTERN_C_BEGIN
HANDLE CreateToolhelp32Snapshot(DWORD flags, DWORD pid);
BOOL Module32First(HANDLE snapshot, LPMODULEENTRY32 me);
BOOL Module32Next(HANDLE snapshot, LPMODULEENTRY32 me);
BOOL Thread32First(HANDLE snapshot, LPTHREADENTRY32 te);
BOOL Thread32Next(HANDLE snapshot, LPTHREADENTRY32 te);
BOOL Process32First(HANDLE snapshot, LPPROCESSENTRY32 pe);
BOOL Process32Next(HANDLE snapshot, LPPROCESSENTRY32 pe);
HANDLE OpenThread(DWORD access, BOOL inherit, DWORD threadId);
BO1_EXTERN_C_END
