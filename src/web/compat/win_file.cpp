// win_file.cpp - Win32 / MSVC CRT file APIs over POSIX file descriptors (web build). Every path goes through the
// case-insensitive resolver (web_path.cpp), so "\", "/" and any letter case work.
//
// Overlapped I/O completes synchronously: ReadFile/WriteFile with an OVERLAPPED read/write at its offset and set its
// event; ReadFileEx/WriteFileEx queue their completion routine as an APC of the calling thread (win_sync.cpp), which
// runs in that thread's next alertable wait - the fastfile loader (database/db_file_load.cpp) depends on this.
//
// fopen / remove / rename: the link wraps them (-Wl,--wrap=..., cmake/web.cmake) so the engine's plain C calls get
// the same path resolution.
#include "bo1_win_internal.h"
#include <io.h>
#include <direct.h>
#include <ShlObj.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string>
#include <vector>

using namespace bo1w;

namespace
{
struct File : Object
{
    int fd = -1;
    std::string path;
    File() : Object(KIND_FILE) {}
    ~File() override
    {
        if (fd >= 0)
            close(fd);
    }
};

struct FindEntry
{
    std::string name;
    bool dir;
    uint64_t size;
    int64_t mtime;
};

struct Find : Object
{
    std::vector<FindEntry> entries;
    size_t next = 0;
    Find() : Object(KIND_FIND) {}
};

void SetErrnoError(int e)
{
    SetLastError(ErrnoToWin32(e));
}

Find *StartFind(const char *pattern)
{
    // "<dir>\<wildcard>" or "<wildcard>"
    std::string p(pattern ? pattern : "");
    for (char &c : p)
        if (c == '\\')
            c = '/';
    const size_t slash = p.find_last_of('/');
    std::string dir = slash == std::string::npos ? std::string(".") : p.substr(0, slash);
    const std::string wild = slash == std::string::npos ? p : p.substr(slash + 1);
    if (dir.empty())
        dir = "/";
    char real[1024];
    if (!ResolvePath(dir.c_str(), real, sizeof(real)))
    {
        SetLastError(ERROR_PATH_NOT_FOUND);
        return nullptr;
    }
    DIR *d = opendir(real);
    if (!d)
    {
        SetErrnoError(errno);
        return nullptr;
    }
    Find *f = new Find();
    // Windows lists "." and ".." in every directory but the root; the engine skips them
    if (WildcardMatch(wild.c_str(), "."))
    {
        f->entries.push_back({".", true, 0, 0});
        f->entries.push_back({"..", true, 0, 0});
    }
    while (dirent *e = readdir(d))
    {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        if (!WildcardMatch(wild.c_str(), e->d_name))
            continue;
        std::string full = std::string(real) + "/" + e->d_name;
        struct stat st;
        FindEntry fe{e->d_name, false, 0, 0};
        if (stat(full.c_str(), &st) == 0)
        {
            fe.dir = S_ISDIR(st.st_mode);
            fe.size = (uint64_t)st.st_size;
            fe.mtime = (int64_t)st.st_mtime;
        }
        f->entries.push_back(fe);
    }
    closedir(d);
    if (f->entries.empty())
    {
        delete f;
        SetLastError(ERROR_FILE_NOT_FOUND);
        return nullptr;
    }
    return f;
}

void ToFileTime(int64_t unixSeconds, FILETIME *ft)
{
    const uint64_t v = UnixNsToFileTime(unixSeconds, 0);
    ft->dwLowDateTime = (DWORD)v;
    ft->dwHighDateTime = (DWORD)(v >> 32);
}

void FillFindData(const FindEntry &e, WIN32_FIND_DATAA *data)
{
    memset(data, 0, sizeof(*data));
    data->dwFileAttributes = e.dir ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_ARCHIVE;
    ToFileTime(e.mtime, &data->ftLastWriteTime);
    data->ftCreationTime = data->ftLastWriteTime;
    data->ftLastAccessTime = data->ftLastWriteTime;
    data->nFileSizeHigh = (DWORD)(e.size >> 32);
    data->nFileSizeLow = (DWORD)e.size;
    snprintf(data->cFileName, sizeof(data->cFileName), "%s", e.name.c_str());
}

template <class T> void FillFinddata(const FindEntry &e, T *data)
{
    memset(data, 0, sizeof(*data));
    data->attrib = e.dir ? _A_SUBDIR : _A_ARCH;
    data->time_create = data->time_access = data->time_write = e.mtime;
    data->size = (decltype(data->size))e.size;
    snprintf(data->name, sizeof(data->name), "%s", e.name.c_str());
}

std::string Resolved(const char *path, bool *exists = nullptr)
{
    char real[1024];
    const bool e = ResolvePath(path, real, sizeof(real));
    if (exists)
        *exists = e;
    return real;
}
}

