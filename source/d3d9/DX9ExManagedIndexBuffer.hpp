#pragma once

#include <d3d9.h>
#include <shared_mutex>
#include <mutex>
#include <atomic>

class DX9ExManagedIndexBuffer : public IDirect3DIndexBuffer9
{
public:
    DX9ExManagedIndexBuffer(IDirect3DDevice9* device, IDirect3DIndexBuffer9* gpuBuf, UINT length, DWORD usage, D3DFORMAT format);
    ~DX9ExManagedIndexBuffer();

    // IUnknown
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObj) override;

    // IDirect3DIndexBuffer9
    HRESULT STDMETHODCALLTYPE Lock(UINT OffsetToLock, UINT SizeToLock, void **ppbData, DWORD Flags) override;
    HRESULT STDMETHODCALLTYPE Unlock() override;
    HRESULT STDMETHODCALLTYPE GetDesc(D3DINDEXBUFFER_DESC *pDesc) override;

    // IDirect3DResource9
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9** ppDevice) override;
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID refguid, CONST void* pData, DWORD SizeOfData, DWORD Flags) override;
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID refguid, void* pData, DWORD* pSizeOfData) override;
    HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID refguid) override;
    DWORD STDMETHODCALLTYPE SetPriority(DWORD PriorityNew) override;
    DWORD STDMETHODCALLTYPE GetPriority() override;
    void STDMETHODCALLTYPE PreLoad() override;
    D3DRESOURCETYPE STDMETHODCALLTYPE GetType() override;

    // Helpers
    void InvalidateCpu();
    void InvalidateGpu();
    void SyncIfNeeded();
    IDirect3DIndexBuffer9 *GetSystemBuffer() { return systemBuf; }

    // Ensure system buffer exists. If 'alreadyLocked' is true the caller already holds unique_lock on cs.
    void EnsureSystemBuffer(bool alreadyLocked = false);

private:
    std::atomic<ULONG> refCount;
    IDirect3DDevice9* device;
    IDirect3DIndexBuffer9* systemBuf;
    IDirect3DIndexBuffer9* gpuBuf;
    UINT length;
    D3DFORMAT format;
    DWORD originalUsage;
    std::atomic<bool> dirtyCpuFlag;
    std::atomic<bool> dirtyGpuFlag;
    std::shared_mutex cs;
};

// Factory: creates the GPU index buffer on the raw device first, then returns a proxy wrapping it.
HRESULT CreateDX9ExManagedIndexBuffer(IDirect3DDevice9 *orig, UINT length, DWORD usage, D3DFORMAT format, D3DPOOL pool, IDirect3DIndexBuffer9 **ppIndexBuffer, HANDLE *pSharedHandle);
