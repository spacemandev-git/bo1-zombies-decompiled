// web_main.cpp - the web build's entry point and the parts of win32/win_main.cpp (and win_syscon.cpp,
// win_mini_dumper.cpp) the rest of the engine calls: the event queue, Sys_Error / Sys_Quit / Sys_Print, Sys_Init and
// the system information dvars.
//
// main() runs in a worker (-sPROXY_TO_PTHREAD) and follows WinMain's order: critical sections, main thread, physical
// memory, localization, parse/dvar/timing init, command line, TL callbacks, Com_Init, then Com_Frame forever. The
// headless test modes of win_main.cpp (bo1_headless...) do not exist on the web: Sys_IsHeadless() is false.
#include <win32/win_main.h>
#include <win32/win_common.h>
#include <win32/win_localize.h>
#include <win32/win_configure.h>
#include <win32/win_input.h>
#include <win32/win_wndproc.h>
#include <win32/win_syscon.h>
#include <win32/win_steam.h>
#include <win32/win_net.h>
#include <win32/win_shared.h>
#include <win32/win_mini_dumper.h>
#include <qcommon/common.h>
#include <qcommon/threads.h>
#include <qcommon/mem_track.h>
#include <qcommon/tl_support.h>
#include <qcommon/cmd.h>
#include <universal/physicalmemory.h>
#include <universal/q_parse.h>
#include <universal/timing.h>
#include <universal/com_memory.h>
#include <universal/com_buildinfo.h>
#include <universal/q_shared.h>
#include <universal/dvar.h>
#include <clientscript/cscr_stringlist.h>
#include <client/client.h>
#include <client/cl_keys.h>
#include <client/con_channels.h>
#include <game_mp/g_main_mp.h>
#include <live/live_steam.h>
#include <tl/tl_system.h>
#include <sound/snd_driver_xaudio2.h>

#include "web_bridge.h"

#include <emscripten.h>
#ifndef BO1_WEB_NODE
#include <emscripten/wasmfs.h>
#endif
#include <sys/stat.h>
#include <unistd.h>
#include <string>

const dvar_t *sys_configureGHz;
const dvar_t *sys_sysMB;
const dvar_t *sys_gpu;
const dvar_t *sys_configSum;
const dvar_t *sys_SSE;

bool g_allowMature = true;

int s_nosnd;
SysInfo sys_info;
char sys_exitCmdLine[1024];
sysEvent_t eventQue[256];
int eventHead;
int eventTail;
char sys_cmdline[1024];

static volatile LONG s_quitRequested;

// ===================================================================================================================
// headless test modes (win_main.cpp): not on the web
// ===================================================================================================================
bool __cdecl Sys_IsHeadless()
{
    return false;
}

bool __cdecl Sys_IsHeadlessClient()
{
    return false;
}

void __cdecl Sys_HeadlessNoteHud(int pmType, bool hudDrawn)
{
}

void __cdecl Sys_HeadlessTimeline(const char *what)
{
}

void __cdecl Sys_HeadlessLevelNotify(const char *name)
{
}

void __cdecl Sys_HeadlessLogAssert(const char *filename, int line, const char *message)
{
    // the assert's text (Assert_MyHandler), before its __debugbreak aborts the runtime
    fprintf(stderr, "ASSERT: %s(%d): %s\n", filename, line, message);
}

unsigned int __cdecl Sys_HitchWatchBegin()
{
    return 0;
}

void __cdecl Sys_HitchWatchEnd()
{
}

unsigned int __cdecl Sys_FramePerfHitchMs()
{
    return 0;
}

void __cdecl Sys_SetQuitRequested()
{
    InterlockedExchange(&s_quitRequested, 1);
}

bool __cdecl Sys_IsQuitRequested()
{
    return s_quitRequested != 0 || Sys_QueryWin32QuitEvent();
}

void __cdecl Sys_QuitWithLostDevice(int lostMs)
{
    bo1_web_exit(0);   // web: a WebGL context loss with a quit pending
}

// ===================================================================================================================
// system information (win_main.cpp)
// ===================================================================================================================
bool __cdecl PC_StartWithNoSounds()
{
    if (G_ExitAfterToolComplete())
        s_nosnd = 1;
    if (!SD_Xaudio2CanInit())
        s_nosnd = 1;
    return s_nosnd != 0;
}

