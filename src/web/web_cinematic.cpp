// web_cinematic.cpp - the Bink API for the web build: no video decoder yet.
//
// gfx_d3d/r_cinematic.cpp is compiled unchanged; Bink itself (binkw32.dll) does not exist in the browser. BinkOpen
// fails, so every R_Cinematic_StartPlayback ends at once (the intro and loading videos are skipped) and the engine
// carries on as when a .bik file is missing. A real player (WebCodecs or a wasm Bink 1 decoder writing the
// Y/cR/cB/A planes r_cinematic.cpp uploads) replaces these; see docs/web-port.md.
#include <gfx_d3d/r_cinematic.h>
#include <dsound.h>

extern "C" {

HBINK BinkOpen(const char *name, U32 flags)
{
    return 0;
}

char *BinkGetError(void)
{
    static char error[] = "Bink video is not available in the browser build";
    return error;
}

void BinkClose(HBINK bnk)
{
}

S32 BinkControlBackgroundIO(HBINK bink, U32 control)
{
    return 0;
}

S32 BinkDoFrame(HBINK bnk)
{
    return 0;
}

void BinkGetFrameBuffersInfo(HBINK bink, BINKFRAMEBUFFERS *fbset)
{
    if (fbset)
        memset(fbset, 0, sizeof(*fbset));
}

void BinkGetRealtime(HBINK bink, BINKREALTIME *run, U32 frames)
{
    if (run)
        memset(run, 0, sizeof(*run));
}

void BinkNextFrame(HBINK bnk)
{
}

BINKSNDOPEN BinkOpenDirectSound(UINTa param)
{
    return 0;
}

S32 BinkPause(HBINK bnk, S32 pause)
{
    return 0;
}

void BinkRegisterFrameBuffers(HBINK bink, BINKFRAMEBUFFERS *fbset)
{
}

void BinkSetIOSize(U32 iosize)
{
}

void BinkSetMemory(BINKMEMALLOC a, BINKMEMFREE f)
{
}

S32 BinkSetSoundSystem(BINKSNDSYSOPEN open, UINTa param)
{
    return 0;
}

void BinkSetSoundTrack(U32 total_tracks, U32 *tracks)
{
}

void BinkSetVolume(HBINK bnk, U32 trackid, S32 volume)
{
}

S32 BinkShouldSkip(HBINK bink)
{
    return 0;
}

S32 BinkWait(HBINK bnk)
{
    return 0;
}

// Bink's DirectSound output (R_CinematicInitSound): no device
HRESULT DirectSoundCreate8(LPCGUID device, LPDIRECTSOUND8 *ds, LPUNKNOWN outer)
{
    if (ds)
        *ds = nullptr;
    return DSERR_NODRIVER;
}

} // extern "C"
