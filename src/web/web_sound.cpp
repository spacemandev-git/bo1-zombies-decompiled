// web_sound.cpp - the sound driver API (SD_*, sound/snd_driver_xaudio2.h) for the web build: a null driver.
//
// Nothing is heard yet. Every voice the engine starts is simulated with the timing XAudio2 would give it, so the sound
// system behaves as on Windows: a voice starts when its start delay has passed (SD_UpdateVoice, as the XAudio2 driver
// does), advances SamplesPlayed at its sample rate times its pitch, and ends (SND_StopVoice) when its buffers have
// played; looping voices play until the engine stops them. Streamed voices pull 2 windows at a time from snd_stream.cpp
// and hand each back (Snd_StreamReleaseWindow) when its frames have played, exactly as the XAudio2 OnBufferEnd
// callback does, so the stream thread keeps reading and EOF ends the voice.
//
// The real output (a Web Audio mixer fed from these voices: PCM16 / MS-ADPCM decode, the engine's pan matrix and the
// DSP chain of snd_driver_xaudio2_dsp.cpp) replaces the "advance" step below; see docs/web-port.md.
#include <sound/snd_driver_xaudio2.h>
#include <sound/snd_stream.h>
#include <sound/snd_db.h>
#include <sound/snd_public_async.h>
#include <sound/snd.h>
#include <qcommon/common.h>
#include <win32/win_shared.h>

