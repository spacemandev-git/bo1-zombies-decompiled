// XAPOBase.h - type declarations only, for the web build (see XAudio2.h in this folder). CXAPOBase's methods live in
// the DirectX SDK's xapobase.lib on Windows; on the web nothing derived from it is ever constructed (the XAudio2
// driver is not compiled), so the methods are declared and never defined.
#pragma once
#include "XAudio2.h"

#define XAPO_MIN_CHANNELS 1
#define XAPO_MAX_CHANNELS 64
#define XAPO_MIN_FRAMERATE 1000
#define XAPO_MAX_FRAMERATE 200000
#define XAPO_REGISTRATION_STRING_LENGTH 256
#define XAPO_FLAG_CHANNELS_MUST_MATCH 0x00000001
#define XAPO_FLAG_FRAMERATE_MUST_MATCH 0x00000002
#define XAPO_FLAG_BITSPERSAMPLE_MUST_MATCH 0x00000004
#define XAPO_FLAG_BUFFERCOUNT_MUST_MATCH 0x00000008
#define XAPO_FLAG_INPLACE_REQUIRED 0x00000020
#define XAPO_FLAG_INPLACE_SUPPORTED 0x00000010
#define XAPOBASE_DEFAULT_FORMAT_TAG WAVE_FORMAT_IEEE_FLOAT
#define XAPOBASE_DEFAULT_FORMAT_MIN_CHANNELS XAPO_MIN_CHANNELS
#define XAPOBASE_DEFAULT_FORMAT_MAX_CHANNELS XAPO_MAX_CHANNELS
#define XAPOBASE_DEFAULT_FORMAT_MIN_FRAMERATE XAPO_MIN_FRAMERATE
#define XAPOBASE_DEFAULT_FORMAT_MAX_FRAMERATE XAPO_MAX_FRAMERATE
#define XAPOBASE_DEFAULT_FORMAT_BITSPERSAMPLE 32
#define XAPOBASE_DEFAULT_FLAG (XAPO_FLAG_CHANNELS_MUST_MATCH | XAPO_FLAG_FRAMERATE_MUST_MATCH | XAPO_FLAG_BITSPERSAMPLE_MUST_MATCH | XAPO_FLAG_BUFFERCOUNT_MUST_MATCH | XAPO_FLAG_INPLACE_SUPPORTED)
#define XAPOBASE_DEFAULT_BUFFER_COUNT 1

#pragma pack(push, 1)
typedef struct XAPO_REGISTRATION_PROPERTIES {
    CLSID clsid;
    WCHAR FriendlyName[XAPO_REGISTRATION_STRING_LENGTH];
    WCHAR CopyrightInfo[XAPO_REGISTRATION_STRING_LENGTH];
    UINT32 MajorVersion;
    UINT32 MinorVersion;
    UINT32 Flags;
    UINT32 MinInputBufferCount;
    UINT32 MaxInputBufferCount;
    UINT32 MinOutputBufferCount;
    UINT32 MaxOutputBufferCount;
} XAPO_REGISTRATION_PROPERTIES;
typedef struct XAPO_LOCKFORPROCESS_BUFFER_PARAMETERS {
    const WAVEFORMATEX *pFormat;
    UINT32 MaxFrameCount;
} XAPO_LOCKFORPROCESS_BUFFER_PARAMETERS;
typedef enum XAPO_BUFFER_FLAGS { XAPO_BUFFER_SILENT, XAPO_BUFFER_VALID } XAPO_BUFFER_FLAGS;
typedef struct XAPO_PROCESS_BUFFER_PARAMETERS {
    void *pBuffer;
    XAPO_BUFFER_FLAGS BufferFlags;
    UINT32 ValidFrameCount;
} XAPO_PROCESS_BUFFER_PARAMETERS;
#pragma pack(pop)

