// web_shared.cpp - win32/win_shared.cpp for the web build: the millisecond clock and Sys_SnapVector.
#include <win32/win_shared.h>
#include <windows.h>

int sys_timeBase;

unsigned int __cdecl Sys_Milliseconds()
{
    static int initialized = 0;
    if (!initialized)
    {
        sys_timeBase = timeGetTime();
        initialized = 1;
    }
    return timeGetTime() - sys_timeBase;
}

unsigned int __cdecl Sys_MillisecondsRaw()
{
    return timeGetTime();
}

// x87 "fld f; fistp i" per component: round to nearest even (the default control word); out of range gives the
// integer indefinite 0x80000000, as fistp does
static float SnapComponent(float f)
{
    if (!(f >= -2147483648.0f && f < 2147483648.0f))
        return (float)(int)0x80000000;
    return (float)(int)__builtin_rintf(f);
}

void Sys_SnapVector(float *v)
{
    v[0] = SnapComponent(v[0]);
    v[1] = SnapComponent(v[1]);
    v[2] = SnapComponent(v[2]);
}
