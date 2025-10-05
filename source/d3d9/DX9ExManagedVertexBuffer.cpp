#include "DX9ExManagedVertexBuffer.hpp"
#include <utility>
#include "DX9ExManagedProxyIID.hpp"
#include "dll_log.hpp"

// Factory implementation
HRESULT CreateDX9ExManagedVertexBuffer(IDirect3DDevice9 *orig, UINT Length, DWORD Usage, DWORD FVF, D3DPOOL Pool, IDirect3DVertexBuffer9 **ppVertexBuffer, HANDLE *pSharedHandle)
{
    if (ppVertexBuffer == nullptr) return E_POINTER;
    *ppVertexBuffer = nullptr;

    if (orig == nullptr) return E_POINTER;

    // Create the GPU buffer on the raw device first so any failures are surfaced as HRESULT.
    IDirect3DVertexBuffer9 *gpuVB = nullptr;
    DWORD gpuUsage = Usage & ~D3DUSAGE_DYNAMIC;
    HRESULT hr = orig->CreateVertexBuffer(Length, gpuUsage, FVF, D3DPOOL_DEFAULT, &gpuVB, pSharedHandle);
    if (FAILED(hr)) return hr;

    // Allocate proxy and hand over ownership of gpuVB to it.
    DX9ExManagedVertexBuffer *proxy = new (std::nothrow) DX9ExManagedVertexBuffer(orig, gpuVB, Length, Usage, FVF, Pool);
    if (proxy == nullptr) {
        // clean up created gpu buffer if allocation failed
        if (gpuVB) gpuVB->Release();
        return E_OUTOFMEMORY;
    }

    *ppVertexBuffer = proxy;
    return S_OK;
}

DX9ExManagedVertexBuffer::DX9ExManagedVertexBuffer(IDirect3DDevice9 *device, IDirect3DVertexBuffer9 *gpuVB, UINT Length, DWORD Usage, DWORD FVF, D3DPOOL Pool)
    : refCount(1), device(device), systemVB(nullptr), gpuVB(gpuVB), length(Length), usage(Usage), fvf(FVF), pool(Pool),
      dirtyCpuFlag(false), dirtyGpuFlag(false)
{
    // Constructor intentionally does not perform device calls. GPU resource must be created by the factory.
}

DX9ExManagedVertexBuffer::~DX9ExManagedVertexBuffer()
{
    if (systemVB) systemVB->Release();
    if (gpuVB) gpuVB->Release();
}

// IUnknown
ULONG STDMETHODCALLTYPE DX9ExManagedVertexBuffer::AddRef()
{
    return refCount.fetch_add(1, std::memory_order_relaxed) + 1;
}

ULONG STDMETHODCALLTYPE DX9ExManagedVertexBuffer::Release()
{
    const ULONG prev = refCount.fetch_sub(1, std::memory_order_acq_rel);
    const ULONG newRef = prev - 1;
    if (newRef == 0) {
        if (systemVB) systemVB->Release();
        if (gpuVB) gpuVB->Release();
        delete this;
        return 0;
    }
    return newRef;
}

HRESULT STDMETHODCALLTYPE DX9ExManagedVertexBuffer::QueryInterface(REFIID riid, void **ppvObj)
{
    if (ppvObj == nullptr) return E_POINTER;

        if (riid == __uuidof(IDirect3DVertexBuffer9) || riid == IID_IUnknown)
        {
            *ppvObj = static_cast<IDirect3DVertexBuffer9*>(this);
            AddRef();
            return S_OK;
        }

        // Honor the proxy IID so device detection via QueryInterface works.
            if (riid == IID_DX9EX_MANAGED_PROXY)
        {
                reshade::log::message(reshade::log::level::debug, "DX9Ex managed proxy QueryInterface for vertex buffer object %p in %s.", this, __FUNCTION__);
            *ppvObj = static_cast<DX9ExManagedVertexBuffer*>(this);
            AddRef();
            return S_OK;
        }

    return gpuVB ? gpuVB->QueryInterface(riid, ppvObj) : E_NOINTERFACE;
}