void __cdecl Sys_GetInfo(SysInfo *info)
{
    memcpy(info, &sys_info, sizeof(SysInfo));
}

void Sys_RegisterInfoDvars()
{
    sys_configureGHz = _Dvar_RegisterFloat("sys_configureGHz", 0.0, -3.4028235e38, 3.4028235e38, 0x11u,
        "Normalized total CPU power, based on cpu type, count, and speed; used in autoconfigure");
    sys_sysMB = _Dvar_RegisterInt("sys_sysMB", 0, 0x80000000, 0x7FFFFFFF, 0x11u, "Physical memory in the system");
    sys_gpu = _Dvar_RegisterString("sys_gpu", (char *)"", 0x11u, "GPU description");
    sys_configSum = _Dvar_RegisterInt("sys_configSum", 0, 0x80000000, 0x7FFFFFFF, 0x11u, "Configuration checksum");
    sys_SSE = _Dvar_RegisterBool("sys_SSE", sys_info.SSE, 0x40u, "Operating system allows Streaming SIMD Extensions");
    const float value = (float)sys_info.cpuGHz;
    _Dvar_RegisterFloat("sys_cpuGHz", value, -3.4028235e38, 3.4028235e38, 0x40u, "Measured CPU speed");
    _Dvar_RegisterString("sys_cpuName", sys_info.cpuName, 0x40u, "CPU name description");
}

// web: the browser never asks "your hardware changed, apply recommended settings?" (MessageBoxA answers no)
bool __cdecl Sys_ShouldUpdateForConfigChange()
{
    return false;
}

bool __cdecl Sys_HasConfigureChecksumChanged(int checksum)
{
    Sys_RegisterInfoDvars();
    if (G_OnlyConnectingPaths())
        return false;
    bool changed = false;
    if (sys_configSum->current.integer && sys_configSum->current.integer != checksum)
        changed = Sys_ShouldUpdateForConfigChange();
    if (!sys_configSum->current.integer || sys_configSum->current.integer != checksum)
        Dvar_SetInt((dvar_s *)sys_configSum, checksum);
    return changed;
}

bool __cdecl Sys_ShouldUpdateForInfoChange()
{
    Sys_ArchiveInfo(0);
    return false;
}

bool __cdecl Sys_HasInfoChanged()
{
    Sys_RegisterInfoDvars();
    return (sys_configureGHz->current.value > sys_info.configureGHz * 1.100000023841858
               || sys_info.configureGHz * 0.8999999761581421 > sys_configureGHz->current.value
               || sys_sysMB->current.integer > sys_info.sysMB + 32 || sys_sysMB->current.integer < sys_info.sysMB - 32
               || strcmp(sys_gpu->current.string, sys_info.gpuDescription))
        && Sys_ShouldUpdateForInfoChange();
}

void __cdecl Sys_ArchiveInfo(int checksum)
{
    Sys_RegisterInfoDvars();
    Dvar_SetFloat((dvar_s *)sys_configureGHz, (float)sys_info.configureGHz);
    Dvar_SetInt((dvar_s *)sys_sysMB, sys_info.sysMB);
    Dvar_SetString((dvar_s *)sys_gpu, sys_info.gpuDescription);
    Dvar_SetInt((dvar_s *)sys_configSum, checksum);
}

void Sys_FindInfo()
{
    sys_info.logicalCpuCount = Sys_GetCpuCount();
    sys_info.cpuGHz = 1.0 / (((double)1LL - (double)0LL) * msecPerRawTimerTick * 1000000.0);
    sys_info.sysMB = Sys_SystemMemoryMB();
    Sys_DetectVideoCard(512, sys_info.gpuDescription);
    sys_info.SSE = Sys_SupportsSSE();
    Sys_DetectCpuVendorAndName(sys_info.cpuVendor, sys_info.cpuName);
    Sys_SetAutoConfigureGHz(&sys_info);
}

