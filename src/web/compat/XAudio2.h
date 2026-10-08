// XAudio2.h - type declarations only, for the web build. The engine's sound headers (src/sound/snd_driver_xaudio2.h,
// snd_driver_xaudio2_dsp.h) embed XAudio2 types in structs that the sound core includes; the XAudio2 driver itself
// (snd_driver_xaudio2*.cpp) is not compiled for the web - src/web/web_sound.cpp implements the SD_* API instead.
// Nothing here has an implementation; the interfaces are abstract.
#pragma once
#include "windows.h"

#pragma pack(push, 1)
typedef struct tWAVEFORMATEX {
    WORD wFormatTag;
    WORD nChannels;
    DWORD nSamplesPerSec;
    DWORD nAvgBytesPerSec;
    WORD nBlockAlign;
    WORD wBitsPerSample;
    WORD cbSize;
} WAVEFORMATEX, *PWAVEFORMATEX, *LPWAVEFORMATEX;
typedef const WAVEFORMATEX *LPCWAVEFORMATEX;
typedef struct {
    WAVEFORMATEX Format;
    union { WORD wValidBitsPerSample; WORD wSamplesPerBlock; WORD wReserved; } Samples;
    DWORD dwChannelMask;
    GUID SubFormat;
} WAVEFORMATEXTENSIBLE, *PWAVEFORMATEXTENSIBLE;
typedef struct adpcmcoef_tag { short iCoef1; short iCoef2; } ADPCMCOEFSET;
typedef struct adpcmwaveformat_tag {
    WAVEFORMATEX wfx;
    WORD wSamplesPerBlock;
    WORD wNumCoef;
    ADPCMCOEFSET aCoef[1];
} ADPCMWAVEFORMAT;
#pragma pack(pop)
#define WAVE_FORMAT_PCM 1
#define WAVE_FORMAT_ADPCM 2
#define WAVE_FORMAT_IEEE_FLOAT 3
#define WAVE_FORMAT_EXTENSIBLE 0xFFFE
#define SPEAKER_FRONT_LEFT 0x1
#define SPEAKER_FRONT_RIGHT 0x2
#define SPEAKER_FRONT_CENTER 0x4
#define SPEAKER_LOW_FREQUENCY 0x8
#define SPEAKER_BACK_LEFT 0x10
#define SPEAKER_BACK_RIGHT 0x20
#define SPEAKER_SIDE_LEFT 0x200
#define SPEAKER_SIDE_RIGHT 0x400

#define XAUDIO2_MAX_BUFFER_BYTES 0x80000000
#define XAUDIO2_MAX_QUEUED_BUFFERS 64
#define XAUDIO2_COMMIT_NOW 0
#define XAUDIO2_COMMIT_ALL 0
#define XAUDIO2_NO_LOOP_REGION 0
#define XAUDIO2_LOOP_INFINITE 255
#define XAUDIO2_DEFAULT_CHANNELS 0
#define XAUDIO2_DEFAULT_SAMPLERATE 0
#define XAUDIO2_END_OF_STREAM 0x0040
#define XAUDIO2_VOICE_NOPITCH 0x0002
#define XAUDIO2_VOICE_NOSRC 0x0004
#define XAUDIO2_VOICE_USEFILTER 0x0008
#define XAUDIO2_PLAY_TAILS 0x0020
#define XAUDIO2_VOICE_NOSAMPLESPLAYED 0x0100
#define XAUDIO2_DEBUG_ENGINE 0x0001

typedef enum XAUDIO2_WINDOWS_PROCESSOR_SPECIFIER {
    Processor1 = 0x00000001,
    XAUDIO2_ANY_PROCESSOR = 0xffffffff,
    XAUDIO2_DEFAULT_PROCESSOR = XAUDIO2_ANY_PROCESSOR
} XAUDIO2_WINDOWS_PROCESSOR_SPECIFIER, XAUDIO2_PROCESSOR;
typedef enum XAUDIO2_DEVICE_ROLE {
    NotDefaultDevice = 0x0,
    DefaultConsoleDevice = 0x1,
    DefaultMultimediaDevice = 0x2,
    DefaultCommunicationsDevice = 0x4,
    DefaultGameDevice = 0x8,
    GlobalDefaultDevice = 0xf,
    InvalidDeviceRole = ~GlobalDefaultDevice
} XAUDIO2_DEVICE_ROLE;
typedef enum XAUDIO2_FILTER_TYPE { LowPassFilter, BandPassFilter, HighPassFilter, NotchFilter } XAUDIO2_FILTER_TYPE;

