// ShlObj.h - shell folders. web: SHGetFolderPathA answers the engine's home path (fs_h default) for every folder.
#pragma once
#include "windows.h"
#define CSIDL_PERSONAL 0x0005
#define CSIDL_APPDATA 0x001a
#define CSIDL_LOCAL_APPDATA 0x001c
#define CSIDL_COMMON_APPDATA 0x0023
#define CSIDL_MYDOCUMENTS CSIDL_PERSONAL
#define CSIDL_FLAG_CREATE 0x8000
#define SHGFP_TYPE_CURRENT 0
BO1_EXTERN_C HRESULT SHGetFolderPathA(HWND hwnd, int csidl, HANDLE token, DWORD flags, LPSTR path);
#define SHGetFolderPath SHGetFolderPathA
