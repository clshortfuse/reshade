#pragma once

#include <d3d9.h>
#include <shared_mutex>
#include <mutex>
#include <atomic>

class DX9ExManagedTexture : public IDirect3DTexture9
{
public:
    DX9ExManagedTexture(IDirect3DDevice9* device, IDirect3DTexture9* gpuTex, UINT width, UINT height, UINT levels, DWORD usage, D3DFORMAT format);
    ~DX9ExManagedTexture();

    // IUnknown
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObj) override;

    // IDirect3DTexture9
    HRESULT STDMETHODCALLTYPE LockRect(UINT level, D3DLOCKED_RECT* pLockedRect, CONST RECT* pRect, DWORD flags) override;
    HRESULT STDMETHODCALLTYPE UnlockRect(UINT level) override;
    HRESULT STDMETHODCALLTYPE AddDirtyRect(CONST RECT* pDirtyRect) override;
    HRESULT STDMETHODCALLTYPE GetLevelDesc(UINT level, D3DSURFACE_DESC* pDesc) override;
    HRESULT STDMETHODCALLTYPE GetSurfaceLevel(UINT level, IDirect3DSurface9** ppSurfaceLevel) override;

    // IDirect3DBaseTexture9
    DWORD STDMETHODCALLTYPE GetLevelCount() override;
    DWORD STDMETHODCALLTYPE SetLOD(DWORD LODNew) override;
    DWORD STDMETHODCALLTYPE GetLOD() override;
    HRESULT STDMETHODCALLTYPE SetAutoGenFilterType(D3DTEXTUREFILTERTYPE FilterType) override;
    D3DTEXTUREFILTERTYPE STDMETHODCALLTYPE GetAutoGenFilterType() override;
    void STDMETHODCALLTYPE GenerateMipSubLevels() override;

    // IDirect3DResource9
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9** ppDevice) override;
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID refguid, CONST void* pData, DWORD SizeOfData, DWORD Flags) override;
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID refguid, void* pData, DWORD* pSizeOfData) override;
    HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID refguid) override;
    DWORD STDMETHODCALLTYPE SetPriority(DWORD PriorityNew) override;
    DWORD STDMETHODCALLTYPE GetPriority() override;
    void STDMETHODCALLTYPE PreLoad() override;
    D3DRESOURCETYPE STDMETHODCALLTYPE GetType() override;

    // Emulation / helper methods
    void InvalidateCpu();
    void InvalidateGpu();
    void SyncIfNeeded();
    void SyncGpuToCpu(UINT level);
    HRESULT SetTexture(IDirect3DDevice9* device, DWORD stage);

    // Helpers for lazy system texture creation. If 'alreadyLocked' is true the caller already holds a unique_lock on 'cs'.
    void EnsureSystemTexture(bool alreadyLocked = false);
    IDirect3DTexture9 *GetSystemTexture() { return systemTex; }

private:
    std::atomic<ULONG> refCount;
    IDirect3DDevice9* device;
    IDirect3DTexture9* systemTex;
    IDirect3DTexture9* gpuTex;
    UINT width, height, levels;
    D3DFORMAT format;
    DWORD originalUsage;
    std::atomic<bool> dirtyCpuFlag;
    std::atomic<bool> dirtyGpuFlag;
    bool supportsAutogen;
    std::shared_mutex cs;
};

// Factory: creates the GPU texture on the raw device first, then returns a proxy wrapping it.
HRESULT CreateDX9ExManagedTexture(IDirect3DDevice9 *orig, UINT width, UINT height, UINT levels, DWORD usage, D3DFORMAT format, D3DPOOL pool, IDirect3DTexture9 **ppTexture, HANDLE *pSharedHandle);
