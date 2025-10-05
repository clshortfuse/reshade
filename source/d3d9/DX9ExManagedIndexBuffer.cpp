#include <d3d9.h>

#include "DX9ExManagedIndexBuffer.hpp"
#include "DX9ExManagedProxyIID.hpp"
#include "dll_log.hpp"

DX9ExManagedIndexBuffer::DX9ExManagedIndexBuffer(IDirect3DDevice9* device, IDirect3DIndexBuffer9* gpuBuf, UINT length, DWORD usage, D3DFORMAT format)
    : refCount(1), device(device), systemBuf(nullptr), gpuBuf(gpuBuf), length(length), format(format), originalUsage(usage), dirtyCpuFlag(false), dirtyGpuFlag(false)
{
    // Constructor intentionally does not perform device calls. GPU resource must be created by the factory.
}

HRESULT CreateDX9ExManagedIndexBuffer(IDirect3DDevice9 *orig, UINT length, DWORD usage, D3DFORMAT format, D3DPOOL pool, IDirect3DIndexBuffer9 **ppIndexBuffer, HANDLE *pSharedHandle)
{
    if (ppIndexBuffer == nullptr) return E_POINTER;
    *ppIndexBuffer = nullptr;

    if (orig == nullptr) return E_POINTER;

    IDirect3DIndexBuffer9 *gpuBuf = nullptr;
    HRESULT hr = orig->CreateIndexBuffer(length, usage, format, pool, &gpuBuf, pSharedHandle);
    if (FAILED(hr)) return hr;

    DX9ExManagedIndexBuffer *proxy = new (std::nothrow) DX9ExManagedIndexBuffer(orig, gpuBuf, length, usage, format);
    if (proxy == nullptr) {
        if (gpuBuf) gpuBuf->Release();
        return E_OUTOFMEMORY;
    }

    *ppIndexBuffer = proxy;
    return S_OK;
}

DX9ExManagedIndexBuffer::~DX9ExManagedIndexBuffer()
{
    if (systemBuf) systemBuf->Release();
    if (gpuBuf) gpuBuf->Release();
}

ULONG STDMETHODCALLTYPE DX9ExManagedIndexBuffer::AddRef()
{
    return refCount.fetch_add(1, std::memory_order_relaxed) + 1;
}

ULONG STDMETHODCALLTYPE DX9ExManagedIndexBuffer::Release()
{
    const ULONG prev = refCount.fetch_sub(1, std::memory_order_acq_rel);
    const ULONG newRef = prev - 1;
    if (newRef == 0) {
        if (systemBuf) systemBuf->Release();
        if (gpuBuf) gpuBuf->Release();
        delete this;
        return 0;
    }
    return newRef;
}

