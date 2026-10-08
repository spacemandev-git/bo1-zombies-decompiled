// io.h - MSVC low-level I/O and _findfirst. Declarations shared with bo1_web_prelude.h; _find* in win_file.cpp.
#pragma once
#include "bo1_web_prelude.h"
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#define _A_NORMAL 0x00
#define _A_RDONLY 0x01
#define _A_HIDDEN 0x02
#define _A_SYSTEM 0x04
#define _A_SUBDIR 0x10
#define _A_ARCH 0x20

BO1_EXTERN_C_BEGIN
typedef long long __bo1_time64;
struct _finddata_t { unsigned attrib; long time_create; long time_access; long time_write; unsigned long size; char name[260]; };
struct _finddata64i32_t { unsigned attrib; __bo1_time64 time_create; __bo1_time64 time_access; __bo1_time64 time_write; unsigned long size; char name[260]; };
struct __finddata64_t { unsigned attrib; __bo1_time64 time_create; __bo1_time64 time_access; __bo1_time64 time_write; long long size; char name[260]; };
// web: patterns are '<dir>\<wildcard>' (* and ?), matched case-insensitively; the directory resolves case-insensitively
intptr_t _findfirst(const char *pattern, struct _finddata_t *data);
int _findnext(intptr_t handle, struct _finddata_t *data);
intptr_t _findfirst64i32(const char *pattern, struct _finddata64i32_t *data);
int _findnext64i32(intptr_t handle, struct _finddata64i32_t *data);
intptr_t _findfirst64(const char *pattern, struct __finddata64_t *data);
int _findnext64(intptr_t handle, struct __finddata64_t *data);
int _findclose(intptr_t handle);
int _open(const char *path, int flags, ...);
int _close(int fd);
int _read(int fd, void *buf, unsigned int count);
int _write(int fd, const void *buf, unsigned int count);
long _lseek(int fd, long offset, int origin);
long long _lseeki64(int fd, long long offset, int origin);
long _tell(int fd);
int _commit(int fd);
int _eof(int fd);
int _isatty(int fd);
BO1_EXTERN_C_END
#define _S_IREAD 0000400
#define _S_IWRITE 0000200
#define _S_IFDIR S_IFDIR
#define _S_IFREG S_IFREG