namespace
{
struct NullBuffer
{
    char *data;        // stream window (released when played); nullptr for RAM sounds
    uint32_t frames;
};

struct NullVoice
{
    bool exists;       // the XAudio2 source voice exists
    bool started;
    bool looping;
    uint32_t rate;
    uint32_t channels;
    snd_asset_format format;
    NullBuffer buffers[2];
    uint32_t head;
    uint32_t queued;
    uint32_t bufferQueuedCount;
    double framePos;   // frames played of the head buffer
    uint64_t samplesPlayed;
    unsigned int lastMs;
};

NullVoice s_voices[SND_MAX_VOICES];

uint32_t StreamWindowFrames(const NullVoice &v, const snd_asset *header, unsigned int size)
{
    const uint32_t ch = v.channels ? v.channels : 1;
    switch (v.format)
    {
    case SND_ASSET_FORMAT_PCMS16:
        return size / (2 * ch);
    case SND_ASSET_FORMAT_MSADPCM:
    {
        const uint32_t block = 262 * ch;   // nBlockAlign of iSND_CreateVoice; 512 frames per block
        return (size + block - 1) / block * 512;
    }
    default:
        // proportional to the file's frame count (formats the stream path does not use on PC)
        return header && header->data_size ? (uint32_t)((uint64_t)size * header->frame_count / header->data_size) : size / (2 * ch);
    }
}

void CreateVoice(int voiceIndex, const snd_asset *snd, bool looping)
{
    NullVoice &v = s_voices[voiceIndex];
    memset(&v, 0, sizeof(v));
    v.exists = true;
    v.looping = looping;
    v.rate = snd->frame_rate ? snd->frame_rate : 48000;
    v.channels = snd->channel_count;
    v.format = snd->format;
    v.lastMs = Sys_Milliseconds();
}

void ReleaseHead(unsigned int streamVoice, NullVoice &v)
{
    NullBuffer &b = v.buffers[v.head];
    if (b.data)
        Snd_StreamReleaseWindow(streamVoice, b.data);
    b.data = nullptr;
    b.frames = 0;
    v.head = (v.head + 1) % 2;
    --v.queued;
    v.framePos = 0;
}

// plays dt of the voice: consumes buffer frames, releases finished stream windows
void Advance(int voiceIndex)
{
    NullVoice &v = s_voices[voiceIndex];
    if (!v.exists)
        return;
    const unsigned int now = Sys_Milliseconds();
    const unsigned int dt = now - v.lastMs;
    v.lastMs = now;
    if (!v.started || g_snd.voice[voiceIndex].paused || !dt)
        return;
    double pitch = g_snd.voiceAliasHash[voiceIndex] ? SND_GetPitch(&g_snd.voice[voiceIndex]) : 1.0;
    if (!(pitch > 0.0))
        pitch = 1.0;
    double frames = (double)dt * v.rate * pitch / 1000.0;
    const bool stream = SND_IsStream(voiceIndex);
    while (frames > 0.0 && v.queued)
    {
        NullBuffer &b = v.buffers[v.head];
        const double remaining = (double)b.frames - v.framePos;
        if (frames < remaining)
        {
            v.framePos += frames;
            v.samplesPlayed += (uint64_t)frames;
            break;
        }
        v.samplesPlayed += (uint64_t)remaining;
        frames -= remaining;
        if (v.looping && !stream)
        {
            v.framePos = 0;
            if (b.frames)
                frames = fmod(frames, (double)b.frames);
            continue;
        }
        ReleaseHead(stream ? iSND_GetStreamChannel(voiceIndex) : 0, v);
    }
}

void ChannelError(int voiceIndex)
{
    const snd_alias_t *alias = g_snd.voice[voiceIndex].alias;
    SND_LengthNotify(voiceIndex, 0);
    SND_StopVoice(voiceIndex);
    alias->soundFile->exists = 0;
}

// SD_UpdateStreamVoice of snd_driver_xaudio2.cpp, with the null voice
void UpdateStreamVoice(int voiceIndex)
{
    const int streamVoice = iSND_GetStreamChannel(voiceIndex);
    snd_stream_status status = Snd_StreamStatus(streamVoice);
    NullVoice &v = s_voices[voiceIndex];
    const bool isLooping = (g_snd.voice[voiceIndex].alias->flags & 1) != 0;
    if (!status || status == SND_STREAM_STARVING || !Snd_StreamGetHeader(streamVoice))
        return;
    if (status == SND_STREAM_ERROR)
    {
        ChannelError(voiceIndex);
        return;
    }
    const snd_asset *header = Snd_StreamGetHeader(streamVoice);
    if (!v.exists)
    {
        if (status != SND_STREAM_OK)
            return;
        CreateVoice(voiceIndex, header, isLooping);
        const unsigned int lengthMS = (unsigned int)(1000LL * header->frame_count / header->frame_rate);
        SND_SetSoundFileVoiceInfo(voiceIndex, header->channel_count, header->frame_rate, lengthMS, 0, SFLS_LOADED);
        SND_UpdateVoice(&g_snd.voice[voiceIndex], 0.0);
    }
    Advance(voiceIndex);
    while (Snd_StreamGetFreeWindows(streamVoice))
    {
        if (v.queued >= 2)
            break;
        unsigned int size, position;
        char *data;
        status = Snd_StreamAcquireWindow(streamVoice, &size, &position, &data);
        switch (status)
        {
        case SND_STREAM_OK:
        {
            const uint32_t slot = (v.head + v.queued) % 2;
            v.buffers[slot].data = data;
            v.buffers[slot].frames = StreamWindowFrames(v, header, size);
            ++v.queued;
            ++v.bufferQueuedCount;
            break;
        }
        case SND_STREAM_STARVING:
            return;
        case SND_STREAM_EOF:
            if (!v.queued)
                SND_StopVoice(voiceIndex);
            return;
        case SND_STREAM_ERROR:
            ChannelError(voiceIndex);
            return;
        default:
            return;
        }
    }
}

int StartAliasStream(SndStartAliasInfo *startAliasInfo, unsigned int voiceIndex)
{
    char filename2[260];
    const snd_alias_t *alias = startAliasInfo->alias;
    const int streamVoice = iSND_GetStreamChannel(voiceIndex);
    SND_AliasGetFileName(alias, filename2, 256);
    if (!alias->soundFile->exists)
    {
        Com_PrintError(1, "Tried to play streamed sound '%s' from alias '%s', but it was not found at load time.\n",
            filename2, alias->name);
        return SND_SetPlaybackIdNotPlayed(voiceIndex);
    }
    unsigned int primeSize = 0;
    char *primeData = 0;
    if ((alias->flags & 0xC000) >> 14 == 3 && alias->soundFile->u.streamSnd->primeSnd)
    {
        primeData = alias->soundFile->u.streamSnd->primeSnd->buffer;
        primeSize = alias->soundFile->u.streamSnd->primeSnd->size;
    }
    Snd_StreamOpen(streamVoice, filename2, (alias->flags & 1) != 0, primeSize, primeData);
    SND_SetVoiceStartInfo(voiceIndex, startAliasInfo);
    SND_SetSoundFileVoiceInfo(voiceIndex, 0, 0, 0, 0, SFLS_LOADING);
    SD_UpdateVoice(voiceIndex);
    return g_snd.voice[voiceIndex].alias ? startAliasInfo->playbackId : -1;
}

int StartAliasRam(SndStartAliasInfo *startAliasInfo, int voiceIndex)
{
    const bool isLooping = (startAliasInfo->alias->flags & 1) != 0;
    snd_asset *snd = &startAliasInfo->alias->soundFile->u.loadSnd->sound;
    CreateVoice(voiceIndex, snd, isLooping);
    NullVoice &v = s_voices[voiceIndex];
    v.buffers[0].data = nullptr;
    v.buffers[0].frames = snd->frame_count;
    v.queued = 1;
    const unsigned int rate = snd->frame_rate;
    const unsigned int total_msec = isLooping ? 0 : startAliasInfo->startDelay + (__int64)((double)snd->frame_count * 1000.0 / (double)rate);
    SND_SetSoundFileVoiceInfo(voiceIndex, snd->channel_count, rate, total_msec, 0, SFLS_LOADED);
    SND_SetVoiceStartInfo(voiceIndex, startAliasInfo);
    return startAliasInfo->playbackId;
}
}

