// web_input.cpp - keyboard and mouse for the web build: win32/win_input.cpp and the input half of win_wndproc.cpp.
//
// Emscripten's html5 callbacks run on the page's main thread (the engine runs in workers). They only record: key and
// character events, mouse buttons and wheel steps go into a lock-free queue; mouse motion accumulates in atomics. The
// engine thread drains the queue in Win_GetEvent (bo1_web_input_pump, web_main.cpp) with the same mapping as
// MainWndProc / MapKey (virtualKeyConvert below is win_wndproc.cpp's table; DOM keyCode values are Windows virtual-key
// codes) and reads the mouse in IN_Frame, as on Windows.
//
// Pointer lock: CL_MouseEvent says when the game (not a menu) owns the mouse; the next click on the canvas then asks
// for pointer lock (browsers only allow it inside a user gesture), and leaving that state releases it.
#include <win32/win_input.h>
#include <win32/win_wndproc.h>
#include <win32/win_main.h>
#include <win32/win_local.h>
#include <win32/win_shared.h>
#include <client/gpad_core.h>
#include <client/cl_main.h>
#include <client_mp/cl_input_mp.h>
#include <cgame_mp/cg_newDraw_mp.h>
#include <gfx_d3d/rb_backend.h>
#include <gfx_d3d/r_dvars.h>
#include <qcommon/common.h>
#include <universal/dvar.h>

#include "web_bridge.h"

#include <emscripten.h>
#include <emscripten/html5.h>
#include <atomic>

// win_wndproc.cpp / win_input.cpp globals
WinVars_t g_wv;
WinMouseVars_t s_wmv;
const dvar_t *in_mouse;
int in_appactive;
int window_center_x;
int window_center_y;