// ===================================================================================================================
// fatal errors and quitting
// ===================================================================================================================
void __cdecl Sys_DirectXFatalError()
{
    Com_PrintError(16, "Sys_DirectXFatalError: the WebGL2 renderer could not start\n");
    bo1_web_report_error("The renderer could not start (WebGL2). See the console for details.");
    bo1_web_exit(1);
}

void __cdecl Sys_OutOfMemErrorInternal(const char *filename, int line)
{
    char msg[512];
    snprintf(msg, sizeof(msg), "Out of memory: filename '%s', line %d", filename, line);
    fprintf(stderr, "%s\n", msg);
    bo1_web_report_error(msg);
    bo1_web_exit(1);
}

void __cdecl Sys_QuitAndStartProcess(const char *exeName)
{
    I_strncpyz(sys_exitCmdLine, exeName, 1024);   // web: there is no other process to start; just quit
    Cbuf_AddText(0, "quit\n");
}

void __cdecl Sys_OpenURL(const char *url, int doexit)
{
    if (url)
        MAIN_THREAD_ASYNC_EM_ASM({ try { window.open(UTF8ToString($0), '_blank'); } catch (e) {} }, strdup(url));
    if (doexit)
        Cbuf_AddText(0, "quit\n");
}

void Sys_Error(char *error, ...)
{
    char string[4100];
    va_list va;
    va_start(va, error);
    Sys_EnterCriticalSection(CRITSECT_COM_ERROR);
    com_errorEntered = 1;
    Sys_SuspendOtherThreads();
    vsnprintf(string, sizeof(string), error, va);
    va_end(va);
    fprintf(stderr, "Sys_Error: %s\n", string);
    bo1_web_report_error(string);
    bo1_web_exit(1);
}

void Sys_SpawnQuitProcess()
{
}

void __cdecl Sys_NormalExit()
{
}

void __cdecl Sys_Quit()
{
    Sys_EnterCriticalSection(CRITSECT_COM_ERROR);
    LiveSteam_Shutdown();
    CL_ShutdownAll();
    IN_Shutdown();
    Key_Shutdown();
    Win_ShutdownLocalization();
    Dvar_Shutdown();
    Cmd_Shutdown();
    Sys_ShutdownEvents();
    SL_Shutdown(SCRIPTINSTANCE_SERVER);
    SL_Shutdown(SCRIPTINSTANCE_CLIENT);
    if (!com_errorEntered)
        track_shutdown(0);
    Con_ShutdownChannels();
    bo1_web_exit(0);
}

// ===================================================================================================================
// console output / input (win_syscon.cpp): the browser console is the console
// ===================================================================================================================
void __cdecl Sys_Print(char *msg)
{
    if (msg)
        fputs(msg, stdout);
}

void __cdecl Sys_CreateConsole(HINSTANCE__ *hInstance)
{
}

void __cdecl Sys_DestroyConsole()
{
}

void __cdecl Sys_ShowConsole()
{
}

char *__cdecl Sys_ConsoleInput()
{
    return nullptr;
}

void __cdecl Conbuf_AppendText(char *pMsg)
{
}

void __cdecl Conbuf_AppendTextInMainThread(char *msg)
{
}

void __cdecl Sys_SetErrorText(const char *buf)
{
    fprintf(stderr, "%s\n", buf);
}

char *__cdecl Sys_GetClipboardData()
{
    return nullptr;
}

// minidumps (win_mini_dumper.cpp): none on the web
void __cdecl Sys_StartMiniDump(bool prompt)
{
}

bool __cdecl Sys_IsMiniDumpStarted()
{
    return false;
}

// ===================================================================================================================
// event queue (win_main.cpp, unchanged logic)
// ===================================================================================================================
void __cdecl Sys_QueEvent(unsigned int time, sysEventType_t type, int value, int value2, int ptrLength, void *ptr)
{
    Sys_EnterCriticalSection(CRITSECT_SYS_EVENT_QUEUE);
    sysEvent_t *ev = &eventQue[(unsigned __int8)eventHead];
    if (eventHead - eventTail >= 256)
    {
        Com_Printf(16, "Sys_QueEvent: overflow\n");
        if (ev->evPtr)
            Z_Free((char *)ev->evPtr, 11);
        ++eventTail;
    }
    ++eventHead;
    if (!time)
        time = Sys_Milliseconds();
    ev->evTime = time;
    ev->evType = type;
    ev->evValue = value;
    ev->evValue2 = value2;
    ev->evPtrLength = ptrLength;
    ev->evPtr = ptr;
    Sys_LeaveCriticalSection(CRITSECT_SYS_EVENT_QUEUE);
}

