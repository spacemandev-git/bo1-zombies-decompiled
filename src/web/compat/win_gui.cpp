// win_gui.cpp - user32 / gdi32 for the web build: the page's <canvas> is the only window and the only monitor.
//
// CreateWindowExA returns one fake HWND; its client area is the size the engine gives its window (CreateWindowExA /
// SetWindowPos / MoveWindow: the back buffer size in windowed mode), readable with bo1_web_canvas_size. No window
// messages exist: input arrives through Emscripten's html5 callbacks (web_input.cpp), PeekMessageA finds nothing and
// GetMessageA returns 0 (WM_QUIT) so "wait for the user" loops end.
// Display modes are a fixed list plus the canvas size; mode changes and gamma ramps are accepted and ignored.
#include "bo1_win_internal.h"
#include <atomic>

namespace
{
struct FakeWindow
{
    int unused;
    char title[256];
    LONG style;
    LONG exStyle;
    LONG_PTR userData;
    WNDPROC proc;
    bool visible;
};

FakeWindow s_window;
bool s_windowExists;
int s_desktopAnchor, s_dcAnchor, s_monitorAnchor, s_iconAnchor, s_cursorAnchor, s_brushAnchor, s_hookAnchor;
std::atomic<int> s_canvasWidth{1280};
std::atomic<int> s_canvasHeight{720};
int s_cursorCount = 0;
POINT s_cursor;

struct Mode
{
    int w, h;
};
const Mode s_modes[] = {{640, 480}, {800, 600}, {1024, 768}, {1280, 720}, {1280, 800}, {1280, 1024}, {1366, 768},
    {1600, 900}, {1680, 1050}, {1920, 1080}, {1920, 1200}, {2560, 1440}};

HWND TheWindow()
{
    return (HWND)&s_window;
}

bool IsTheWindow(HWND hwnd)
{
    return hwnd == TheWindow();
}

void CanvasRect(LPRECT r)
{
    r->left = 0;
    r->top = 0;
    r->right = s_canvasWidth.load();
    r->bottom = s_canvasHeight.load();
}

void FillMode(DEVMODEA *dm, int w, int h)
{
    const WORD size = dm->dmSize ? dm->dmSize : (WORD)sizeof(DEVMODEA);
    memset(dm, 0, size < sizeof(DEVMODEA) ? size : sizeof(DEVMODEA));
    dm->dmSize = size;
    dm->dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY;
    dm->dmBitsPerPel = 32;
    dm->dmPelsWidth = (DWORD)w;
    dm->dmPelsHeight = (DWORD)h;
    dm->dmDisplayFrequency = 60;
}
}

