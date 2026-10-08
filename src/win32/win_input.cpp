#include "win_input.h"
#include "win_gamepad.h"
#include <client/gpad_core.h> // mod (gpad): IN_GamepadsMove / IN_GamepadsIdle, gpad_background

#include "win_wndproc.h"
#include <universal/assertive.h>
#include <gfx_d3d/r_dvars.h>
#include <qcommon/common.h>
#include "win_main.h"
#include <gfx_d3d/rb_backend.h>
#include "win_shared.h"
#include <client_mp/cl_input_mp.h>

// mod (gpad): buttonList (pad button -> key code pairs) moved to client/gpad_core.cpp with IN_GamepadsMove.



int window_center_x;
int window_center_y;

WinMouseVars_t s_wmv;

const dvar_t *in_mouse;
//const dvar_s *gpad_present;
int in_appactive;



void __cdecl IN_StartupGamepads()
{
    GPad_InitAll();
}

void __cdecl IN_RecenterMouse()
{
    tagRECT window_rect; // [esp+0h] [ebp-10h] BYREF

    GetWindowRect(g_wv.hWnd, &window_rect);
    window_center_x = (window_rect.left + window_rect.right) / 2;
    window_center_y = (window_rect.bottom + window_rect.top) / 2;
    if ( !Sys_IsHeadless() ) // zombies: headless never moves the desktop cursor
        SetCursorPos((window_rect.left + window_rect.right) / 2, (window_rect.bottom + window_rect.top) / 2);
}

bool __cdecl IN_IsForegroundWindow()
{
    return GetForegroundWindow() == g_wv.hWnd;
}