void Sys_ShutdownEvents()
{
    Sys_EnterCriticalSection(CRITSECT_SYS_EVENT_QUEUE);
    while (eventHead > eventTail)
    {
        sysEvent_t *ev = &eventQue[(unsigned __int8)eventTail++];
        if (ev->evPtr)
            Z_Free((char *)ev->evPtr, 11);
    }
    Sys_LeaveCriticalSection(CRITSECT_SYS_EVENT_QUEUE);
}

sysEvent_t *__cdecl Win_GetEvent(sysEvent_t *result)
{
    sysEvent_t ev;
    bo1_web_input_pump();   // web: keyboard / mouse events the page queued -> Sys_QueEvent
    Sys_EnterCriticalSection(CRITSECT_SYS_EVENT_QUEUE);
    if (eventHead <= eventTail)
    {
        if (Sys_QueryWin32QuitEvent())
        {
            Sys_LeaveCriticalSection(CRITSECT_SYS_EVENT_QUEUE);
            Com_Quit_f();
        }
        memset(&ev, 0, sizeof(ev));
        ev.evTime = Sys_Milliseconds();
    }
    else
    {
        ev = eventQue[(unsigned __int8)eventTail++];
    }
    Sys_LeaveCriticalSection(CRITSECT_SYS_EVENT_QUEUE);
    *result = ev;
    return result;
}

sysEvent_t *__cdecl Sys_GetEvent(sysEvent_t *result)
{
    sysEvent_t v;
    *result = *Win_GetEvent(&v);
    return result;
}

void __cdecl Sys_LoadingKeepAlive()
{
    sysEvent_t result;
    do
    {
        Win_GetEvent(&result);
    } while (result.evType);
}

// ===================================================================================================================
// Sys_Init
// ===================================================================================================================
static cmd_function_s Sys_In_Restart_f_VAR;
static cmd_function_s Sys_Net_Restart_f_VAR;

void __cdecl Sys_In_Restart_f()
{
    IN_Shutdown();
    IN_Init();
}

void __cdecl Sys_Net_Restart_f()
{
    NET_Restart();
}

void __cdecl Sys_Init()
{
    Cmd_AddCommandInternal("in_restart", Sys_In_Restart_f, &Sys_In_Restart_f_VAR);
    Cmd_AddCommandInternal("net_restart", Sys_Net_Restart_f, &Sys_Net_Restart_f_VAR);
    Com_Printf(16, "Platform: web (Emscripten / WebAssembly)\n");
    Com_Printf(16, "%i logical CPU%s reported\n", sys_info.logicalCpuCount, sys_info.logicalCpuCount == 1 ? "" : "s");
    Com_Printf(16, "System memory is %i MB (capped at 1 GB)\n", sys_info.sysMB);
    Com_Printf(16, "Video card is \"%s\"\n", sys_info.gpuDescription);
    Com_Printf(16, "\n");
    IN_Init();
}

int __cdecl Sys_CheckCrashOrRerun()
{
    return 1;
}

// ===================================================================================================================
// main
// ===================================================================================================================
static std::string ArgValue(int argc, char **argv, const char *name)
{
    // the last "+set <name> <value>" (arguments are one per +command or one per word)
    std::string all;
    for (int i = 1; i < argc; ++i)
    {
        all += ' ';
        all += argv[i];
    }
    std::string value;
    const std::string key = std::string(" ") + name + " ";
    for (size_t at = all.find(key); at != std::string::npos; at = all.find(key, at + 1))
    {
        size_t v = at + key.size();
        while (v < all.size() && all[v] == ' ')
            ++v;
        size_t e = v;
        if (e < all.size() && all[e] == '"')
        {
            e = all.find('"', v + 1);
            value = all.substr(v + 1, e == std::string::npos ? std::string::npos : e - v - 1);
        }
        else
        {
            while (e < all.size() && all[e] != ' ' && all[e] != '+')
                ++e;
            value = all.substr(v, e - v);
        }
    }
    return value;
}