extern "C" {

// ----- wrapped C library calls (-Wl,--wrap=fopen,--wrap=remove,--wrap=rename) -----
FILE *__real_fopen(const char *name, const char *mode);
int __real_remove(const char *path);
int __real_rename(const char *from, const char *to);

FILE *__wrap_fopen(const char *name, const char *mode)
{
    if (!name)
        return __real_fopen(name, mode);
    bool exists;
    const std::string real = Resolved(name, &exists);
    FILE *f = __real_fopen(real.c_str(), mode);
    if (f && !exists)
        InvalidateDirOf(real.c_str());
    return f;
}

int __wrap_remove(const char *path)
{
    const std::string real = Resolved(path);
    const int r = __real_remove(real.c_str());
    InvalidateDirOf(real.c_str());
    return r;
}

int __wrap_rename(const char *from, const char *to)
{
    const std::string a = Resolved(from), b = Resolved(to);
    const int r = __real_rename(a.c_str(), b.c_str());
    InvalidateDirOf(a.c_str());
    InvalidateDirOf(b.c_str());
    return r;
}

// ----- CreateFile & handle I/O -----
HANDLE CreateFileA(LPCSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disposition, DWORD flags, HANDLE templ)
{
    if (!name)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }
    bool exists;
    const std::string real = Resolved(name, &exists);
    int oflags;
    const bool wantRead = (access & (GENERIC_READ | FILE_READ_DATA | GENERIC_ALL)) != 0;
    const bool wantWrite = (access & (GENERIC_WRITE | FILE_WRITE_DATA | FILE_APPEND_DATA | GENERIC_ALL)) != 0;
    if (wantRead && wantWrite)
        oflags = O_RDWR;
    else if (wantWrite)
        oflags = O_WRONLY;
    else
        oflags = O_RDONLY;
    if ((access & FILE_APPEND_DATA) && !(access & (GENERIC_WRITE | FILE_WRITE_DATA)))
        oflags |= O_APPEND;
    switch (disposition)
    {
    case CREATE_NEW:
        oflags |= O_CREAT | O_EXCL;
        break;
    case CREATE_ALWAYS:
        oflags |= O_CREAT | O_TRUNC;
        break;
    case OPEN_ALWAYS:
        oflags |= O_CREAT;
        break;
    case TRUNCATE_EXISTING:
        oflags |= O_TRUNC;
        break;
    case OPEN_EXISTING:
    default:
        break;
    }
    const int fd = open(real.c_str(), oflags, 0666);
    if (fd < 0)
    {
        SetErrnoError(errno);
        return INVALID_HANDLE_VALUE;
    }
    if (!exists)
        InvalidateDirOf(real.c_str());
    File *f = new File();
    f->fd = fd;
    f->path = real;
    // Windows: OPEN_ALWAYS / CREATE_ALWAYS on an existing file succeed with ERROR_ALREADY_EXISTS
    SetLastError(exists && (disposition == OPEN_ALWAYS || disposition == CREATE_ALWAYS) ? ERROR_ALREADY_EXISTS : ERROR_SUCCESS);
    return f;
}

