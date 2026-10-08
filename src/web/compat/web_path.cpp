// web_path.cpp - case-insensitive path resolution for the web build's file system (OPFS under /opfs, or the host
// file system in the Node test build).
//
// Windows paths are case-insensitive and use '\'. The engine builds paths like "/opfs/bo1/game\zone\English\x.ff"
// while the player's install may say "zone/english/X.FF". ResolvePath turns any such path into the real one:
//   1. '\' -> '/', duplicate separators and "." components dropped, ".." applied lexically, relative paths joined to
//      the current directory;
//   2. each component is looked up in its directory's listing - exact name first, then case-insensitively. Listings
//      are read once (readdir) and cached; the emulation's own writes (fopen for writing, CreateFileA, mkdir, rename,
//      delete) invalidate the affected directory. Files the page writes while the engine runs are not seen until the
//      directory is invalidated (the page imports before it starts the engine).
//   3. the first component that does not exist ends the lookup: the rest is appended as given (creation keeps the
//      engine's spelling).
#include "bo1_win_internal.h"
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
struct Listing
{
    bool exists = false;              // the directory exists (readdir worked)
    std::vector<std::string> names;
};

std::mutex s_cacheLock;
std::unordered_map<std::string, Listing> *s_cache;

// The current directory is tracked here, not asked from the file system: WasmFS's getcwd() drops the name of a mount
// point ("/opfs/bo1/game" comes back as "//bo1/game"), which would resolve relative paths into the wrong place.
std::mutex s_cwdLock;
std::string s_cwd = "/";

const Listing &GetListing(const std::string &dir)
{
    if (!s_cache)
        s_cache = new std::unordered_map<std::string, Listing>();
    auto it = s_cache->find(dir);
    if (it != s_cache->end())
        return it->second;
    Listing listing;
    if (DIR *d = opendir(dir.empty() ? "/" : dir.c_str()))
    {
        listing.exists = true;
        while (dirent *e = readdir(d))
        {
            if (strcmp(e->d_name, ".") && strcmp(e->d_name, ".."))
                listing.names.emplace_back(e->d_name);
        }
        closedir(d);
    }
    return (*s_cache)[dir] = std::move(listing);
}

// splits a Windows or POSIX path into normalized absolute components
void Split(const char *in, std::vector<std::string> &parts)
{
    std::string path;
    if (in[0] != '/' && in[0] != '\\')
    {
        std::lock_guard<std::mutex> lock(s_cwdLock);
        path = s_cwd;
        path += '/';
    }
    path += in;
    std::string cur;
    auto flush = [&]() {
        if (cur.empty() || cur == ".")
        {
        }
        else if (cur == "..")
        {
            if (!parts.empty())
                parts.pop_back();
        }
        else
            parts.push_back(cur);
        cur.clear();
    };
    for (char c : path)
    {
        if (c == '/' || c == '\\')
            flush();
        else
            cur += c;
    }
    flush();
}
}

bool bo1w::ResolvePath(const char *in, char *out, size_t outSize)
{
    if (!in || !*in)
    {
        if (outSize)
            out[0] = 0;
        return false;
    }
    std::vector<std::string> parts;
    Split(in, parts);
    std::string resolved;
    bool exists = true;
    {
        std::lock_guard<std::mutex> lock(s_cacheLock);
        for (size_t i = 0; i < parts.size(); ++i)
        {
            const std::string &want = parts[i];
            if (exists)
            {
                const Listing &l = GetListing(resolved);
                const std::string *match = nullptr;
                if (l.exists)
                {
                    for (const std::string &n : l.names)
                    {
                        if (n == want)
                        {
                            match = &n;
                            break;
                        }
                    }
                    if (!match)
                    {
                        for (const std::string &n : l.names)
                        {
                            if (!strcasecmp(n.c_str(), want.c_str()))
                            {
                                match = &n;
                                break;
                            }
                        }
                    }
                }
                resolved += '/';
                if (match)
                    resolved += *match;
                else
                {
                    resolved += want;
                    exists = false;
                }
            }
            else
            {
                resolved += '/';
                resolved += want;
            }
        }
    }
    if (resolved.empty())
        resolved = "/";
    snprintf(out, outSize, "%s", resolved.c_str());
    return exists;
}

void bo1w::InvalidateDirOf(const char *resolvedPath)
{
    std::string p(resolvedPath ? resolvedPath : "");
    const size_t slash = p.find_last_of('/');
    const std::string dir = slash == std::string::npos || slash == 0 ? std::string() : p.substr(0, slash);
    std::lock_guard<std::mutex> lock(s_cacheLock);
    if (s_cache)
    {
        s_cache->erase(dir);
        s_cache->erase(p);   // the path itself, if it was a directory
    }
}

void bo1w::InvalidateAllDirs()
{
    std::lock_guard<std::mutex> lock(s_cacheLock);
    if (s_cache)
        s_cache->clear();
}

std::string bo1w::GetCwd()
{
    std::lock_guard<std::mutex> lock(s_cwdLock);
    return s_cwd;
}

bool bo1w::SetCwd(const char *path)
{
    char real[1024];
    if (!ResolvePath(path, real, sizeof(real)))
        return false;
    struct stat st;
    if (stat(real, &st) != 0 || !S_ISDIR(st.st_mode))
        return false;
    chdir(real);   // keep the file system's own idea in step (relative paths the resolver does not see)
    std::lock_guard<std::mutex> lock(s_cwdLock);
    s_cwd = real;
    return true;
}

bool bo1w::WildcardMatch(const char *pattern, const char *name)
{
    // Windows: "*.*" and "*" match everything (also names without a dot)
    if (!strcmp(pattern, "*.*") || !strcmp(pattern, "*"))
        return true;
    const char *p = pattern, *n = name, *star = nullptr, *mark = nullptr;
    while (*n)
    {
        if (*p == '?' || (*p && tolower((unsigned char)*p) == tolower((unsigned char)*n)))
        {
            ++p;
            ++n;
        }
        else if (*p == '*')
        {
            star = p++;
            mark = n;
        }
        else if (star)
        {
            p = star + 1;
            n = ++mark;
        }
        else
            return false;
    }
    while (*p == '*' || (*p == '.' && p[1] == '*' && !p[2]))
        p += *p == '*' ? 1 : 2;
    return !*p;
}

// the engine's view: exported for src/web (Sys_ListFiles etc.)
extern "C" int bo1_web_resolve_path(const char *in, char *out, int outSize)
{
    return bo1w::ResolvePath(in, out, (size_t)outSize) ? 1 : 0;
}

extern "C" void bo1_web_invalidate_path(const char *resolvedPath)
{
    bo1w::InvalidateDirOf(resolvedPath);
}