HRESULT STDMETHODCALLTYPE DX9ExManagedIndexBuffer::QueryInterface(REFIID riid, void** ppvObj)
{
    if (ppvObj == nullptr)
        return E_POINTER;

    if (riid == __uuidof(IDirect3DIndexBuffer9) || riid == IID_IUnknown) {
        *ppvObj = static_cast<IDirect3DIndexBuffer9*>(this);
        AddRef();
        return S_OK;
    }

    if (riid == IID_DX9EX_MANAGED_PROXY)
    {
        reshade::log::message(reshade::log::level::debug, "DX9Ex managed proxy QueryInterface for index buffer object %p in %s.", this, __FUNCTION__);
        *ppvObj = static_cast<DX9ExManagedIndexBuffer*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObj = nullptr;
    return E_NOINTERFACE;
}

HRESULT STDMETHODCALLTYPE DX9ExManagedIndexBuffer::Lock(UINT OffsetToLock, UINT SizeToLock, void **ppbData, DWORD Flags)
{
    EnsureSystemBuffer();
    SyncIfNeeded();
    return systemBuf->Lock(OffsetToLock, SizeToLock, ppbData, Flags);
}

HRESULT STDMETHODCALLTYPE DX9ExManagedIndexBuffer::Unlock()
{
    dirtyCpuFlag.store(true, std::memory_order_release);
    const HRESULT hr = systemBuf ? systemBuf->Unlock() : D3DERR_INVALIDCALL;
    return hr;
}

HRESULT STDMETHODCALLTYPE DX9ExManagedIndexBuffer::GetDesc(D3DINDEXBUFFER_DESC *pDesc)
{
    return gpuBuf->GetDesc(pDesc);
}

HRESULT STDMETHODCALLTYPE DX9ExManagedIndexBuffer::GetDevice(IDirect3DDevice9** ppDevice)
{
    return gpuBuf->GetDevice(ppDevice);
}

HRESULT STDMETHODCALLTYPE DX9ExManagedIndexBuffer::SetPrivateData(REFGUID refguid, CONST void* pData, DWORD SizeOfData, DWORD Flags)
{
    return gpuBuf->SetPrivateData(refguid, pData, SizeOfData, Flags);
}

HRESULT STDMETHODCALLTYPE DX9ExManagedIndexBuffer::GetPrivateData(REFGUID refguid, void* pData, DWORD* pSizeOfData)
{
    return gpuBuf->GetPrivateData(refguid, pData, pSizeOfData);
}

HRESULT STDMETHODCALLTYPE DX9ExManagedIndexBuffer::FreePrivateData(REFGUID refguid)
{
    return gpuBuf->FreePrivateData(refguid);
}

DWORD STDMETHODCALLTYPE DX9ExManagedIndexBuffer::SetPriority(DWORD PriorityNew)
{
    return gpuBuf->SetPriority(PriorityNew);
}

DWORD STDMETHODCALLTYPE DX9ExManagedIndexBuffer::GetPriority()
{
    return gpuBuf->GetPriority();
}

void STDMETHODCALLTYPE DX9ExManagedIndexBuffer::PreLoad()
{
    gpuBuf->PreLoad();
}

D3DRESOURCETYPE STDMETHODCALLTYPE DX9ExManagedIndexBuffer::GetType()
{
    return gpuBuf->GetType();
}

void DX9ExManagedIndexBuffer::InvalidateCpu() { dirtyGpuFlag.store(1, std::memory_order_release); }
void DX9ExManagedIndexBuffer::InvalidateGpu() { dirtyCpuFlag.store(1, std::memory_order_release); }

void DX9ExManagedIndexBuffer::SyncIfNeeded()
{
    if (dirtyCpuFlag.load(std::memory_order_acquire)) {
        std::unique_lock<std::shared_mutex> lock(cs);
        EnsureSystemBuffer(true);
        dirtyCpuFlag.store(false, std::memory_order_release);

        // push from system to gpu (no UpdateBuffer in D3D9): lock both and memcpy
        if (systemBuf && gpuBuf) {
            void *src = nullptr;
            void *dst = nullptr;
            if (SUCCEEDED(systemBuf->Lock(0, length, &src, D3DLOCK_READONLY)) && SUCCEEDED(gpuBuf->Lock(0, length, &dst, 0))) {
                memcpy(dst, src, length);
                gpuBuf->Unlock();
                systemBuf->Unlock();
            } else {
                if (dst) gpuBuf->Unlock();
                if (src) systemBuf->Unlock();
            }
        }
        return;
    }

    if (dirtyGpuFlag.load(std::memory_order_acquire)) {
        std::unique_lock<std::shared_mutex> lock(cs);
        EnsureSystemBuffer(true);
        dirtyGpuFlag.store(false, std::memory_order_release);

        // copy entire buffer from GPU to system
        void *dst = nullptr, *src = nullptr;
        if (SUCCEEDED(systemBuf->Lock(0, length, &dst, 0)) && SUCCEEDED(gpuBuf->Lock(0, length, &src, D3DLOCK_READONLY))) {
            memcpy(dst, src, length);
            systemBuf->Unlock();
            gpuBuf->Unlock();
        } else {
            if (dst) systemBuf->Unlock();
            if (src) gpuBuf->Unlock();
        }
    }
}

void DX9ExManagedIndexBuffer::EnsureSystemBuffer(bool alreadyLocked)
{
    if (!alreadyLocked)
    {
        {
            std::shared_lock<std::shared_mutex> shared(cs);
            if (systemBuf != nullptr) return;
        }

        std::unique_lock<std::shared_mutex> lock(cs);
        if (systemBuf == nullptr) {
            device->CreateIndexBuffer(length, 0, format, D3DPOOL_SYSTEMMEM, &systemBuf, nullptr);
            dirtyCpuFlag.store(false, std::memory_order_release);
        }
    }
    else
    {
        if (systemBuf == nullptr) {
            device->CreateIndexBuffer(length, 0, format, D3DPOOL_SYSTEMMEM, &systemBuf, nullptr);
            dirtyCpuFlag.store(false, std::memory_order_release);
        }
    }
}