// ===================================================================================================================
// the driver API
// ===================================================================================================================
bool __cdecl SD_Xaudio2CanInit()
{
    return true;
}

char __cdecl SD_Init()
{
    memset(s_voices, 0, sizeof(s_voices));
    Com_Printf(9, "Sound: web null driver (no audio output yet; voices are timed, docs/web-port.md)\n");
    return 1;
}

void SD_Shutdown()
{
    for (int i = 0; i < SND_MAX_VOICES; ++i)
        SND_StopVoice(i);
    memset(s_voices, 0, sizeof(s_voices));
}

void __cdecl SD_PreUpdate()
{
}

void __cdecl SD_TruncateAudioDeviceNames(Font_s *font, float scale, int size)
{
}

unsigned int __cdecl iSND_GetStreamChannel(unsigned int index)
{
    return index;
}

void __cdecl iSND_ReleaseStreamBuffer(unsigned int streamVoice, unsigned int bufferIndex)
{
    NullBuffer &b = s_voices[streamVoice].buffers[bufferIndex];
    if (b.data)
    {
        char *data = b.data;
        b.data = nullptr;
        Snd_StreamReleaseWindow(streamVoice, data);
    }
}

void __cdecl SD_StopVoice(int voiceIndex)
{
    NullVoice &v = s_voices[voiceIndex];
    if (SND_IsStream(voiceIndex))
    {
        const int streamVoice = iSND_GetStreamChannel(voiceIndex);
        for (unsigned int i = 0; i < 2; ++i)
            iSND_ReleaseStreamBuffer(streamVoice, i);
        if (Snd_StreamStatus(streamVoice))
            Snd_StreamClose(streamVoice);
    }
    memset(&v, 0, sizeof(v));
}

void __cdecl SD_PauseVoice(int voiceIndex)
{
    Advance(voiceIndex);
    g_snd.voice[voiceIndex].paused = 1;
}

void __cdecl SD_UnpauseVoice(int voiceIndex)
{
    s_voices[voiceIndex].lastMs = Sys_Milliseconds();   // paused time does not play
}

__int64 SD_BO1SamplesPlayed(int voiceIndex)
{
    if (g_snd.voice[voiceIndex].soundFileInfo.loadingState == SFLS_LOADING || !s_voices[voiceIndex].exists)
        return -1;
    return (__int64)s_voices[voiceIndex].samplesPlayed;
}

void __cdecl SD_UpdateVoice(unsigned int voiceIndex)
{
    if (SND_IsStream(voiceIndex))
    {
        UpdateStreamVoice((int)voiceIndex);
        if (!g_snd.voiceAliasHash[voiceIndex])
            return;
    }
    if (g_snd.voice[voiceIndex].soundFileInfo.loadingState == SFLS_LOADING)
        return;
    NullVoice &v = s_voices[voiceIndex];
    snd_voice_t *voice = &g_snd.voice[voiceIndex];
    Advance((int)voiceIndex);
    if (!voice->startDelay && !v.samplesPlayed && v.queued && !voice->paused && !v.started)
    {
        v.started = true;
        v.lastMs = Sys_Milliseconds();
    }
    if (!v.queued)
        SND_StopVoice(voiceIndex);
}

int __cdecl SD_StartAlias(SndStartAliasInfo *startAliasInfo, unsigned int voice)
{
    if (SND_IsStream(voice))
        return StartAliasStream(startAliasInfo, voice);
    return StartAliasRam(startAliasInfo, voice);
}