void __cdecl IN_ActivateMouse(int force)
{
    if ( s_wmv.mouseInitialized )
    {
        if ( !r_fullscreen
            && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\win32\\win_input.cpp", 332, 0, "%s", "r_fullscreen") )
        {
            __debugbreak();
        }
        if ( in_mouse->current.enabled )
        {
            if ( force || !s_wmv.mouseActive )
                s_wmv.mouseActive = IN_IsForegroundWindow();
        }
        else
        {
            s_wmv.mouseActive = 0;
        }
    }
}

void __cdecl IN_DeactivateMouse()
{
    if ( s_wmv.mouseInitialized && s_wmv.mouseActive )
    {
        s_wmv.mouseActive = 0;
        IN_DeactivateWin32Mouse();
    }
}

void IN_DeactivateWin32Mouse()
{
    IN_ShowSystemCursor(1);
}

void __cdecl IN_StartupMouse()
{
    s_wmv.mouseInitialized = 0;
    if ( in_mouse->current.enabled )
    {
        s_wmv.mouseInitialized = 1;
        //BLOPS_NULLSUB();
    }
    else
    {
        Com_Printf(16, "Mouse control not active.\n");
    }
}

void __cdecl IN_MouseEvent(int mstate)
{
    int diff; // [esp+0h] [ebp-8h]
    int button; // [esp+4h] [ebp-4h]

    if ( s_wmv.mouseInitialized )
    {
        diff = s_wmv.oldButtonState ^ mstate;
        if ( s_wmv.oldButtonState != mstate )
        {
            //BLOPS_NULLSUB();
            for ( button = 0; button < 5; ++button )
            {
                if ( (diff & (1 << button)) != 0 )
                    Sys_QueEvent(g_msgTime, SE_KEY, button + 200, (mstate & (1 << button)) != 0, 0, 0);
            }
            s_wmv.oldButtonState = mstate;
        }
    }
}

void __cdecl IN_SetCursorPos(unsigned int x, unsigned int y)
{
    tagPOINT curPos; // [esp+0h] [ebp-8h] BYREF

    //curPos = (tagPOINT)__PAIR64__(y, x);
    curPos.x = x;
    curPos.y = y;
    ClientToScreen(g_wv.hWnd, &curPos);
    if ( !Sys_IsHeadless() ) // zombies: headless never moves the desktop cursor
        SetCursorPos(curPos.x, curPos.y);
    s_wmv.oldPos = curPos;
}

void __cdecl IN_ShowSystemCursor(bool show)
{
    int actualShow; // [esp+0h] [ebp-8h]
    int desiredShow; // [esp+4h] [ebp-4h]

    g_showCursor = show;
    desiredShow = show - 1;
    for ( actualShow = ShowCursor(show); actualShow != desiredShow; actualShow = ShowCursor(actualShow < desiredShow) )
        ;
}

void __cdecl IN_Startup()
{
    IN_StartupMouse();
    IN_StartupGamepads();
    Dvar_ClearModified(in_mouse);
}

void __cdecl IN_Shutdown()
{
    IN_DeactivateMouse();
}

void __cdecl IN_Init()
{
    in_mouse = _Dvar_RegisterBool("in_mouse", 1, 0x21u, "Initialize the mouse drivers");
    IN_Startup();
}

void __cdecl IN_Activate(int active)
{
    in_appactive = active;
    if ( active )
        IN_ActivateMouse(1);
    else
        IN_DeactivateMouse();
}

void __cdecl IN_Frame()
{
    if ( Dvar_GetBool("ClickToContinue") )
        PostMessageA(g_wv.hWnd, 0x201u, 1u, 0);
    if ( Sys_IsHeadless() ) // zombies: a headless client never reads the user's mouse or gamepads, nor moves the cursor
    {
        CL_SP_HeadlessMouseTest(); // p1 c30 TEST SWITCH bo1_mousetest: scripted mouse deltas only
        return;
    }
    if ( s_wmv.mouseInitialized )
    {
        if ( in_appactive )
        {
            IN_ActivateMouse(0);
            IN_MouseMove();
            // mod (gpad): IN_GamepadsMove was called here, so with in_mouse 0 (no mouse initialized) the pad was never read
        }
        else
        {
            IN_DeactivateMouse();
        }
    }
    // mod (gpad): read the pads whenever the game window is active and in the foreground (retail: the same, but only with
    // the mouse initialized), or always with gpad_background 1. Otherwise release whatever was held, once.
    if ( in_appactive && IN_IsForegroundWindow() || gpad_background && gpad_background->current.enabled )
        IN_GamepadsMove();
    else
        IN_GamepadsIdle();
}

// mod (gpad): IN_GamepadsMove (the per-frame pad poll) moved to client/gpad_core.cpp: it is platform-neutral now (reads
// the pad through client/gpad_backend.h) and the web build shares it.

void IN_MouseMove()
{
    int v0; // [esp+0h] [ebp-10h]
    tagPOINT curPos; // [esp+4h] [ebp-Ch] BYREF
    int dy; // [esp+Ch] [ebp-4h]

    if ( !s_wmv.mouseInitialized
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\win32\\win_input.cpp",
                    475,
                    0,
                    "%s",
                    "s_wmv.mouseInitialized") )
    {
        __debugbreak();
    }
    if ( !r_fullscreen
        && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\win32\\win_input.cpp", 476, 0, "%s", "r_fullscreen") )
    {
        __debugbreak();
    }
    if ( IN_IsForegroundWindow() )
    {
        GetCursorPos(&curPos);
        if ( r_fullscreen->current.enabled )
            IN_ClampMouseMove(&curPos);
        v0 = curPos.x - s_wmv.oldPos.x;
        dy = curPos.y - s_wmv.oldPos.y;
        if (curPos.x != s_wmv.oldPos.x || dy)
        {
            //BLOPS_NULLSUB();
        }
        s_wmv.oldPos = curPos;
        ScreenToClient(g_wv.hWnd, &curPos);
        g_wv.recenterMouse = CL_MouseEvent(curPos.x, curPos.y, v0, dy);
        if ( g_wv.recenterMouse && (v0 || dy) )
        {
            IN_RecenterMouse();
            s_wmv.oldPos.x = window_center_x;
            s_wmv.oldPos.y = window_center_y;
        }
    }
}

void __cdecl IN_ClampMouseMove(tagPOINT *curPos)
{
    bool isClamped; // [esp+3h] [ebp-11h]
    tagRECT rc; // [esp+4h] [ebp-10h] BYREF

    GetWindowRect(g_wv.hWnd, &rc);
    isClamped = 0;
    if ( curPos->x >= rc.left )
    {
        if ( curPos->x >= rc.right )
        {
            curPos->x = rc.right - 1;
            isClamped = 1;
        }
    }
    else
    {
        curPos->x = rc.left;
        isClamped = 1;
    }
    if ( curPos->y >= rc.top )
    {
        if ( curPos->y >= rc.bottom )
        {
            curPos->y = rc.bottom - 1;
            isClamped = 1;
        }
    }
    else
    {
        curPos->y = rc.top;
        isClamped = 1;
    }
    if ( isClamped )
        SetCursorPos(curPos->x, curPos->y);
}