extern "C" {

void bo1_web_canvas_size(int *width, int *height)
{
    if (width)
        *width = s_canvasWidth.load();
    if (height)
        *height = s_canvasHeight.load();
}

void bo1_web_set_canvas_size(int width, int height)
{
    if (width > 0)
        s_canvasWidth.store(width);
    if (height > 0)
        s_canvasHeight.store(height);
}

// ----- classes and windows -----
ATOM RegisterClassA(const WNDCLASSA *wc)
{
    return 0xC001;
}

ATOM RegisterClassExA(const WNDCLASSEXA *wc)
{
    return 0xC001;
}

BOOL UnregisterClassA(LPCSTR name, HINSTANCE instance)
{
    return TRUE;
}

HWND CreateWindowExA(DWORD exStyle, LPCSTR className, LPCSTR windowName, DWORD style, int x, int y, int width,
    int height, HWND parent, HMENU menu, HINSTANCE instance, LPVOID param)
{
    s_windowExists = true;
    snprintf(s_window.title, sizeof(s_window.title), "%s", windowName ? windowName : "");
    s_window.style = (LONG)style;
    s_window.exStyle = (LONG)exStyle;
    s_window.visible = (style & WS_VISIBLE) != 0;
    // the engine sizes its window to the back buffer (windowed mode; AdjustWindowRectEx adds no border here): that is
    // the client area mouse coordinates are measured in
    if (width > 0 && height > 0 && width != (int)0x80000000)
        bo1_web_set_canvas_size(width, height);
    return TheWindow();
}

BOOL DestroyWindow(HWND hwnd)
{
    if (IsTheWindow(hwnd))
        s_windowExists = false;
    return TRUE;
}

BOOL ShowWindow(HWND hwnd, int cmd)
{
    const bool was = s_window.visible;
    if (IsTheWindow(hwnd))
        s_window.visible = cmd != SW_HIDE;
    return was;
}

BOOL UpdateWindow(HWND hwnd)
{
    return TRUE;
}

BOOL SetWindowPos(HWND hwnd, HWND after, int x, int y, int cx, int cy, UINT flags)
{
    if (IsTheWindow(hwnd) && !(flags & SWP_NOSIZE) && cx > 0 && cy > 0)
        bo1_web_set_canvas_size(cx, cy);
    return TRUE;
}

BOOL MoveWindow(HWND hwnd, int x, int y, int w, int h, BOOL repaint)
{
    if (IsTheWindow(hwnd) && w > 0 && h > 0)
        bo1_web_set_canvas_size(w, h);
    return TRUE;
}

BOOL GetWindowRect(HWND hwnd, LPRECT rect)
{
    if (!rect)
        return FALSE;
    CanvasRect(rect);
    return TRUE;
}

BOOL GetClientRect(HWND hwnd, LPRECT rect)
{
    if (!rect)
        return FALSE;
    CanvasRect(rect);
    return TRUE;
}

BOOL AdjustWindowRect(LPRECT rect, DWORD style, BOOL menu)
{
    return TRUE;   // no borders around a canvas
}

BOOL AdjustWindowRectEx(LPRECT rect, DWORD style, BOOL menu, DWORD exStyle)
{
    return TRUE;
}

LONG SetWindowLongA(HWND hwnd, int index, LONG value)
{
    LONG previous = 0;
    switch (index)
    {
    case GWL_STYLE:
        previous = s_window.style;
        s_window.style = value;
        break;
    case GWL_EXSTYLE:
        previous = s_window.exStyle;
        s_window.exStyle = value;
        break;
    case GWL_USERDATA:
        previous = (LONG)s_window.userData;
        s_window.userData = value;
        break;
    case GWL_WNDPROC:
        previous = (LONG)(intptr_t)s_window.proc;
        s_window.proc = (WNDPROC)(intptr_t)value;
        break;
    default:
        break;
    }
    return previous;
}

LONG GetWindowLongA(HWND hwnd, int index)
{
    switch (index)
    {
    case GWL_STYLE:
        return s_window.style;
    case GWL_EXSTYLE:
        return s_window.exStyle;
    case GWL_USERDATA:
        return (LONG)s_window.userData;
    case GWL_WNDPROC:
        return (LONG)(intptr_t)s_window.proc;
    default:
        return 0;
    }
}

BOOL SetWindowTextA(HWND hwnd, LPCSTR text)
{
    snprintf(s_window.title, sizeof(s_window.title), "%s", text ? text : "");
    return TRUE;
}

int GetWindowTextA(HWND hwnd, LPSTR text, int max)
{
    if (!text || max <= 0)
        return 0;
    snprintf(text, (size_t)max, "%s", s_window.title);
    return (int)strlen(text);
}

BOOL GetWindowPlacement(HWND hwnd, WINDOWPLACEMENT *wp)
{
    if (!wp)
        return FALSE;
    memset(wp, 0, sizeof(*wp));
    wp->length = sizeof(*wp);
    wp->showCmd = SW_SHOWNORMAL;
    CanvasRect(&wp->rcNormalPosition);
    return TRUE;
}

BOOL SetWindowPlacement(HWND hwnd, const WINDOWPLACEMENT *wp)
{
    return TRUE;
}

LRESULT DefWindowProcA(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    return 0;
}

LRESULT CallWindowProcA(WNDPROC proc, HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    return proc ? proc(hwnd, msg, wParam, lParam) : 0;
}

BOOL PeekMessageA(LPMSG msg, HWND hwnd, UINT filterMin, UINT filterMax, UINT remove)
{
    return FALSE;
}

BOOL GetMessageA(LPMSG msg, HWND hwnd, UINT filterMin, UINT filterMax)
{
    if (msg)
    {
        memset(msg, 0, sizeof(*msg));
        msg->message = WM_QUIT;
    }
    return FALSE;
}

BOOL TranslateMessage(const MSG *msg)
{
    return FALSE;
}

LRESULT DispatchMessageA(const MSG *msg)
{
    return 0;
}

BOOL PostMessageA(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    return TRUE;   // no message queue: posted messages are dropped
}

LRESULT SendMessageA(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    return 0;
}

void PostQuitMessage(int code)
{
}

BOOL PostThreadMessageA(DWORD threadId, UINT msg, WPARAM wParam, LPARAM lParam)
{
    return TRUE;
}

HICON LoadIconA(HINSTANCE instance, LPCSTR name)
{
    return (HICON)&s_iconAnchor;
}

HCURSOR LoadCursorA(HINSTANCE instance, LPCSTR name)
{
    return (HCURSOR)&s_cursorAnchor;
}

HANDLE LoadImageA(HINSTANCE instance, LPCSTR name, UINT type, int cx, int cy, UINT flags)
{
    return nullptr;
}

HBRUSH CreateSolidBrush(COLORREF color)
{
    return (HBRUSH)&s_brushAnchor;
}

HGDIOBJ GetStockObject(int object)
{
    return (HGDIOBJ)&s_brushAnchor;
}

BOOL DeleteObject(HGDIOBJ object)
{
    return TRUE;
}

HGDIOBJ SelectObject(HDC hdc, HGDIOBJ object)
{
    return object;
}

HFONT CreateFontA(int h, int w, int esc, int orient, int weight, DWORD italic, DWORD underline, DWORD strike, DWORD charset,
    DWORD outPrecision, DWORD clipPrecision, DWORD quality, DWORD pitch, LPCSTR face)
{
    return (HFONT)&s_brushAnchor;
}

// ----- cursor (the engine's own cursor logic lives in web_input.cpp; these keep Win32 callers consistent) -----
HCURSOR SetCursor(HCURSOR cursor)
{
    return (HCURSOR)&s_cursorAnchor;
}

int ShowCursor(BOOL show)
{
    s_cursorCount += show ? 1 : -1;
    return s_cursorCount;
}

BOOL SetCursorPos(int x, int y)
{
    s_cursor.x = x;
    s_cursor.y = y;
    return TRUE;
}

BOOL GetCursorPos(LPPOINT point)
{
    if (!point)
        return FALSE;
    *point = s_cursor;
    return TRUE;
}

BOOL ClientToScreen(HWND hwnd, LPPOINT point)
{
    return TRUE;   // the canvas is at the screen origin
}

BOOL ScreenToClient(HWND hwnd, LPPOINT point)
{
    return TRUE;
}

BOOL ClipCursor(const RECT *rect)
{
    return TRUE;
}

BOOL GetClipCursor(LPRECT rect)
{
    if (rect)
        CanvasRect(rect);
    return TRUE;
}

HWND SetCapture(HWND hwnd)
{
    return nullptr;
}

BOOL ReleaseCapture(void)
{
    return TRUE;
}

HWND GetCapture(void)
{
    return nullptr;
}

HWND GetForegroundWindow(void)
{
    return s_windowExists ? TheWindow() : nullptr;
}

BOOL SetForegroundWindow(HWND hwnd)
{
    return TRUE;
}

HWND GetActiveWindow(void)
{
    return s_windowExists ? TheWindow() : nullptr;
}

HWND SetActiveWindow(HWND hwnd)
{
    return GetActiveWindow();
}

HWND SetFocus(HWND hwnd)
{
    return GetActiveWindow();
}

HWND GetFocus(void)
{
    return GetActiveWindow();
}

HWND GetDesktopWindow(void)
{
    return (HWND)&s_desktopAnchor;
}

HWND GetParent(HWND hwnd)
{
    return nullptr;
}

HWND FindWindowA(LPCSTR cls, LPCSTR name)
{
    return nullptr;
}

BOOL EnumWindows(WNDENUMPROC proc, LPARAM data)
{
    return TRUE;
}

BOOL EnumThreadWindows(DWORD thread, WNDENUMPROC proc, LPARAM data)
{
    return TRUE;
}

DWORD GetWindowThreadProcessId(HWND hwnd, LPDWORD pid)
{
    if (pid)
        *pid = GetCurrentProcessId();
    return 0;
}

BOOL IsIconic(HWND hwnd)
{
    return FALSE;
}

BOOL IsZoomed(HWND hwnd)
{
    return FALSE;
}

BOOL IsWindow(HWND hwnd)
{
    return IsTheWindow(hwnd) && s_windowExists;
}

BOOL IsWindowVisible(HWND hwnd)
{
    return IsTheWindow(hwnd) && s_window.visible;
}

BOOL IsWindowEnabled(HWND hwnd)
{
    return TRUE;
}

BOOL EnableWindow(HWND hwnd, BOOL enable)
{
    return FALSE;
}

BOOL BringWindowToTop(HWND hwnd)
{
    return TRUE;
}

BOOL FlashWindow(HWND hwnd, BOOL invert)
{
    return FALSE;
}

BOOL InvalidateRect(HWND hwnd, const RECT *rect, BOOL erase)
{
    return TRUE;
}

BOOL ValidateRect(HWND hwnd, const RECT *rect)
{
    return TRUE;
}

HDC BeginPaint(HWND hwnd, LPPAINTSTRUCT ps)
{
    if (ps)
        memset(ps, 0, sizeof(*ps));
    return (HDC)&s_dcAnchor;
}

BOOL EndPaint(HWND hwnd, const PAINTSTRUCT *ps)
{
    return TRUE;
}

// ----- rectangles -----
BOOL EqualRect(const RECT *a, const RECT *b)
{
    return a && b && a->left == b->left && a->top == b->top && a->right == b->right && a->bottom == b->bottom;
}

BOOL SetRect(LPRECT rect, int left, int top, int right, int bottom)
{
    if (!rect)
        return FALSE;
    rect->left = left;
    rect->top = top;
    rect->right = right;
    rect->bottom = bottom;
    return TRUE;
}

BOOL SetRectEmpty(LPRECT rect)
{
    return SetRect(rect, 0, 0, 0, 0);
}

BOOL IsRectEmpty(const RECT *rect)
{
    return !rect || rect->right <= rect->left || rect->bottom <= rect->top;
}

BOOL OffsetRect(LPRECT rect, int dx, int dy)
{
    if (!rect)
        return FALSE;
    rect->left += dx;
    rect->right += dx;
    rect->top += dy;
    rect->bottom += dy;
    return TRUE;
}

BOOL IntersectRect(LPRECT dst, const RECT *a, const RECT *b)
{
    RECT r;
    r.left = a->left > b->left ? a->left : b->left;
    r.top = a->top > b->top ? a->top : b->top;
    r.right = a->right < b->right ? a->right : b->right;
    r.bottom = a->bottom < b->bottom ? a->bottom : b->bottom;
    if (r.right <= r.left || r.bottom <= r.top)
    {
        SetRectEmpty(dst);
        return FALSE;
    }
    *dst = r;
    return TRUE;
}

BOOL PtInRect(const RECT *rect, POINT pt)
{
    return rect && pt.x >= rect->left && pt.x < rect->right && pt.y >= rect->top && pt.y < rect->bottom;
}

// ----- system metrics, monitors, display modes -----
int GetSystemMetrics(int index)
{
    switch (index)
    {
    case SM_CXSCREEN:
    case SM_CXVIRTUALSCREEN:
        return s_canvasWidth.load();
    case SM_CYSCREEN:
    case SM_CYVIRTUALSCREEN:
        return s_canvasHeight.load();
    case SM_CMONITORS:
        return 1;
    case SM_REMOTESESSION:
        return 0;
    default:
        return 0;
    }
}

BOOL SystemParametersInfoA(UINT action, UINT param, PVOID data, UINT winIni)
{
    if (action == SPI_GETWORKAREA && data)
        CanvasRect((LPRECT)data);
    return TRUE;
}

HMONITOR MonitorFromWindow(HWND hwnd, DWORD flags)
{
    return (HMONITOR)&s_monitorAnchor;
}

HMONITOR MonitorFromPoint(POINT pt, DWORD flags)
{
    return (HMONITOR)&s_monitorAnchor;
}

HMONITOR MonitorFromRect(LPCRECT rect, DWORD flags)
{
    return (HMONITOR)&s_monitorAnchor;
}

BOOL GetMonitorInfoA(HMONITOR monitor, LPMONITORINFO info)
{
    if (!info)
        return FALSE;
    CanvasRect(&info->rcMonitor);
    info->rcWork = info->rcMonitor;
    info->dwFlags = MONITORINFOF_PRIMARY;
    if (info->cbSize >= sizeof(MONITORINFOEXA))
        snprintf(((MONITORINFOEXA *)info)->szDevice, 32, "\\\\.\\DISPLAY1");
    return TRUE;
}

BOOL EnumDisplayMonitors(HDC hdc, LPCRECT clip, MONITORENUMPROC proc, LPARAM data)
{
    if (!proc)
        return FALSE;
    RECT r;
    CanvasRect(&r);
    proc((HMONITOR)&s_monitorAnchor, hdc, &r, data);
    return TRUE;
}

BOOL EnumDisplaySettingsA(LPCSTR device, DWORD mode, DEVMODEA *dm)
{
    if (!dm)
        return FALSE;
    if (mode == ENUM_CURRENT_SETTINGS || mode == ENUM_REGISTRY_SETTINGS)
    {
        FillMode(dm, s_canvasWidth.load(), s_canvasHeight.load());
        return TRUE;
    }
    const DWORD count = (DWORD)(sizeof(s_modes) / sizeof(s_modes[0]));
    if (mode < count)
    {
        FillMode(dm, s_modes[mode].w, s_modes[mode].h);
        return TRUE;
    }
    if (mode == count)
    {
        FillMode(dm, s_canvasWidth.load(), s_canvasHeight.load());
        return TRUE;
    }
    return FALSE;
}

BOOL EnumDisplayDevicesA(LPCSTR device, DWORD index, PDISPLAY_DEVICEA dd, DWORD flags)
{
    if (index != 0 || !dd)
        return FALSE;
    const DWORD cb = dd->cb;
    memset(dd, 0, sizeof(*dd));
    dd->cb = cb;
    snprintf(dd->DeviceName, sizeof(dd->DeviceName), "\\\\.\\DISPLAY1");
    snprintf(dd->DeviceString, sizeof(dd->DeviceString), "WebGL2");
    dd->StateFlags = DISPLAY_DEVICE_ATTACHED_TO_DESKTOP | DISPLAY_DEVICE_PRIMARY_DEVICE;
    return TRUE;
}

LONG ChangeDisplaySettingsA(DEVMODEA *dm, DWORD flags)
{
    return DISP_CHANGE_SUCCESSFUL;
}

LONG ChangeDisplaySettingsExA(LPCSTR device, DEVMODEA *dm, HWND hwnd, DWORD flags, LPVOID param)
{
    return DISP_CHANGE_SUCCESSFUL;
}

HDC GetDC(HWND hwnd)
{
    return (HDC)&s_dcAnchor;
}

HDC GetWindowDC(HWND hwnd)
{
    return (HDC)&s_dcAnchor;
}

int ReleaseDC(HWND hwnd, HDC hdc)
{
    return 1;
}

HDC CreateDCA(LPCSTR driver, LPCSTR device, LPCSTR port, const DEVMODEA *dm)
{
    return (HDC)&s_dcAnchor;
}

BOOL DeleteDC(HDC hdc)
{
    return TRUE;
}

int GetDeviceCaps(HDC hdc, int index)
{
    switch (index)
    {
    case HORZRES:
        return s_canvasWidth.load();
    case VERTRES:
        return s_canvasHeight.load();
    case BITSPIXEL:
        return 32;
    case VREFRESH:
        return 60;
    case LOGPIXELSX:
    case LOGPIXELSY:
        return 96;
    default:
        return 0;
    }
}

BOOL SetDeviceGammaRamp(HDC hdc, LPVOID ramp)
{
    return FALSE;
}

BOOL GetDeviceGammaRamp(HDC hdc, LPVOID ramp)
{
    if (!ramp)
        return FALSE;
    WORD *r = (WORD *)ramp;
    for (int c = 0; c < 3; ++c)
        for (int i = 0; i < 256; ++i)
            r[c * 256 + i] = (WORD)(i * 257);
    return TRUE;
}

COLORREF SetTextColor(HDC hdc, COLORREF color)
{
    return 0;
}

COLORREF SetBkColor(HDC hdc, COLORREF color)
{
    return 0;
}

int SetBkMode(HDC hdc, int mode)
{
    return OPAQUE;
}

// ----- clipboard: the engine has none on the web -----
BOOL OpenClipboard(HWND owner)
{
    return FALSE;
}

BOOL CloseClipboard(void)
{
    return TRUE;
}

BOOL EmptyClipboard(void)
{
    return FALSE;
}

HANDLE GetClipboardData(UINT format)
{
    return nullptr;
}

HANDLE SetClipboardData(UINT format, HANDLE mem)
{
    return nullptr;
}

BOOL IsClipboardFormatAvailable(UINT format)
{
    return FALSE;
}

// ----- keyboard state (the engine tracks its own from key events) -----
SHORT GetAsyncKeyState(int vkey)
{
    return 0;
}

SHORT GetKeyState(int vkey)
{
    return 0;
}

BOOL GetKeyboardState(PBYTE state)
{
    if (state)
        memset(state, 0, 256);
    return TRUE;
}

// MAPVK_VK_TO_CHAR for a US layout: the unshifted character (letters upper case, as Windows returns them)
UINT MapVirtualKeyA(UINT code, UINT type)
{
    if (type != MAPVK_VK_TO_CHAR)
        return 0;
    if ((code >= '0' && code <= '9') || (code >= 'A' && code <= 'Z'))
        return code;
    switch (code)
    {
    case VK_OEM_1: return ';';
    case VK_OEM_PLUS: return '=';
    case VK_OEM_COMMA: return ',';
    case VK_OEM_MINUS: return '-';
    case VK_OEM_PERIOD: return '.';
    case VK_OEM_2: return '/';
    case VK_OEM_3: return '`';
    case VK_OEM_4: return '[';
    case VK_OEM_5: return '\\';
    case VK_OEM_6: return ']';
    case VK_OEM_7: return '\'';
    case VK_SPACE: return ' ';
    default: return 0;
    }
}

int ToAscii(UINT vkey, UINT scan, const BYTE *state, LPWORD out, UINT flags)
{
    return 0;
}

HKL GetKeyboardLayout(DWORD thread)
{
    return (HKL)(uintptr_t)0x04090409;
}

HHOOK SetWindowsHookExA(int id, HOOKPROC proc, HINSTANCE mod, DWORD thread)
{
    return (HHOOK)&s_hookAnchor;
}

BOOL UnhookWindowsHookEx(HHOOK hook)
{
    return TRUE;
}

LRESULT CallNextHookEx(HHOOK hook, int code, WPARAM wParam, LPARAM lParam)
{
    return 0;
}

UINT_PTR SetTimer(HWND hwnd, UINT_PTR id, UINT elapse, void *proc)
{
    return id ? id : 1;
}

BOOL KillTimer(HWND hwnd, UINT_PTR id)
{
    return TRUE;
}

BOOL MessageBeep(UINT type)
{
    return TRUE;
}

DWORD GetQueueStatus(UINT flags)
{
    return 0;
}

HWND CreateDialogParamA(HINSTANCE inst, LPCSTR tmpl, HWND parent, DLGPROC proc, LPARAM init)
{
    return nullptr;
}

INT_PTR DialogBoxParamA(HINSTANCE inst, LPCSTR tmpl, HWND parent, DLGPROC proc, LPARAM init)
{
    return -1;
}

BOOL EndDialog(HWND dlg, INT_PTR result)
{
    return TRUE;
}

HWND GetDlgItem(HWND dlg, int id)
{
    return nullptr;
}

BOOL SetDlgItemTextA(HWND dlg, int id, LPCSTR text)
{
    return FALSE;
}

UINT GetDlgItemTextA(HWND dlg, int id, LPSTR text, int max)
{
    if (text && max > 0)
        text[0] = 0;
    return 0;
}

} // extern "C"