// IDirect3DVertexBuffer9
HRESULT STDMETHODCALLTYPE DX9ExManagedVertexBuffer::Lock(UINT OffsetToLock, UINT SizeToLock, void **ppbData, DWORD Flags)
{
    EnsureSystemBuffer();
    SyncGpuToCpu();
    return systemVB ? systemVB->Lock(OffsetToLock, SizeToLock, ppbData, Flags) : D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE DX9ExManagedVertexBuffer::Unlock()
{
    dirtyCpuFlag.store(true, std::memory_order_release);
    return systemVB ? systemVB->Unlock() : D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE DX9ExManagedVertexBuffer::GetDesc(D3DVERTEXBUFFER_DESC *pDesc)
{
    return gpuVB ? gpuVB->GetDesc(pDesc) : E_FAIL;
}

// IDirect3DResource9
HRESULT STDMETHODCALLTYPE DX9ExManagedVertexBuffer::GetDevice(IDirect3DDevice9 **ppDevice)
{
    return gpuVB ? gpuVB->GetDevice(ppDevice) : E_FAIL;
}

HRESULT STDMETHODCALLTYPE DX9ExManagedVertexBuffer::SetPrivateData(REFGUID refguid, CONST void *pData, DWORD SizeOfData, DWORD Flags)
{
    return gpuVB ? gpuVB->SetPrivateData(refguid, pData, SizeOfData, Flags) : D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE DX9ExManagedVertexBuffer::GetPrivateData(REFGUID refguid, void *pData, DWORD *pSizeOfData)
{
    return gpuVB ? gpuVB->GetPrivateData(refguid, pData, pSizeOfData) : D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE DX9ExManagedVertexBuffer::FreePrivateData(REFGUID refguid)
{
    return gpuVB ? gpuVB->FreePrivateData(refguid) : D3DERR_INVALIDCALL;
}

DWORD STDMETHODCALLTYPE DX9ExManagedVertexBuffer::SetPriority(DWORD PriorityNew)
{
    return gpuVB ? gpuVB->SetPriority(PriorityNew) : 0;
}

DWORD STDMETHODCALLTYPE DX9ExManagedVertexBuffer::GetPriority()
{
    return gpuVB ? gpuVB->GetPriority() : 0;
}

void STDMETHODCALLTYPE DX9ExManagedVertexBuffer::PreLoad()
{
    if (gpuVB) gpuVB->PreLoad();
}

D3DRESOURCETYPE STDMETHODCALLTYPE DX9ExManagedVertexBuffer::GetType()
{
    return gpuVB ? gpuVB->GetType() : D3DRTYPE_VERTEXBUFFER;
}

// Helpers
void DX9ExManagedVertexBuffer::InvalidateCpu() { dirtyGpuFlag.store(true, std::memory_order_release); }
void DX9ExManagedVertexBuffer::InvalidateGpu() { dirtyCpuFlag.store(true, std::memory_order_release); }

void DX9ExManagedVertexBuffer::SyncIfNeeded()
{
    if (dirtyCpuFlag.load(std::memory_order_acquire)) {
        std::unique_lock<std::shared_mutex> lock(cs);
        EnsureSystemBuffer(true);
        dirtyCpuFlag.store(false, std::memory_order_release);
        // push from system to gpu
        if (systemVB && gpuVB) {
            // There is no UpdateVertexBuffer; use UpdateSubresource-like via Lock/Unlock of gpu buffer
            void *src = nullptr;
            void *dst = nullptr;
            if (SUCCEEDED(systemVB->Lock(0, 0, &src, D3DLOCK_READONLY)) && SUCCEEDED(gpuVB->Lock(0, 0, &dst, 0))) {
                memcpy(dst, src, length);
                gpuVB->Unlock();
                systemVB->Unlock();
            }
        }
        return;
    }

    if (dirtyGpuFlag.load(std::memory_order_acquire)) {
        std::unique_lock<std::shared_mutex> lock(cs);
        EnsureSystemBuffer(true);
        dirtyGpuFlag.store(false, std::memory_order_release);
        // pull from gpu to system
        if (systemVB && gpuVB) {
            void *src = nullptr;
            void *dst = nullptr;
            if (SUCCEEDED(gpuVB->Lock(0, 0, &src, D3DLOCK_READONLY)) && SUCCEEDED(systemVB->Lock(0, 0, &dst, 0))) {
                memcpy(dst, src, length);
                systemVB->Unlock();
                gpuVB->Unlock();
            }
        }
    }
}

void DX9ExManagedVertexBuffer::SyncGpuToCpu()
{
    if (!systemVB || !gpuVB) return;
    void *src = nullptr;
    void *dst = nullptr;
    if (SUCCEEDED(gpuVB->Lock(0, 0, &src, D3DLOCK_READONLY)) && SUCCEEDED(systemVB->Lock(0, 0, &dst, 0))) {
        memcpy(dst, src, length);
        systemVB->Unlock();
        gpuVB->Unlock();
    }
}

void DX9ExManagedVertexBuffer::EnsureSystemBuffer(bool alreadyLocked)
{
    if (!alreadyLocked)
    {
        // Take a shared lock just to check the fast-path. Release it before
        // taking the exclusive lock so the current thread does not deadlock
        // while attempting to upgrade.
        {
            std::shared_lock<std::shared_mutex> shared(cs);
            if (systemVB != nullptr) return;
        }

        std::unique_lock<std::shared_mutex> lock(cs);
        if (systemVB == nullptr) {
            device->CreateVertexBuffer(length, 0, fvf, D3DPOOL_SYSTEMMEM, &systemVB, nullptr);
            dirtyCpuFlag.store(false, std::memory_order_release);
        }
    }
    else
    {
        // Caller already holds a unique_lock on 'cs'. Create the system buffer directly if missing.
        if (systemVB == nullptr) {
            device->CreateVertexBuffer(length, 0, fvf, D3DPOOL_SYSTEMMEM, &systemVB, nullptr);
            dirtyCpuFlag.store(false, std::memory_order_release);
        }
    }
}
