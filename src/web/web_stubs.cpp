// web_stubs.cpp - stand-ins for the parts of the Windows build the browser build leaves out (cmake/web.cmake
// BO1_WEB_EXCLUDE). Each says what is missing; docs/web-port.md lists them with the plan for each.
#include <win32/win_voice.h>
#include <monkey/monkey.h>
#include <mjpeg/mjpeg.h>
#include <mjpeg/yuv.h>
#include <gfx_d3d/rb_backend.h>
#include <CubeMapGenLib/CCubeMapProcessor.h>
#include <qcommon/common.h>

// ===================================================================================================================
// voice chat (win32/win_voice.cpp, groupvoice/): web: stub, no microphone capture or speex codec; sv_voice is 0
// ===================================================================================================================
float voice_current_scaler = 1.0f;

bool __cdecl Voice_Init()
{
    return false;
}

void __cdecl Voice_StopClientSamples()
{
}

void __cdecl Voice_Shutdown()
{
}

double __cdecl Voice_GetVoiceLevel()
{
    return 0.0;
}

void __cdecl Voice_Playback()
{
}

int __cdecl Voice_GetLocalVoiceData()
{
    return 0;
}

void __cdecl Voice_IncomingVoiceData(unsigned __int8 talker, unsigned __int8 *data, int packetDataSize)
{
}

bool __cdecl Voice_IsClientTalking(unsigned int clientNum)
{
    return false;
}

char __cdecl Voice_StartRecording()
{
    return 0;
}

char __cdecl Voice_StopRecording()
{
    return 0;
}

unsigned int __cdecl mixerGetRecordLevel(char *SrcName)
{
    return 0;
}

int __cdecl mixerSetRecordLevel(char *SrcName, unsigned __int16 newLevel)
{
    return 0;
}

int __cdecl mixerGetRecordSource(char *srcName)
{
    return 0;
}

int __cdecl mixerSetRecordSource(char *SrcName)
{
    return 0;
}

int __cdecl mixerSetMicrophoneMute(unsigned __int8 bMute)
{
    return 0;
}

int __cdecl Live_GetClientNumForXuid(const SessionData_s *session, unsigned __int64 xuid)
{
    return -1;   // web: stub (win_voice.cpp's lookup); voice is off
}

// ===================================================================================================================
// monkey (test automation over Winsock, monkey/): web: stub, never running
// ===================================================================================================================
void __cdecl Monkey_Error(const char *text)
{
}

void __cdecl Monkey_GrabComPrints(bool enable)
{
}

void __cdecl Monkey_ComPrintHook(const char *msg)
{
}

void __cdecl Monkey_Start()
{
}

void __cdecl Monkey_AssertCallback(const char *assertMsg)
{
}

bool __cdecl Monkey_IsRunning()
{
    return false;
}

void __cdecl Monkey_Frame()
{
}

void __cdecl Monkey_KeepAlive()
{
}

void __cdecl Monkey_Event(const char *event)
{
}

bool __cdecl Monkey_UseRandomInput()
{
    return false;
}

// ===================================================================================================================
// movie capture (mjpeg/, movie_start): web: stub, never encoding
// ===================================================================================================================
bool mjpeg_run_encoder;

void mjpeg_initonce()
{
}

void __cdecl mjpeg_set_callback(void(__cdecl *callback)(unsigned __int8 *, unsigned __int8 *, unsigned __int8 *))
{
}

void __cdecl mjpeg_init()
{
}

bool __cdecl mjpeg_is_encoding()
{
    return false;
}

void __cdecl mjpeg_close()
{
}

void __cdecl mjpeg_draw()
{
}

void __cdecl yuv_lost_device()
{
}

void __cdecl yuv_recover_device()
{
}

// ===================================================================================================================
// NVIDIA driver API (nvapi/): web: stub, no NVIDIA driver; NvAPI_Initialize fails so dx.nvInitialized stays 0 and
// nothing else is called
// ===================================================================================================================
extern "C" {

NvAPI_Status __cdecl NvAPI_Initialize()
{
    return NVAPI_LIBRARY_NOT_FOUND;
}

NvAPI_Status __cdecl NvAPI_Stereo_CreateHandleFromIUnknown(IUnknown *pDevice, StereoHandle *pStereoHandle)
{
    if (pStereoHandle)
        *pStereoHandle = 0;
    return NVAPI_LIBRARY_NOT_FOUND;
}

NvAPI_Status __cdecl NvAPI_Stereo_DestroyHandle(StereoHandle stereoHandle)
{
    return NVAPI_LIBRARY_NOT_FOUND;
}

NvAPI_Status __cdecl NvAPI_Stereo_IsActivated(StereoHandle stereoHandle, NvU8 *pIsStereoOn)
{
    if (pIsStereoOn)
        *pIsStereoOn = 0;
    return NVAPI_LIBRARY_NOT_FOUND;
}

NvAPI_Status __cdecl NvAPI_Stereo_SetConvergence(StereoHandle stereoHandle, float newConvergence)
{
    return NVAPI_LIBRARY_NOT_FOUND;
}

NvAPI_Status __cdecl NvAPI_Stereo_IsEnabled(NvU8 *pIsStereoEnabled)
{
    if (pIsStereoEnabled)
        *pIsStereoEnabled = 0;
    return NVAPI_LIBRARY_NOT_FOUND;
}

NvAPI_Status __cdecl NvAPI_GetDisplayDriverVersion(NvDisplayHandle hNvDisplay, NV_DISPLAY_DRIVER_VERSION *pVersion)
{
    return NVAPI_LIBRARY_NOT_FOUND;
}

NvAPI_Status __cdecl NvAPI_D3D_GetCurrentSLIState(IUnknown *pDevice, NV_GET_CURRENT_SLI_STATE *pSliState)
{
    return NVAPI_LIBRARY_NOT_FOUND;
}

} // extern "C"

// ===================================================================================================================
// CubeMapGenLib (tools-only cube map filtering for r_screenshot's cube map shots): web: stub, never filters
// ===================================================================================================================
CImageSurface::CImageSurface(void)
{
}

CImageSurface::~CImageSurface()
{
}

CCubeMapProcessor::CCubeMapProcessor(void)
{
}

CCubeMapProcessor::~CCubeMapProcessor()
{
}

void CCubeMapProcessor::Init(int32 a_InputSize, int32 a_OutputSize, int32 a_NumMipLevels, int32 a_NumChannels)
{
}

void CCubeMapProcessor::SetInputFaceData(int32 a_FaceIdx, int32 a_SrcType, int32 a_SrcNumChannels, int32 a_SrcPitch,
    void *a_SrcDataPtr, float32 a_MaxClamp, float32 a_Degamma, float32 a_Scale)
{
}

void CCubeMapProcessor::GetOutputFaceData(int32 a_FaceIdx, int32 a_Level, int32 a_DstType, int32 a_DstNumChannels,
    int32 a_DstPitch, void *a_DstDataPtr, float32 a_Scale, float32 a_Gamma)
{
}

void CCubeMapProcessor::InitiateFiltering(float32 a_BaseFilterAngle, float32 a_InitialMipAngle, float32 a_MipAnglePerLevelScale,
    int32 a_FilterType, int32 a_FixupType, int32 a_FixupWidth, bool8 a_bUseSolidAngle)
{
}

int32 CCubeMapProcessor::GetStatus(void)
{
    return 0;
}