static void MakeDirs(const char *path)
{
    std::string p;
    for (const char *c = path; *c; ++c)
    {
        p += *c;
        if (*c == '/' && p.size() > 1)
            mkdir(p.c_str(), 0777);
    }
    mkdir(path, 0777);
}

static void MountFileSystem()
{
#ifndef BO1_WEB_NODE
    // OPFS (docs/web-engine-interface.md section 2): synchronous access handles from the engine's threads
    backend_t opfs = wasmfs_create_opfs_backend();
    if (!opfs || wasmfs_create_directory("/opfs", 0777, opfs) != 0)
    {
        fprintf(stderr, "web: could not mount the Origin Private File System at /opfs\n");
        bo1_web_report_error("Could not open the browser's private file system (OPFS).");
    }
#endif
}

#ifndef BO1_WEB_NODE
static void Web_Frame()
{
    if (IsDedicatedServer())
        Sleep(5);
    Com_Frame();
}
#endif

int main(int argc, char **argv)
{
    MountFileSystem();

    // the command line: the page passes one token per +command; joined with spaces as WinMain's lpCmdLine.
    // Defaults first so the page's own +set commands win: r_smp_backend 0 keeps every D3D9 (WebGL) call on this
    // thread, which owns the canvas (docs/web-port.md, "Render thread").
    std::string cmdline = "+set r_smp_backend 0";
    for (int i = 1; i < argc; ++i)
    {
        cmdline += ' ';
        cmdline += argv[i];
    }
    std::string game = ArgValue(argc, argv, "fs_b");
    if (game.empty())
        game = ArgValue(argc, argv, "fs_basepath");
    if (game.empty())
        game = "/opfs/bo1/game";
    std::string home = ArgValue(argc, argv, "fs_h");
    if (home.empty())
        home = ArgValue(argc, argv, "fs_homepath");
    if (home.empty())
        home = "/opfs/bo1/home";
    MakeDirs(game.c_str());
    MakeDirs(home.c_str());
    bo1_web_set_exe_dir(game.c_str());   // Sys_DefaultInstallPath (GetModuleFileNameA's folder)
    bo1_web_set_home_dir(home.c_str());
    bo1_web_set_command_line(cmdline.c_str());
    SetCurrentDirectoryA(game.c_str());  // Windows starts the game in its install folder
    printf("BO1 web: game %s, home %s\ncommand line: %s\n", game.c_str(), home.c_str(), cmdline.c_str());

    // WinMain (win_main.cpp), the non-headless path
    Sys_InitializeCriticalSections();
    Sys_InitMainThread();
    PMem_Init();
    track_init();
    Win_InitLocalization();
    s_nosnd = I_stristr(cmdline.c_str(), "nosnd") != 0;
    Com_InitParse();
    Dvar_Init();
    InitTiming();
    Sys_FindInfo();
    I_strncpyz(sys_cmdline, cmdline.c_str(), 1024);
    bo1_web_input_init();
    Sys_Milliseconds();
    tlPrintf("Hello from the wonderful world of TL\n");
    Sys_SetupTLCallbacks(0x900000);
    Steam_Init();
    Com_Init(sys_cmdline);
    Steam_PrintInitStatus();
    if (!IsDedicatedServer())
        Cbuf_AddText(0, "readStats\n");
    char cwd[260];
    if (_getcwd(cwd, sizeof(cwd)))
        Com_Printf(16, "Working directory: %s\n", cwd);
#ifdef BO1_WEB_NODE
    for (;;)
    {
        if (IsDedicatedServer())
            Sleep(5);
        Com_Frame();
    }
#else
    // web: one Com_Frame per browser frame on this (the canvas-owning) thread. Returning to the event loop between
    // frames is what presents the OffscreenCanvas frame and lets WebGL query results and OPFS work complete
    // (src/web/d3d9/README.md); WinMain's for (;;) would never show a frame. Timing is requestAnimationFrame; the
    // engine's own com_maxfps wait still runs inside Com_Frame. simulate_infinite_loop: main() does not return.
    emscripten_set_main_loop(Web_Frame, 0, true);
#endif
    return 0;
}