#ifdef __cplusplus
struct __declspec(uuid("A90BC001-E897-E897-55E4-9E4700000000")) IXAPO : IUnknown
{
    virtual HRESULT GetRegistrationProperties(XAPO_REGISTRATION_PROPERTIES **ppRegistrationProperties) = 0;
    virtual HRESULT IsInputFormatSupported(const WAVEFORMATEX *pOutputFormat, const WAVEFORMATEX *pRequestedInputFormat, WAVEFORMATEX **ppSupportedInputFormat) = 0;
    virtual HRESULT IsOutputFormatSupported(const WAVEFORMATEX *pInputFormat, const WAVEFORMATEX *pRequestedOutputFormat, WAVEFORMATEX **ppSupportedOutputFormat) = 0;
    virtual HRESULT Initialize(const void *pData, UINT32 DataByteSize) = 0;
    virtual void Reset() = 0;
    virtual HRESULT LockForProcess(UINT32 InputLockedParameterCount, const XAPO_LOCKFORPROCESS_BUFFER_PARAMETERS *pInputLockedParameters,
        UINT32 OutputLockedParameterCount, const XAPO_LOCKFORPROCESS_BUFFER_PARAMETERS *pOutputLockedParameters) = 0;
    virtual void UnlockForProcess() = 0;
    virtual void Process(UINT32 InputProcessParameterCount, const XAPO_PROCESS_BUFFER_PARAMETERS *pInputProcessParameters,
        UINT32 OutputProcessParameterCount, XAPO_PROCESS_BUFFER_PARAMETERS *pOutputProcessParameters, BOOL IsEnabled) = 0;
    virtual UINT32 CalcInputFrames(UINT32 OutputFrameCount) = 0;
    virtual UINT32 CalcOutputFrames(UINT32 InputFrameCount) = 0;
};
struct __declspec(uuid("A90BC001-E897-E897-55E4-9E4700000001")) IXAPOParameters : IUnknown
{
    virtual void SetParameters(const void *pParameters, UINT32 ParameterByteSize) = 0;
    virtual void GetParameters(void *pParameters, UINT32 ParameterByteSize) = 0;
};
class CXAPOBase : public IXAPO
{
public:
    CXAPOBase(const XAPO_REGISTRATION_PROPERTIES *pRegistrationProperties);
    virtual ~CXAPOBase();
    HRESULT QueryInterface(REFIID riid, void **ppInterface) override;
    ULONG AddRef() override;
    ULONG Release() override;
    HRESULT GetRegistrationProperties(XAPO_REGISTRATION_PROPERTIES **ppRegistrationProperties) override;
    HRESULT IsInputFormatSupported(const WAVEFORMATEX *pOutputFormat, const WAVEFORMATEX *pRequestedInputFormat, WAVEFORMATEX **ppSupportedInputFormat) override;
    HRESULT IsOutputFormatSupported(const WAVEFORMATEX *pInputFormat, const WAVEFORMATEX *pRequestedOutputFormat, WAVEFORMATEX **ppSupportedOutputFormat) override;
    HRESULT Initialize(const void *pData, UINT32 DataByteSize) override;
    void Reset() override;
    HRESULT LockForProcess(UINT32 InputLockedParameterCount, const XAPO_LOCKFORPROCESS_BUFFER_PARAMETERS *pInputLockedParameters,
        UINT32 OutputLockedParameterCount, const XAPO_LOCKFORPROCESS_BUFFER_PARAMETERS *pOutputLockedParameters) override;
    void UnlockForProcess() override;
    UINT32 CalcInputFrames(UINT32 OutputFrameCount) override;
    UINT32 CalcOutputFrames(UINT32 InputFrameCount) override;
protected:
    const XAPO_REGISTRATION_PROPERTIES *m_pRegistrationProperties;
    void *m_pfnMatrixMixFunction;
    float *m_pfl32MatrixCoefficients;
    UINT32 m_nSrcFormatType;
    BOOL m_fIsScalarMatrix;
    BOOL m_fIsLocked;
    LONG m_lReferenceCount;
    BOOL IsLocked() { return m_fIsLocked; }
};
#endif