BOOL ReadFile(HANDLE file, LPVOID buffer, DWORD toRead, LPDWORD read, LPOVERLAPPED overlapped)
{
    File *f = As<File>(file, KIND_FILE);
    if (!f)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    ssize_t n;
    if (overlapped)
    {
        const off_t offset = (off_t)(((uint64_t)overlapped->OffsetHigh << 32) | overlapped->Offset);
        n = pread(f->fd, buffer, toRead, offset);
    }
    else
        n = ::read(f->fd, buffer, toRead);
    if (n < 0)
    {
        SetErrnoError(errno);
        if (read)
            *read = 0;
        return FALSE;
    }
    if (read)
        *read = (DWORD)n;
    if (overlapped)
    {
        overlapped->Internal = 0;
        overlapped->InternalHigh = (ULONG_PTR)n;
        if (overlapped->hEvent)
            SetEvent(overlapped->hEvent);
    }
    return TRUE;
}

BOOL ReadFileEx(HANDLE file, LPVOID buffer, DWORD toRead, LPOVERLAPPED overlapped, LPOVERLAPPED_COMPLETION_ROUTINE done)
{
    File *f = As<File>(file, KIND_FILE);
    if (!f || !overlapped)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    const off_t offset = (off_t)(((uint64_t)overlapped->OffsetHigh << 32) | overlapped->Offset);
    struct stat st;
    if (fstat(f->fd, &st) == 0 && offset >= st.st_size)
    {
        SetLastError(ERROR_HANDLE_EOF);
        return FALSE;
    }
    const ssize_t n = pread(f->fd, buffer, toRead, offset);
    if (n < 0)
    {
        SetErrnoError(errno);
        return FALSE;
    }
    overlapped->Internal = 0;
    overlapped->InternalHigh = (ULONG_PTR)n;
    QueueIoCompletion(done, 0, (DWORD)n, overlapped);
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

BOOL WriteFile(HANDLE file, LPCVOID buffer, DWORD toWrite, LPDWORD written, LPOVERLAPPED overlapped)
{
    File *f = As<File>(file, KIND_FILE);
    if (!f)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    ssize_t n;
    if (overlapped)
    {
        const off_t offset = (off_t)(((uint64_t)overlapped->OffsetHigh << 32) | overlapped->Offset);
        n = pwrite(f->fd, buffer, toWrite, offset);
    }
    else
        n = ::write(f->fd, buffer, toWrite);
    if (n < 0)
    {
        SetErrnoError(errno);
        if (written)
            *written = 0;
        return FALSE;
    }
    if (written)
        *written = (DWORD)n;
    if (overlapped)
    {
        overlapped->Internal = 0;
        overlapped->InternalHigh = (ULONG_PTR)n;
        if (overlapped->hEvent)
            SetEvent(overlapped->hEvent);
    }
    return TRUE;
}

BOOL WriteFileEx(HANDLE file, LPCVOID buffer, DWORD toWrite, LPOVERLAPPED overlapped, LPOVERLAPPED_COMPLETION_ROUTINE done)
{
    File *f = As<File>(file, KIND_FILE);
    if (!f || !overlapped)
        return FALSE;
    const off_t offset = (off_t)(((uint64_t)overlapped->OffsetHigh << 32) | overlapped->Offset);
    const ssize_t n = pwrite(f->fd, buffer, toWrite, offset);
    if (n < 0)
    {
        SetErrnoError(errno);
        return FALSE;
    }
    overlapped->Internal = 0;
    overlapped->InternalHigh = (ULONG_PTR)n;
    QueueIoCompletion(done, 0, (DWORD)n, overlapped);
    return TRUE;
}

BOOL GetOverlappedResult(HANDLE file, LPOVERLAPPED overlapped, LPDWORD transferred, BOOL wait)
{
    if (!overlapped)
        return FALSE;
    if (transferred)
        *transferred = (DWORD)overlapped->InternalHigh;
    return overlapped->Internal == 0;
}

BOOL CancelIo(HANDLE file)
{
    return TRUE;
}

BOOL FlushFileBuffers(HANDLE file)
{
    File *f = As<File>(file, KIND_FILE);
    return f && fsync(f->fd) == 0;
}

DWORD GetFileSize(HANDLE file, LPDWORD sizeHigh)
{
    File *f = As<File>(file, KIND_FILE);
    struct stat st;
    if (!f || fstat(f->fd, &st) != 0)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return INVALID_FILE_SIZE;
    }
    if (sizeHigh)
        *sizeHigh = (DWORD)((uint64_t)st.st_size >> 32);
    return (DWORD)st.st_size;
}