namespace
{
// win_wndproc.cpp: virtual key -> engine key, [non-extended, extended]
const unsigned __int8 virtualKeyConvert[146][2] = {
    {0, 0}, {200, 200}, {201, 201}, {0, 0}, {202, 202}, {203, 203}, {204, 204}, {0, 0}, {127, 127}, {9, 9},
    {0, 0}, {0, 0}, {186, 0}, {13, 191}, {0, 0}, {0, 0}, {160, 160}, {159, 159}, {158, 158}, {153, 153},
    {151, 151}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {27, 27}, {0, 0}, {0, 0},
    {0, 0}, {0, 0}, {32, 32}, {184, 164}, {190, 163}, {188, 166}, {182, 165}, {185, 156}, {183, 154}, {187, 157},
    {189, 155}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {192, 161}, {193, 162}, {0, 0}, {48, 48}, {49, 49},
    {50, 50}, {51, 51}, {52, 52}, {53, 53}, {54, 54}, {55, 55}, {56, 56}, {57, 57}, {0, 0}, {0, 0},
    {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {97, 65}, {98, 66}, {99, 67}, {100, 68}, {101, 69},
    {102, 70}, {103, 71}, {104, 72}, {105, 73}, {106, 74}, {107, 75}, {108, 76}, {109, 77}, {110, 78}, {111, 79},
    {112, 80}, {113, 81}, {114, 82}, {115, 83}, {116, 84}, {117, 85}, {118, 86}, {119, 87}, {120, 88}, {121, 89},
    {122, 90}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {192, 192}, {188, 188}, {189, 189}, {190, 190},
    {185, 185}, {186, 186}, {187, 187}, {182, 182}, {183, 183}, {184, 184}, {198, 198}, {196, 196}, {0, 0}, {195, 195},
    {193, 193}, {194, 194}, {167, 167}, {168, 168}, {169, 169}, {170, 170}, {171, 171}, {172, 172}, {173, 173}, {174, 174},
    {175, 175}, {176, 176}, {177, 177}, {178, 178}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
    {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
    {0, 0}, {0, 0}, {0, 0}, {0, 0}, {197, 197}, {0, 0}};

enum InputType : uint8_t
{
    INPUT_KEY,
    INPUT_CHAR,
    INPUT_BUTTONS,
    INPUT_WHEEL,
    INPUT_FOCUS,
};

struct InputEvent
{
    uint8_t type;
    uint8_t down;       // key: pressed; focus: active
    uint8_t extended;   // key: the Windows "extended key" bit
    uint8_t console;    // key: the scancode-0x29 key (`~)
    int32_t value;      // key: virtual key; char: character; buttons: mask; wheel: +1 up / -1 down
    uint32_t time;
};

constexpr uint32_t QUEUE_SIZE = 1024;
InputEvent s_queue[QUEUE_SIZE];
std::atomic<uint32_t> s_queueWrite{0};
std::atomic<uint32_t> s_queueRead{0};
std::atomic<int> s_pumping{0};

std::atomic<int> s_mouseDx{0};
std::atomic<int> s_mouseDy{0};
std::atomic<int> s_mouseX{32768};   // position in the canvas, 0..65536 of its CSS box
std::atomic<int> s_mouseY{32768};
std::atomic<int> s_wantPointerLock{0};
std::atomic<int> s_pointerLocked{0};
std::atomic<int> s_focused{1};
bool s_keyDown[256];   // page thread: keys down, released on blur

uint32_t NowMs()
{
    return (uint32_t)emscripten_get_now();
}

// page thread only (single producer)
void Push(const InputEvent &e)
{
    const uint32_t w = s_queueWrite.load(std::memory_order_relaxed);
    if (w - s_queueRead.load(std::memory_order_acquire) >= QUEUE_SIZE)
        return;
    s_queue[w % QUEUE_SIZE] = e;
    s_queueWrite.store(w + 1, std::memory_order_release);
}

bool IsNumLockAffectedVK(unsigned int wParam)
{
    return (wParam >= 0x60 && wParam <= 0x69) || wParam == 110;
}

// AdustKeyForNumericKeypad (win_wndproc.cpp)
unsigned int AdjustKeyForNumericKeypad(unsigned int key, unsigned int vk, unsigned int extended)
{
    if ((CL_GetLocalClientUIGlobals(0)->keyCatchers & 0x11) == 0)
        return key;
    if (extended)
        return key;
    return !IsNumLockAffectedVK(vk) ? key : 0;
}

// MapKey (win_wndproc.cpp) for a DOM key event
unsigned int MapWebKey(const InputEvent &e)
{
    if (e.console)
        return 126;
    const unsigned int vk = (unsigned int)e.value;
    unsigned int result = 0;
    if (vk && vk <= 0x91)
        result = AdjustKeyForNumericKeypad(virtualKeyConvert[vk][e.extended], vk, e.extended);
    if (!result)
    {
        result = (unsigned __int8)MapVirtualKeyA(vk, MAPVK_VK_TO_CHAR);
        if (result >= 'A' && result <= 'Z')
            result += 'a' - 'A';
    }
    return result < 256 ? result : 0;
}

#ifndef BO1_WEB_NODE
EM_JS(int, bo1_js_page_has_text_focus, (), {
    const a = document.activeElement;
    return a && (a.tagName === 'INPUT' || a.tagName === 'TEXTAREA' || a.isContentEditable) ? 1 : 0;
});

uint32_t DecodeKeyChar(const EmscriptenKeyboardEvent *e)
{
    const unsigned char *k = (const unsigned char *)e->key;
    if (!strcmp(e->key, "Backspace"))
        return 8;
    if (!strcmp(e->key, "Tab"))
        return 9;
    if (!strcmp(e->key, "Enter"))
        return 13;
    if (!strcmp(e->key, "Escape"))
        return 27;
    uint32_t c = k[0];
    size_t len = 1;
    if (c >= 0xF0)
        len = 4, c &= 0x07;
    else if (c >= 0xE0)
        len = 3, c &= 0x0F;
    else if (c >= 0xC0)
        len = 2, c &= 0x1F;
    for (size_t i = 1; i < len; ++i)
    {
        if ((k[i] & 0xC0) != 0x80)
            return 0;
        c = (c << 6) | (k[i] & 0x3F);
    }
    if (!c || k[len] != 0)
        return 0;   // a named key ("ArrowUp"), not a character
    if (e->ctrlKey && !e->altKey)
    {
        // Windows WM_CHAR: Ctrl+letter is the control character
        if (c >= 'a' && c <= 'z')
            return c - 'a' + 1;
        if (c >= 'A' && c <= 'Z')
            return c - 'A' + 1;
        return 0;
    }
    if (e->metaKey)
        return 0;
    return c < 256 ? c : 0;   // the engine's text is single-byte
}

EM_BOOL OnKey(int eventType, const EmscriptenKeyboardEvent *e, void *)
{
    if (bo1_js_page_has_text_focus())
        return EM_FALSE;
    const bool down = eventType == EMSCRIPTEN_EVENT_KEYDOWN;
    unsigned int vk = e->keyCode;
    // F5 (reload), F11 (full screen), F12 (developer tools) stay the browser's
    if (vk == VK_F5 || vk == VK_F11 || vk == VK_F12)
        return EM_FALSE;
    InputEvent ev{};
    ev.type = INPUT_KEY;
    ev.down = down;
    ev.time = NowMs();
    ev.console = !strcmp(e->code, "Backquote");
    const bool numpad = e->location == DOM_KEY_LOCATION_NUMPAD;
    if ((vk >= VK_PRIOR && vk <= VK_DOWN) || vk == VK_INSERT || vk == VK_DELETE)
        ev.extended = !numpad;   // the dedicated keys are "extended", the keypad ones (num lock off) are not
    else if (vk == VK_RETURN)
        ev.extended = numpad;
    if (vk == VK_RETURN && numpad)
        ev.extended = 1;
    ev.value = (int32_t)vk;
    if (vk < 256)
    {
        if (down && e->repeat && s_keyDown[vk])
        {
            // Windows repeats WM_KEYDOWN too; the engine's key code handles repeats
        }
        s_keyDown[vk] = down;
    }
    Push(ev);
    if (down)
    {
        const uint32_t ch = DecodeKeyChar(e);
        if (ch)
        {
            InputEvent c{};
            c.type = INPUT_CHAR;
            c.value = (int32_t)ch;
            c.time = ev.time;
            Push(c);
        }
    }
    return EM_TRUE;   // the game owns the keyboard while the canvas page is active
}

void UpdateMousePosition(const EmscriptenMouseEvent *e)
{
    double w = 0, h = 0;
    emscripten_get_element_css_size("#canvas", &w, &h);
    if (w > 0 && h > 0 && !s_pointerLocked.load())
    {
        double x = e->targetX / w, y = e->targetY / h;
        x = x < 0 ? 0 : x > 1 ? 1 : x;
        y = y < 0 ? 0 : y > 1 ? 1 : y;
        s_mouseX.store((int)(x * 65536.0));
        s_mouseY.store((int)(y * 65536.0));
    }
}

EM_BOOL OnMouseMove(int, const EmscriptenMouseEvent *e, void *)
{
    s_mouseDx.fetch_add((int)e->movementX);
    s_mouseDy.fetch_add((int)e->movementY);
    UpdateMousePosition(e);
    return EM_FALSE;
}

EM_BOOL OnMouseButton(int eventType, const EmscriptenMouseEvent *e, void *)
{
    UpdateMousePosition(e);
    if (eventType == EMSCRIPTEN_EVENT_MOUSEDOWN && s_wantPointerLock.load() && !s_pointerLocked.load())
        emscripten_request_pointerlock("#canvas", EM_FALSE);   // inside the click: allowed
    InputEvent ev{};
    ev.type = INPUT_BUTTONS;
    ev.value = e->buttons;   // DOM bits match the engine's: 1 left, 2 right, 4 middle, 8 back (X1), 16 forward (X2)
    ev.time = NowMs();
    Push(ev);
    return EM_TRUE;
}

EM_BOOL OnWheel(int, const EmscriptenWheelEvent *e, void *)
{
    if (e->deltaY == 0)
        return EM_TRUE;
    InputEvent ev{};
    ev.type = INPUT_WHEEL;
    ev.value = e->deltaY < 0 ? 1 : -1;
    ev.time = NowMs();
    Push(ev);
    return EM_TRUE;
}

EM_BOOL OnPointerLockChange(int, const EmscriptenPointerlockChangeEvent *e, void *)
{
    s_pointerLocked.store(e->isActive ? 1 : 0);
    return EM_FALSE;
}

EM_BOOL OnFocus(int eventType, const EmscriptenFocusEvent *, void *)
{
    const bool active = eventType == EMSCRIPTEN_EVENT_FOCUS;
    s_focused.store(active ? 1 : 0);
    if (!active)
    {
        // nothing stays held while the page is in the background (Windows sends the key-ups to the old focus)
        for (int vk = 0; vk < 256; ++vk)
        {
            if (!s_keyDown[vk])
                continue;
            s_keyDown[vk] = false;
            InputEvent up{};
            up.type = INPUT_KEY;
            up.value = vk;
            up.time = NowMs();
            Push(up);
        }
        InputEvent b{};
        b.type = INPUT_BUTTONS;
        b.time = NowMs();
        Push(b);
    }
    InputEvent f{};
    f.type = INPUT_FOCUS;
    f.down = active;
    f.time = NowMs();
    Push(f);
    return EM_FALSE;
}
#endif
}

// ===================================================================================================================
// registration and the engine-side pump
// ===================================================================================================================
extern "C" void bo1_web_input_init(void)
{
    g_wv.hWnd = GetActiveWindow();
    g_wv.activeApp = 1;
    in_appactive = 1;
#ifndef BO1_WEB_NODE
    const pthread_t page = EM_CALLBACK_THREAD_CONTEXT_MAIN_RUNTIME_THREAD;
    emscripten_set_keydown_callback_on_thread(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, nullptr, EM_TRUE, OnKey, page);
    emscripten_set_keyup_callback_on_thread(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, nullptr, EM_TRUE, OnKey, page);
    emscripten_set_mousemove_callback_on_thread("#canvas", nullptr, EM_TRUE, OnMouseMove, page);
    emscripten_set_mousedown_callback_on_thread("#canvas", nullptr, EM_TRUE, OnMouseButton, page);
    emscripten_set_mouseup_callback_on_thread(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, nullptr, EM_TRUE, OnMouseButton, page);
    emscripten_set_wheel_callback_on_thread("#canvas", nullptr, EM_TRUE, OnWheel, page);
    emscripten_set_pointerlockchange_callback_on_thread(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, nullptr, EM_TRUE, OnPointerLockChange, page);
    emscripten_set_focus_callback_on_thread(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_TRUE, OnFocus, page);
    emscripten_set_blur_callback_on_thread(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_TRUE, OnFocus, page);
#endif
}

extern "C" void bo1_web_input_pump(void)
{
    if (s_pumping.exchange(1))
        return;   // one consumer at a time (Win_GetEvent can run on other threads while loading)
    uint32_t r = s_queueRead.load(std::memory_order_relaxed);
    const uint32_t w = s_queueWrite.load(std::memory_order_acquire);
    for (; r != w; ++r)
    {
        const InputEvent e = s_queue[r % QUEUE_SIZE];
        g_msgTime = (int)Sys_Milliseconds();
        switch (e.type)
        {
        case INPUT_KEY:
        {
            const unsigned int code = MapWebKey(e);
            if (code)
                Sys_QueEvent(g_msgTime, SE_KEY, (int)code, e.down, 0, 0);
            break;
        }
        case INPUT_CHAR:
            Sys_QueEvent(g_msgTime, SE_CHAR, e.value, 0, 0, 0);
            break;
        case INPUT_BUTTONS:
            IN_MouseEvent(e.value);
            break;
        case INPUT_WHEEL:
            Sys_QueEvent(g_msgTime, SE_KEY, e.value > 0 ? 206 : 205, 1, 0, 0);
            Sys_QueEvent(g_msgTime, SE_KEY, e.value > 0 ? 206 : 205, 0, 0, 0);
            break;
        case INPUT_FOCUS:
            g_wv.activeApp = e.down;
            IN_Activate(e.down);
            break;
        default:
            break;
        }
    }
    s_queueRead.store(r, std::memory_order_release);
    s_pumping.store(0);
}

// ===================================================================================================================
// win_input.cpp
// ===================================================================================================================
void __cdecl IN_StartupGamepads()
{
    GPad_InitAll();
}

void __cdecl IN_RecenterMouse()
{
    int w, h;
    bo1_web_canvas_size(&w, &h);
    window_center_x = w / 2;
    window_center_y = h / 2;
}

bool __cdecl IN_IsForegroundWindow()
{
    return s_focused.load() != 0;
}

void __cdecl IN_ActivateMouse(int force)
{
    if (s_wmv.mouseInitialized)
    {
        if (in_mouse->current.enabled)
        {
            if (force || !s_wmv.mouseActive)
                s_wmv.mouseActive = IN_IsForegroundWindow();
        }
        else
        {
            s_wmv.mouseActive = 0;
        }
    }
}

void IN_DeactivateWin32Mouse()
{
    IN_ShowSystemCursor(1);
}

void __cdecl IN_DeactivateMouse()
{
    if (s_wmv.mouseInitialized && s_wmv.mouseActive)
    {
        s_wmv.mouseActive = 0;
        IN_DeactivateWin32Mouse();
    }
}

void __cdecl IN_StartupMouse()
{
    s_wmv.mouseInitialized = 0;
    if (in_mouse->current.enabled)
        s_wmv.mouseInitialized = 1;
    else
        Com_Printf(16, "Mouse control not active.\n");
}

void __cdecl IN_MouseEvent(int mstate)
{
    if (!s_wmv.mouseInitialized)
        return;
    const int diff = s_wmv.oldButtonState ^ mstate;
    if (s_wmv.oldButtonState == mstate)
        return;
    for (int button = 0; button < 5; ++button)
    {
        if ((diff & (1 << button)) != 0)
            Sys_QueEvent(g_msgTime, SE_KEY, button + 200, (mstate & (1 << button)) != 0, 0, 0);
    }
    s_wmv.oldButtonState = mstate;
}

void __cdecl IN_SetCursorPos(unsigned int x, unsigned int y)
{
    // web: the page's cursor cannot be moved; the engine's idea of it can
    int w, h;
    bo1_web_canvas_size(&w, &h);
    if (w > 0 && h > 0)
    {
        s_mouseX.store((int)((double)x / w * 65536.0));
        s_mouseY.store((int)((double)y / h * 65536.0));
    }
    s_wmv.oldPos.x = (LONG)x;
    s_wmv.oldPos.y = (LONG)y;
}

void __cdecl IN_ShowSystemCursor(bool show)
{
    g_showCursor = show;
#ifndef BO1_WEB_NODE
    MAIN_THREAD_ASYNC_EM_ASM({
        const c = Module['canvas'];
        if (c && c.style) c.style.cursor = $0 ? 'default' : 'none';
    }, show ? 1 : 0);
#endif
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
    if (active)
        IN_ActivateMouse(1);
    else
        IN_DeactivateMouse();
}

void IN_MouseMove()
{
    if (!IN_IsForegroundWindow())
        return;
    int w, h;
    bo1_web_canvas_size(&w, &h);
    const int dx = s_mouseDx.exchange(0);
    const int dy = s_mouseDy.exchange(0);
    const int x = (int)((int64_t)s_mouseX.load() * w / 65536);
    const int y = (int)((int64_t)s_mouseY.load() * h / 65536);
    s_wmv.oldPos.x = x;
    s_wmv.oldPos.y = y;
    g_wv.recenterMouse = CL_MouseEvent(x, y, dx, dy);
    // the game owns the mouse: pointer lock on the next click; a menu: release it
    const int want = g_wv.recenterMouse ? 1 : 0;
    if (s_wantPointerLock.exchange(want) != want && !want && s_pointerLocked.load())
    {
#ifndef BO1_WEB_NODE
        MAIN_THREAD_ASYNC_EM_ASM({ if (document.exitPointerLock) document.exitPointerLock(); });
#endif
    }
}

void __cdecl IN_ClampMouseMove(tagPOINT *curPos)
{
}

void __cdecl IN_Frame()
{
    if (Dvar_GetBool("ClickToContinue"))
        IN_MouseEvent(s_wmv.oldButtonState | 1);   // win_input.cpp posts WM_LBUTTONDOWN
    if (s_wmv.mouseInitialized)
    {
        if (in_appactive)
        {
            IN_ActivateMouse(0);
            IN_MouseMove();
        }
        else
        {
            IN_DeactivateMouse();
        }
    }
    if ((in_appactive && IN_IsForegroundWindow()) || (gpad_background && gpad_background->current.enabled))
        IN_GamepadsMove();
    else
        IN_GamepadsIdle();
}

// win_wndproc.cpp
void __cdecl Sys_UpdateHotkeyBlock()
{
}

void __cdecl Sys_SetBlockSystemHotkeys(int block)
{
}

bool __cdecl Sys_AllowVidRestart()
{
    return true;
}

void __cdecl VID_AppActivate(unsigned int activeState, int minimize)
{
    g_wv.activeApp = activeState != 0;
    g_wv.isMinimized = minimize;
    IN_Activate(g_wv.activeApp);
}
