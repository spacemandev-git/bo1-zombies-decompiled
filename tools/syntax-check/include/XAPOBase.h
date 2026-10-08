// XAPOBase.h - syntax-check stand-in for the June 2010 DirectX SDK header of the same name, which MinGW-w64 does
// not ship. Only tools/syntax-check.sh puts this folder on the include path; the real build uses the SDK's header.
// Bodies are placeholders: nothing compiled against this file is ever linked or run.
#pragma once
#include <xapo.h>
typedef float FLOAT32;

class CXAPOBase : public IXAPO
{
protected:
    CXAPOBase(const XAPO_REGISTRATION_PROPERTIES *) {}
    virtual ~CXAPOBase() {}
    const XAPO_REGISTRATION_PROPERTIES *GetRegistrationPropertiesInternal() { return nullptr; }
    BOOL IsLocked() { return FALSE; }
    HRESULT ValidateFormatDefault(WAVEFORMATEX *, BOOL) { return S_OK; }
    HRESULT ValidateFormatPair(const WAVEFORMATEX *, WAVEFORMATEX *, BOOL) { return S_OK; }
    void ProcessThru(void *, FLOAT32 *, UINT32, UINT32, UINT32, BOOL) {}
    LONG m_lReferenceCount = 1;

public:
    STDMETHOD(QueryInterface)(REFIID, void **) { return E_NOINTERFACE; }
    STDMETHOD_(ULONG, AddRef)() { return 1; }
    STDMETHOD_(ULONG, Release)() { return 0; }
    STDMETHOD(GetRegistrationProperties)(XAPO_REGISTRATION_PROPERTIES **) { return E_NOTIMPL; }
    STDMETHOD(IsInputFormatSupported)(const WAVEFORMATEX *, const WAVEFORMATEX *, WAVEFORMATEX **) { return S_OK; }
    STDMETHOD(IsOutputFormatSupported)(const WAVEFORMATEX *, const WAVEFORMATEX *, WAVEFORMATEX **) { return S_OK; }
    STDMETHOD(Initialize)(const void *, UINT32) { return S_OK; }
    STDMETHOD_(void, Reset)() {}
    STDMETHOD(LockForProcess)(UINT32, const XAPO_LOCKFORPROCESS_BUFFER_PARAMETERS *, UINT32,
                              const XAPO_LOCKFORPROCESS_BUFFER_PARAMETERS *) { return S_OK; }
    STDMETHOD_(void, UnlockForProcess)() {}
    STDMETHOD_(UINT32, CalcInputFrames)(UINT32 n) { return n; }
    STDMETHOD_(UINT32, CalcOutputFrames)(UINT32 n) { return n; }
};