BOOL GetFileSizeEx(HANDLE file, PLARGE_INTEGER size)
{
    File *f = As<File>(file, KIND_FILE);
    struct stat st;
    if (!f || !size || fstat(f->fd, &st) != 0)
        return FALSE;
    size->QuadPart = st.st_size;
    return TRUE;
}

DWORD SetFilePointer(HANDLE file, LONG distance, PLONG distanceHigh, DWORD method)
{
    File *f = As<File>(file, KIND_FILE);
    if (!f)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return INVALID_SET_FILE_POINTER;
    }
    int64_t move = distanceHigh ? (int64_t)(((uint64_t)(uint32_t)*distanceHigh << 32) | (uint32_t)distance) : (int64_t)distance;
    const int whence = method == FILE_CURRENT ? SEEK_CUR : method == FILE_END ? SEEK_END : SEEK_SET;
    const off_t r = lseek(f->fd, (off_t)move, whence);
    if (r < 0)
    {
        SetErrnoError(errno);
        return INVALID_SET_FILE_POINTER;
    }
    if (distanceHigh)
        *distanceHigh = (LONG)((uint64_t)r >> 32);
    SetLastError(ERROR_SUCCESS);
    return (DWORD)r;
}

BOOL SetFilePointerEx(HANDLE file, LARGE_INTEGER distance, PLARGE_INTEGER newPointer, DWORD method)
{
    File *f = As<File>(file, KIND_FILE);
    if (!f)
        return FALSE;
    const int whence = method == FILE_CURRENT ? SEEK_CUR : method == FILE_END ? SEEK_END : SEEK_SET;
    const off_t r = lseek(f->fd, (off_t)distance.QuadPart, whence);
    if (r < 0)
        return FALSE;
    if (newPointer)
        newPointer->QuadPart = r;
    return TRUE;
}

BOOL SetEndOfFile(HANDLE file)
{
    File *f = As<File>(file, KIND_FILE);
    if (!f)
        return FALSE;
    const off_t pos = lseek(f->fd, 0, SEEK_CUR);
    return pos >= 0 && ftruncate(f->fd, pos) == 0;
}

BOOL GetFileTime(HANDLE file, LPFILETIME creation, LPFILETIME access, LPFILETIME write)
{
    File *f = As<File>(file, KIND_FILE);
    struct stat st;
    if (!f || fstat(f->fd, &st) != 0)
        return FALSE;
    if (creation)
        ToFileTime(st.st_mtime, creation);
    if (access)
        ToFileTime(st.st_atime, access);
    if (write)
        ToFileTime(st.st_mtime, write);
    return TRUE;
}

BOOL SetFileTime(HANDLE file, const FILETIME *creation, const FILETIME *access, const FILETIME *write)
{
    return TRUE;
}

BOOL GetFileInformationByHandle(HANDLE file, LPBY_HANDLE_FILE_INFORMATION info)
{
    File *f = As<File>(file, KIND_FILE);
    struct stat st;
    if (!f || !info || fstat(f->fd, &st) != 0)
        return FALSE;
    memset(info, 0, sizeof(*info));
    info->dwFileAttributes = S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_ARCHIVE;
    ToFileTime(st.st_mtime, &info->ftLastWriteTime);
    info->ftCreationTime = info->ftLastAccessTime = info->ftLastWriteTime;
    info->nFileSizeHigh = (DWORD)((uint64_t)st.st_size >> 32);
    info->nFileSizeLow = (DWORD)st.st_size;
    info->nNumberOfLinks = 1;
    return TRUE;
}

