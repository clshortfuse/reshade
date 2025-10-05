/* Vertex buffer proxy that lazily keeps a SYSTEMMEM copy for CPU access while the real GPU buffer
   lives in DEFAULT pool. Mirrors DX9ExManagedTexture design with std::shared_mutex and atomics. */
#pragma once

#include <d3d9.h>
#include <shared_mutex>
#include <atomic>

__declspec(uuid("019997f2-4447-748a-bc7d-ca46301323a6"))
class DX9ExManagedVertexBuffer : public IDirect3DVertexBuffer9
{
public:
    // Construction is private; use CreateDX9ExManagedVertexBuffer factory which returns an HRESULT.
    ~DX9ExManagedVertexBuffer();

    // IUnknown
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObj) override;

    // IDirect3DVertexBuffer9
    HRESULT STDMETHODCALLTYPE Lock(UINT OffsetToLock, UINT SizeToLock, void **ppbData, DWORD Flags) override;
    HRESULT STDMETHODCALLTYPE Unlock() override;
    HRESULT STDMETHODCALLTYPE GetDesc(D3DVERTEXBUFFER_DESC *pDesc) override;

    // IDirect3DResource9
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9 **ppDevice) override;
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID refguid, CONST void *pData, DWORD SizeOfData, DWORD Flags) override;
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID refguid, void *pData, DWORD *pSizeOfData) override;
    HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID refguid) override;
    DWORD STDMETHODCALLTYPE SetPriority(DWORD PriorityNew) override;
    DWORD STDMETHODCALLTYPE GetPriority() override;
    void STDMETHODCALLTYPE PreLoad() override;
    D3DRESOURCETYPE STDMETHODCALLTYPE GetType() override;

    // Helpers
    void InvalidateCpu();
    void InvalidateGpu();
    void SyncIfNeeded();
    void SyncGpuToCpu();
    // Ensure the SYSTEMMEM backup exists. If 'alreadyLocked' is true the caller already holds a unique_lock on 'cs'.
    void EnsureSystemBuffer(bool alreadyLocked = false);

    IDirect3DVertexBuffer9 *GetSystemBuffer() { return systemVB; }
    // Return the underlying GPU vertex buffer (may be null until created)
    IDirect3DVertexBuffer9 *GetGpuBuffer() { return gpuVB; }
private:
    // Private ctor: only factory may construct.
    DX9ExManagedVertexBuffer(IDirect3DDevice9 *device, IDirect3DVertexBuffer9 *gpuVB, UINT Length, DWORD Usage, DWORD FVF, D3DPOOL Pool);

    // Factory is friend so it can construct objects (and handle HRESULT-returning device calls).
    friend HRESULT CreateDX9ExManagedVertexBuffer(IDirect3DDevice9 *orig, UINT Length, DWORD Usage, DWORD FVF, D3DPOOL Pool, IDirect3DVertexBuffer9 **ppVertexBuffer, HANDLE *pSharedHandle);

    std::atomic<ULONG> refCount;
    IDirect3DDevice9 *device;
    IDirect3DVertexBuffer9 *systemVB; // SYSTEMMEM copy used for CPU access
    IDirect3DVertexBuffer9 *gpuVB;    // real GPU buffer in DEFAULT pool
    UINT length;
    DWORD usage;
    DWORD fvf;
    D3DPOOL pool;
    std::atomic<bool> dirtyCpuFlag;
    std::atomic<bool> dirtyGpuFlag;
    std::shared_mutex cs;
};

// Factory: create a DX9Ex managed VB that owns a GPU buffer on the raw device and a lazy SYSTEMMEM copy.
// Returns an HRESULT and the created proxy in ppVertexBuffer on success.
HRESULT CreateDX9ExManagedVertexBuffer(IDirect3DDevice9 *orig, UINT Length, DWORD Usage, DWORD FVF, D3DPOOL Pool, IDirect3DVertexBuffer9 **ppVertexBuffer, HANDLE *pSharedHandle);