#pragma pack(push, 1)
typedef struct XAUDIO2_DEVICE_DETAILS {
    WCHAR DeviceID[256];
    WCHAR DisplayName[256];
    XAUDIO2_DEVICE_ROLE Role;
    WAVEFORMATEXTENSIBLE OutputFormat;
} XAUDIO2_DEVICE_DETAILS;
typedef struct XAUDIO2_VOICE_DETAILS { UINT32 CreationFlags; UINT32 InputChannels; UINT32 InputSampleRate; } XAUDIO2_VOICE_DETAILS;
typedef struct XAUDIO2_SEND_DESCRIPTOR { UINT32 Flags; struct IXAudio2Voice *pOutputVoice; } XAUDIO2_SEND_DESCRIPTOR;
typedef struct XAUDIO2_VOICE_SENDS { UINT32 SendCount; XAUDIO2_SEND_DESCRIPTOR *pSends; } XAUDIO2_VOICE_SENDS;
typedef struct XAUDIO2_EFFECT_DESCRIPTOR { IUnknown *pEffect; BOOL InitialState; UINT32 OutputChannels; } XAUDIO2_EFFECT_DESCRIPTOR;
typedef struct XAUDIO2_EFFECT_CHAIN { UINT32 EffectCount; XAUDIO2_EFFECT_DESCRIPTOR *pEffectDescriptors; } XAUDIO2_EFFECT_CHAIN;
typedef struct XAUDIO2_FILTER_PARAMETERS { XAUDIO2_FILTER_TYPE Type; float Frequency; float OneOverQ; } XAUDIO2_FILTER_PARAMETERS;
typedef struct XAUDIO2_BUFFER {
    UINT32 Flags;
    UINT32 AudioBytes;
    const BYTE *pAudioData;
    UINT32 PlayBegin;
    UINT32 PlayLength;
    UINT32 LoopBegin;
    UINT32 LoopLength;
    UINT32 LoopCount;
    void *pContext;
} XAUDIO2_BUFFER;
typedef struct XAUDIO2_BUFFER_WMA { const UINT32 *pDecodedPacketCumulativeBytes; UINT32 PacketCount; } XAUDIO2_BUFFER_WMA;
typedef struct XAUDIO2_VOICE_STATE { void *pCurrentBufferContext; UINT32 BuffersQueued; UINT64 SamplesPlayed; } XAUDIO2_VOICE_STATE;
typedef struct XAUDIO2_PERFORMANCE_DATA {
    UINT64 AudioCyclesSinceLastQuery;
    UINT64 TotalCyclesSinceLastQuery;
    UINT32 MinimumCyclesPerQuantum;
    UINT32 MaximumCyclesPerQuantum;
    UINT32 MemoryUsageInBytes;
    UINT32 CurrentLatencyInSamples;
    UINT32 GlitchesSinceEngineStarted;
    UINT32 ActiveSourceVoiceCount;
    UINT32 TotalSourceVoiceCount;
    UINT32 ActiveSubmixVoiceCount;
    UINT32 TotalSubmixVoiceCount;
    UINT32 ActiveXmaSourceVoices;
    UINT32 ActiveXmaStreams;
} XAUDIO2_PERFORMANCE_DATA;
typedef struct XAUDIO2_DEBUG_CONFIGURATION {
    UINT32 TraceMask;
    UINT32 BreakMask;
    BOOL LogThreadID;
    BOOL LogFileline;
    BOOL LogFunctionName;
    BOOL LogTiming;
} XAUDIO2_DEBUG_CONFIGURATION;
#pragma pack(pop)

