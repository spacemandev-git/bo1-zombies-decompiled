// dxerr.h - syntax-check stand-in for the June 2010 DirectX SDK header (MinGW-w64 ships dxerr9.h instead).
#pragma once
#include <dxerr9.h>
#ifndef DXGetErrorStringA
#define DXGetErrorStringA DXGetErrorString9A
#define DXGetErrorDescriptionA DXGetErrorDescription9A
#endif