DWORD GetFileType(HANDLE file)
{
    return As<File>(file, KIND_FILE) ? FILE_TYPE_DISK : FILE_TYPE_UNKNOWN;
}

// ----- paths -----
BOOL DeleteFileA(LPCSTR name)
{
    const std::string real = Resolved(name);
    if (unlink(real.c_str()) != 0)
    {
        SetErrnoError(errno);
        return FALSE;
    }
    InvalidateDirOf(real.c_str());
    return TRUE;
}

BOOL MoveFileExA(LPCSTR from, LPCSTR to, DWORD flags)
{
    bool targetExists;
    const std::string a = Resolved(from), b = Resolved(to, &targetExists);
    if (targetExists && !(flags & MOVEFILE_REPLACE_EXISTING))
    {
        SetLastError(ERROR_ALREADY_EXISTS);
        return FALSE;
    }
    if (rename(a.c_str(), b.c_str()) != 0)
    {
        SetErrnoError(errno);
        return FALSE;
    }
    InvalidateDirOf(a.c_str());
    InvalidateDirOf(b.c_str());
    return TRUE;
}

BOOL MoveFileA(LPCSTR from, LPCSTR to)
{
    return MoveFileExA(from, to, 0);
}

BOOL CopyFileA(LPCSTR from, LPCSTR to, BOOL failIfExists)
{
    bool targetExists;
    const std::string a = Resolved(from), b = Resolved(to, &targetExists);
    if (targetExists && failIfExists)
    {
        SetLastError(ERROR_FILE_EXISTS);
        return FALSE;
    }
    const int in = open(a.c_str(), O_RDONLY);
    if (in < 0)
    {
        SetErrnoError(errno);
        return FALSE;
    }
    const int out = open(b.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (out < 0)
    {
        SetErrnoError(errno);
        close(in);
        return FALSE;
    }
    char buf[65536];
    ssize_t n;
    bool ok = true;
    while ((n = ::read(in, buf, sizeof(buf))) > 0)
    {
        if (::write(out, buf, (size_t)n) != n)
        {
            ok = false;
            break;
        }
    }
    close(in);
    close(out);
    InvalidateDirOf(b.c_str());
    return ok && n >= 0;
}

BOOL CreateDirectoryA(LPCSTR path, LPSECURITY_ATTRIBUTES sa)
{
    bool exists;
    const std::string real = Resolved(path, &exists);
    if (exists)
    {
        SetLastError(ERROR_ALREADY_EXISTS);
        return FALSE;
    }
    if (mkdir(real.c_str(), 0777) != 0)
    {
        SetErrnoError(errno);
        return FALSE;
    }
    InvalidateDirOf(real.c_str());
    return TRUE;
}

BOOL RemoveDirectoryA(LPCSTR path)
{
    const std::string real = Resolved(path);
    if (rmdir(real.c_str()) != 0)
    {
        SetErrnoError(errno);
        return FALSE;
    }
    InvalidateDirOf(real.c_str());
    return TRUE;
}

DWORD GetFileAttributesA(LPCSTR name)
{
    bool exists;
    const std::string real = Resolved(name, &exists);
    struct stat st;
    if (!exists || stat(real.c_str(), &st) != 0)
    {
        SetLastError(ERROR_FILE_NOT_FOUND);
        return INVALID_FILE_ATTRIBUTES;
    }
    return S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_ARCHIVE;
}

BOOL GetFileAttributesExA(LPCSTR name, GET_FILEEX_INFO_LEVELS level, LPVOID info)
{
    bool exists;
    const std::string real = Resolved(name, &exists);
    struct stat st;
    if (!exists || stat(real.c_str(), &st) != 0 || !info)
    {
        SetLastError(ERROR_FILE_NOT_FOUND);
        return FALSE;
    }
    WIN32_FILE_ATTRIBUTE_DATA *d = (WIN32_FILE_ATTRIBUTE_DATA *)info;
    memset(d, 0, sizeof(*d));
    d->dwFileAttributes = S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_ARCHIVE;
    ToFileTime(st.st_mtime, &d->ftLastWriteTime);
    d->ftCreationTime = d->ftLastAccessTime = d->ftLastWriteTime;
    d->nFileSizeHigh = (DWORD)((uint64_t)st.st_size >> 32);
    d->nFileSizeLow = (DWORD)st.st_size;
    return TRUE;
}

BOOL SetFileAttributesA(LPCSTR name, DWORD attributes)
{
    return GetFileAttributesA(name) != INVALID_FILE_ATTRIBUTES;
}

HANDLE FindFirstFileA(LPCSTR pattern, LPWIN32_FIND_DATAA data)
{
    Find *f = StartFind(pattern);
    if (!f)
        return INVALID_HANDLE_VALUE;
    FillFindData(f->entries[f->next++], data);
    return f;
}

BOOL FindNextFileA(HANDLE find, LPWIN32_FIND_DATAA data)
{
    Find *f = As<Find>(find, KIND_FIND);
    if (!f || f->next >= f->entries.size())
    {
        SetLastError(ERROR_NO_MORE_FILES);
        return FALSE;
    }
    FillFindData(f->entries[f->next++], data);
    return TRUE;
}

BOOL FindClose(HANDLE find)
{
    Find *f = As<Find>(find, KIND_FIND);
    if (!f)
        return FALSE;
    Release(f);
    return TRUE;
}

DWORD GetCurrentDirectoryA(DWORD size, LPSTR buffer)
{
    const std::string cwd = GetCwd();
    const DWORD len = (DWORD)cwd.size();
    if (!buffer || size <= len)
        return len + 1;
    memcpy(buffer, cwd.c_str(), len + 1);
    return len;
}

BOOL SetCurrentDirectoryA(LPCSTR path)
{
    if (!SetCwd(path))
    {
        SetLastError(ERROR_PATH_NOT_FOUND);
        return FALSE;
    }
    return TRUE;
}

DWORD GetFullPathNameA(LPCSTR name, DWORD size, LPSTR buffer, LPSTR *filePart)
{
    const std::string real = Resolved(name);
    const DWORD len = (DWORD)real.size();
    if (!buffer || size <= len)
        return len + 1;
    memcpy(buffer, real.c_str(), len + 1);
    if (filePart)
    {
        char *slash = strrchr(buffer, '/');
        *filePart = slash ? slash + 1 : buffer;
    }
    return len;
}

DWORD GetTempPathA(DWORD size, LPSTR buffer)
{
    const char *tmp = "/tmp/";
    const DWORD len = (DWORD)strlen(tmp);
    if (!buffer || size <= len)
        return len + 1;
    memcpy(buffer, tmp, len + 1);
    return len;
}

UINT GetTempFileNameA(LPCSTR path, LPCSTR prefix, UINT unique, LPSTR name)
{
    static std::atomic<UINT> counter{1};
    const UINT n = unique ? unique : counter.fetch_add(1);
    snprintf(name, MAX_PATH, "%s/%.3s%04X.tmp", path, prefix ? prefix : "", n & 0xFFFF);
    return n;
}

BOOL GetDiskFreeSpaceExA(LPCSTR dir, PULARGE_INTEGER freeToCaller, PULARGE_INTEGER total, PULARGE_INTEGER totalFree)
{
    // web: OPFS quota is the browser's business; report plenty (the engine only refuses to save when this is small)
    const ULONGLONG plenty = 8ull << 30;
    if (freeToCaller)
        freeToCaller->QuadPart = plenty;
    if (total)
        total->QuadPart = plenty * 2;
    if (totalFree)
        totalFree->QuadPart = plenty;
    return TRUE;
}

BOOL GetDiskFreeSpaceA(LPCSTR root, LPDWORD sectorsPerCluster, LPDWORD bytesPerSector, LPDWORD freeClusters, LPDWORD totalClusters)
{
    if (sectorsPerCluster)
        *sectorsPerCluster = 8;
    if (bytesPerSector)
        *bytesPerSector = 512;
    if (freeClusters)
        *freeClusters = 0x200000;
    if (totalClusters)
        *totalClusters = 0x400000;
    return TRUE;
}

UINT GetDriveTypeA(LPCSTR root)
{
    return DRIVE_FIXED;
}

DWORD GetLogicalDrives(void)
{
    return 1u << 2;   // C:
}

BOOL GetVolumeInformationA(LPCSTR root, LPSTR name, DWORD nameSize, LPDWORD serial, LPDWORD maxComponent, LPDWORD flags, LPSTR fsName, DWORD fsNameSize)
{
    if (name && nameSize)
        snprintf(name, nameSize, "OPFS");
    if (serial)
        *serial = 0xB01B0001;
    if (maxComponent)
        *maxComponent = 255;
    if (flags)
        *flags = 0;
    if (fsName && fsNameSize)
        snprintf(fsName, fsNameSize, "OPFS");
    return TRUE;
}

// ----- io.h -----
static intptr_t FindFirstCommon(const char *pattern, Find **out)
{
    Find *f = StartFind(pattern);
    *out = f;
    if (!f)
    {
        errno = ENOENT;
        return -1;
    }
    return (intptr_t)f;
}

intptr_t _findfirst(const char *pattern, struct _finddata_t *data)
{
    Find *f;
    if (FindFirstCommon(pattern, &f) == -1)
        return -1;
    FillFinddata(f->entries[f->next++], data);
    return (intptr_t)f;
}

int _findnext(intptr_t handle, struct _finddata_t *data)
{
    Find *f = As<Find>((HANDLE)handle, KIND_FIND);
    if (!f || f->next >= f->entries.size())
    {
        errno = ENOENT;
        return -1;
    }
    FillFinddata(f->entries[f->next++], data);
    return 0;
}

intptr_t _findfirst64i32(const char *pattern, struct _finddata64i32_t *data)
{
    Find *f;
    if (FindFirstCommon(pattern, &f) == -1)
        return -1;
    FillFinddata(f->entries[f->next++], data);
    return (intptr_t)f;
}

int _findnext64i32(intptr_t handle, struct _finddata64i32_t *data)
{
    Find *f = As<Find>((HANDLE)handle, KIND_FIND);
    if (!f || f->next >= f->entries.size())
    {
        errno = ENOENT;
        return -1;
    }
    FillFinddata(f->entries[f->next++], data);
    return 0;
}

intptr_t _findfirst64(const char *pattern, struct __finddata64_t *data)
{
    Find *f;
    if (FindFirstCommon(pattern, &f) == -1)
        return -1;
    FillFinddata(f->entries[f->next++], data);
    return (intptr_t)f;
}

int _findnext64(intptr_t handle, struct __finddata64_t *data)
{
    Find *f = As<Find>((HANDLE)handle, KIND_FIND);
    if (!f || f->next >= f->entries.size())
    {
        errno = ENOENT;
        return -1;
    }
    FillFinddata(f->entries[f->next++], data);
    return 0;
}

int _findclose(intptr_t handle)
{
    Find *f = As<Find>((HANDLE)handle, KIND_FIND);
    if (!f)
        return -1;
    Release(f);
    return 0;
}

int _open(const char *path, int flags, ...)
{
    int mode = 0666;
    if (flags & _O_CREAT)
    {
        va_list ap;
        va_start(ap, flags);
        mode = va_arg(ap, int);
        va_end(ap);
    }
    int o = 0;
    if ((flags & 3) == _O_WRONLY)
        o = O_WRONLY;
    else if ((flags & 3) == _O_RDWR)
        o = O_RDWR;
    if (flags & _O_APPEND)
        o |= O_APPEND;
    if (flags & _O_CREAT)
        o |= O_CREAT;
    if (flags & _O_TRUNC)
        o |= O_TRUNC;
    if (flags & _O_EXCL)
        o |= O_EXCL;
    bool exists;
    const std::string real = Resolved(path, &exists);
    const int fd = open(real.c_str(), o, mode);
    if (fd >= 0 && !exists)
        InvalidateDirOf(real.c_str());
    return fd;
}

int _close(int fd)
{
    return close(fd);
}

int _read(int fd, void *buf, unsigned int count)
{
    return (int)::read(fd, buf, count);
}

int _write(int fd, const void *buf, unsigned int count)
{
    return (int)::write(fd, buf, count);
}

long _lseek(int fd, long offset, int origin)
{
    return (long)lseek(fd, offset, origin);
}

long long _lseeki64(int fd, long long offset, int origin)
{
    return (long long)lseek(fd, (off_t)offset, origin);
}

long _tell(int fd)
{
    return (long)lseek(fd, 0, SEEK_CUR);
}

int _commit(int fd)
{
    return fsync(fd);
}

int _eof(int fd)
{
    const off_t cur = lseek(fd, 0, SEEK_CUR);
    struct stat st;
    if (cur < 0 || fstat(fd, &st) != 0)
        return -1;
    return cur >= st.st_size ? 1 : 0;
}

int _isatty(int fd)
{
    return isatty(fd);
}

// ----- direct.h -----
int _mkdir(const char *path)
{
    bool exists;
    const std::string real = Resolved(path, &exists);
    if (exists)
    {
        errno = EEXIST;
        return -1;
    }
    const int r = mkdir(real.c_str(), 0777);
    if (r == 0)
        InvalidateDirOf(real.c_str());
    return r;
}

char *_getcwd(char *buf, int size)
{
    const std::string cwd = GetCwd();
    if (!buf)
        return strdup(cwd.c_str());
    if (size <= 0 || (size_t)size <= cwd.size())
    {
        errno = ERANGE;
        return nullptr;
    }
    memcpy(buf, cwd.c_str(), cwd.size() + 1);
    return buf;
}

int _chdir(const char *path)
{
    if (SetCwd(path))
        return 0;
    errno = ENOENT;
    return -1;
}

int _rmdir(const char *path)
{
    const std::string real = Resolved(path);
    const int r = rmdir(real.c_str());
    InvalidateDirOf(real.c_str());
    return r;
}

int _unlink(const char *path)
{
    const std::string real = Resolved(path);
    const int r = unlink(real.c_str());
    InvalidateDirOf(real.c_str());
    return r;
}

int _access(const char *path, int mode)
{
    bool exists;
    const std::string real = Resolved(path, &exists);
    if (!exists)
    {
        errno = ENOENT;
        return -1;
    }
    return access(real.c_str(), mode & 6);
}

// ----- ShlObj.h -----
// web: every shell folder is the engine's home folder (bo1_web_set_home_dir; fs_h on the command line wins anyway)
static char s_homeDir[MAX_PATH] = "/opfs/bo1/home";

void bo1_web_set_home_dir(const char *dir)
{
    snprintf(s_homeDir, sizeof(s_homeDir), "%s", dir);
}

HRESULT SHGetFolderPathA(HWND hwnd, int csidl, HANDLE token, DWORD flags, LPSTR path)
{
    if (!path)
        return E_INVALIDARG;
    snprintf(path, MAX_PATH, "%s", s_homeDir);
    if (csidl & CSIDL_FLAG_CREATE)
        mkdir(s_homeDir, 0777);
    return S_OK;
}

} // extern "C"