#ifdef __cplusplus
struct IXAudio2VoiceCallback
{
    virtual void OnVoiceProcessingPassStart(UINT32 BytesRequired) = 0;
    virtual void OnVoiceProcessingPassEnd() = 0;
    virtual void OnStreamEnd() = 0;
    virtual void OnBufferStart(void *pBufferContext) = 0;
    virtual void OnBufferEnd(void *pBufferContext) = 0;
    virtual void OnLoopEnd(void *pBufferContext) = 0;
    virtual void OnVoiceError(void *pBufferContext, HRESULT Error) = 0;
};
struct IXAudio2EngineCallback
{
    virtual void OnProcessingPassStart() = 0;
    virtual void OnProcessingPassEnd() = 0;
    virtual void OnCriticalError(HRESULT Error) = 0;
};
struct IXAudio2Voice
{
    virtual void GetVoiceDetails(XAUDIO2_VOICE_DETAILS *pVoiceDetails) = 0;
    virtual HRESULT SetOutputVoices(const XAUDIO2_VOICE_SENDS *pSendList) = 0;
    virtual HRESULT SetEffectChain(const XAUDIO2_EFFECT_CHAIN *pEffectChain) = 0;
    virtual HRESULT EnableEffect(UINT32 EffectIndex, UINT32 OperationSet = 0) = 0;
    virtual HRESULT DisableEffect(UINT32 EffectIndex, UINT32 OperationSet = 0) = 0;
    virtual void GetEffectState(UINT32 EffectIndex, BOOL *pEnabled) = 0;
    virtual HRESULT SetEffectParameters(UINT32 EffectIndex, const void *pParameters, UINT32 ParametersByteSize, UINT32 OperationSet = 0) = 0;
    virtual HRESULT GetEffectParameters(UINT32 EffectIndex, void *pParameters, UINT32 ParametersByteSize) = 0;
    virtual HRESULT SetFilterParameters(const XAUDIO2_FILTER_PARAMETERS *pParameters, UINT32 OperationSet = 0) = 0;
    virtual void GetFilterParameters(XAUDIO2_FILTER_PARAMETERS *pParameters) = 0;
    virtual HRESULT SetOutputFilterParameters(IXAudio2Voice *pDestinationVoice, const XAUDIO2_FILTER_PARAMETERS *pParameters, UINT32 OperationSet = 0) = 0;
    virtual void GetOutputFilterParameters(IXAudio2Voice *pDestinationVoice, XAUDIO2_FILTER_PARAMETERS *pParameters) = 0;
    virtual HRESULT SetVolume(float Volume, UINT32 OperationSet = 0) = 0;
    virtual void GetVolume(float *pVolume) = 0;
    virtual HRESULT SetChannelVolumes(UINT32 Channels, const float *pVolumes, UINT32 OperationSet = 0) = 0;
    virtual void GetChannelVolumes(UINT32 Channels, float *pVolumes) = 0;
    virtual HRESULT SetOutputMatrix(IXAudio2Voice *pDestinationVoice, UINT32 SourceChannels, UINT32 DestinationChannels, const float *pLevelMatrix, UINT32 OperationSet = 0) = 0;
    virtual void GetOutputMatrix(IXAudio2Voice *pDestinationVoice, UINT32 SourceChannels, UINT32 DestinationChannels, float *pLevelMatrix) = 0;
    virtual void DestroyVoice() = 0;
};
struct IXAudio2SourceVoice : IXAudio2Voice
{
    virtual HRESULT Start(UINT32 Flags = 0, UINT32 OperationSet = 0) = 0;
    virtual HRESULT Stop(UINT32 Flags = 0, UINT32 OperationSet = 0) = 0;
    virtual HRESULT SubmitSourceBuffer(const XAUDIO2_BUFFER *pBuffer, const XAUDIO2_BUFFER_WMA *pBufferWMA = 0) = 0;
    virtual HRESULT FlushSourceBuffers() = 0;
    virtual HRESULT Discontinuity() = 0;
    virtual HRESULT ExitLoop(UINT32 OperationSet = 0) = 0;
    virtual void GetState(XAUDIO2_VOICE_STATE *pVoiceState) = 0;
    virtual HRESULT SetFrequencyRatio(float Ratio, UINT32 OperationSet = 0) = 0;
    virtual void GetFrequencyRatio(float *pRatio) = 0;
    virtual HRESULT SetSourceSampleRate(UINT32 NewSourceSampleRate) = 0;
};
struct IXAudio2SubmixVoice : IXAudio2Voice {};
struct IXAudio2MasteringVoice : IXAudio2Voice {};
struct IXAudio2 : IUnknown
{
    virtual HRESULT GetDeviceCount(UINT32 *pCount) = 0;
    virtual HRESULT GetDeviceDetails(UINT32 Index, XAUDIO2_DEVICE_DETAILS *pDeviceDetails) = 0;
    virtual HRESULT Initialize(UINT32 Flags = 0, XAUDIO2_PROCESSOR XAudio2Processor = XAUDIO2_DEFAULT_PROCESSOR) = 0;
    virtual HRESULT RegisterForCallbacks(IXAudio2EngineCallback *pCallback) = 0;
    virtual void UnregisterForCallbacks(IXAudio2EngineCallback *pCallback) = 0;
    virtual HRESULT CreateSourceVoice(IXAudio2SourceVoice **ppSourceVoice, const WAVEFORMATEX *pSourceFormat, UINT32 Flags = 0,
        float MaxFrequencyRatio = 2.0f, IXAudio2VoiceCallback *pCallback = 0, const XAUDIO2_VOICE_SENDS *pSendList = 0,
        const XAUDIO2_EFFECT_CHAIN *pEffectChain = 0) = 0;
    virtual HRESULT CreateSubmixVoice(IXAudio2SubmixVoice **ppSubmixVoice, UINT32 InputChannels, UINT32 InputSampleRate,
        UINT32 Flags = 0, UINT32 ProcessingStage = 0, const XAUDIO2_VOICE_SENDS *pSendList = 0,
        const XAUDIO2_EFFECT_CHAIN *pEffectChain = 0) = 0;
    virtual HRESULT CreateMasteringVoice(IXAudio2MasteringVoice **ppMasteringVoice, UINT32 InputChannels = 0,
        UINT32 InputSampleRate = 0, UINT32 Flags = 0, UINT32 DeviceIndex = 0, const XAUDIO2_EFFECT_CHAIN *pEffectChain = 0) = 0;
    virtual HRESULT StartEngine() = 0;
    virtual void StopEngine() = 0;
    virtual HRESULT CommitChanges(UINT32 OperationSet) = 0;
    virtual void GetPerformanceData(XAUDIO2_PERFORMANCE_DATA *pPerfData) = 0;
    virtual void SetDebugConfiguration(const XAUDIO2_DEBUG_CONFIGURATION *pDebugConfiguration, void *pReserved = 0) = 0;
};
#endif
