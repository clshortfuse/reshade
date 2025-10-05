#include <d3d9.h>

#include "./DX9ExManagedTexture.hpp"
#include "./DX9ExManagedSurface.hpp"
#include <shared_mutex>
#include "DX9ExManagedProxyIID.hpp"
#include "dll_log.hpp"

DX9ExManagedTexture::DX9ExManagedTexture(IDirect3DDevice9* device, IDirect3DTexture9* gpuTex, UINT width, UINT height, UINT levels, DWORD usage, D3DFORMAT format)
    : refCount(1), device(device), systemTex(nullptr), gpuTex(gpuTex), width(width), height(height), levels(levels), format(format),
      originalUsage(usage), dirtyCpuFlag(false), dirtyGpuFlag(false), supportsAutogen((usage & D3DUSAGE_AUTOGENMIPMAP) != 0)
{
    // Constructor intentionally does not perform device calls. GPU resource must be created by the factory.
}

// Factory implementation
HRESULT CreateDX9ExManagedTexture(IDirect3DDevice9 *orig, UINT width, UINT height, UINT levels, DWORD usage, D3DFORMAT format, D3DPOOL pool, IDirect3DTexture9 **ppTexture, HANDLE *pSharedHandle)
{
    if (ppTexture == nullptr) return E_POINTER;
    *ppTexture = nullptr;

    if (orig == nullptr) return E_POINTER;

    // Create the GPU texture on the raw device first so failures are surfaced as HRESULT.
    IDirect3DTexture9 *gpuTex = nullptr;
    HRESULT hr = orig->CreateTexture(width, height, levels, usage, format, pool, &gpuTex, pSharedHandle);
    if (FAILED(hr)) return hr;

    // Allocate proxy and hand over ownership of gpuTex to it.
    DX9ExManagedTexture *proxy = new (std::nothrow) DX9ExManagedTexture(orig, gpuTex, width, height, levels, usage, format);
    if (proxy == nullptr) {
        if (gpuTex) gpuTex->Release();
        return E_OUTOFMEMORY;
    }

    *ppTexture = proxy;
    return S_OK;
}

DX9ExManagedTexture::~DX9ExManagedTexture()
{
    if (systemTex) systemTex->Release();
    if (gpuTex) gpuTex->Release();
}

// IUnknown
ULONG STDMETHODCALLTYPE DX9ExManagedTexture::AddRef()
{
    return refCount.fetch_add(1, std::memory_order_relaxed) + 1;
}

ULONG STDMETHODCALLTYPE DX9ExManagedTexture::Release()
{
    const ULONG prev = refCount.fetch_sub(1, std::memory_order_acq_rel);
    const ULONG newRef = prev - 1;
    if (newRef == 0) {
        if (systemTex) systemTex->Release();
        if (gpuTex) gpuTex->Release();
        delete this;
        return 0;
    }
    return newRef;
}

HRESULT STDMETHODCALLTYPE DX9ExManagedTexture::QueryInterface(REFIID riid, void** ppvObj)
{
    if (ppvObj == nullptr)
        return E_POINTER;

    if (riid == __uuidof(IDirect3DTexture9) || riid == IID_IUnknown) {
        *ppvObj = static_cast<IDirect3DTexture9*>(this);
        AddRef();
        return S_OK;
    }

    // Honor the proxy IID so device detection via QueryInterface works.
        if (riid == IID_DX9EX_MANAGED_PROXY)
    {
            reshade::log::message(reshade::log::level::debug, "DX9Ex managed proxy QueryInterface for texture object %p in %s.", this, __FUNCTION__);
        *ppvObj = static_cast<DX9ExManagedTexture*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObj = nullptr;
    return E_NOINTERFACE;
}

// IDirect3DTexture9
HRESULT STDMETHODCALLTYPE DX9ExManagedTexture::LockRect(UINT level, D3DLOCKED_RECT* pLockedRect, CONST RECT* pRect, DWORD flags)
{
    EnsureSystemTexture();
    SyncGpuToCpu(level);
    return systemTex->LockRect(level, pLockedRect, pRect, flags);
}

HRESULT STDMETHODCALLTYPE DX9ExManagedTexture::UnlockRect(UINT level)
{
    // Mark CPU dirty and unlock
    dirtyCpuFlag.store(true, std::memory_order_release);
    const HRESULT hr = systemTex ? systemTex->UnlockRect(level) : D3DERR_INVALIDCALL;
    return hr;
}

HRESULT STDMETHODCALLTYPE DX9ExManagedTexture::AddDirtyRect(CONST RECT* pDirtyRect)
{
    dirtyCpuFlag.store(true, std::memory_order_release);
    return systemTex ? systemTex->AddDirtyRect(pDirtyRect) : D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE DX9ExManagedTexture::GetLevelDesc(UINT level, D3DSURFACE_DESC* pDesc)
{
    return gpuTex->GetLevelDesc(level, pDesc);
}

HRESULT STDMETHODCALLTYPE DX9ExManagedTexture::GetSurfaceLevel(UINT level, IDirect3DSurface9** ppSurfaceLevel)
{
    IDirect3DSurface9* rawSurface = nullptr;
    HRESULT hr = gpuTex->GetSurfaceLevel(level, &rawSurface);
    if (FAILED(hr)) return hr;

    *ppSurfaceLevel = new DX9ExManagedSurface(rawSurface, this, level);
    rawSurface->Release();
    return S_OK;
}

// IDirect3DBaseTexture9
DWORD STDMETHODCALLTYPE DX9ExManagedTexture::GetLevelCount()
{
    return gpuTex ? gpuTex->GetLevelCount() : levels;
}

DWORD STDMETHODCALLTYPE DX9ExManagedTexture::SetLOD(DWORD LODNew)
{
    return gpuTex ? gpuTex->SetLOD(LODNew) : 0;
}

DWORD STDMETHODCALLTYPE DX9ExManagedTexture::GetLOD()
{
    return gpuTex ? gpuTex->GetLOD() : 0;
}

HRESULT STDMETHODCALLTYPE DX9ExManagedTexture::SetAutoGenFilterType(D3DTEXTUREFILTERTYPE FilterType)
{
    return supportsAutogen ? gpuTex->SetAutoGenFilterType(FilterType) : D3DERR_INVALIDCALL;
}

D3DTEXTUREFILTERTYPE STDMETHODCALLTYPE DX9ExManagedTexture::GetAutoGenFilterType()
{
    return supportsAutogen ? gpuTex->GetAutoGenFilterType() : D3DTEXF_NONE;
}

void STDMETHODCALLTYPE DX9ExManagedTexture::GenerateMipSubLevels()
{
    if (supportsAutogen) {
        gpuTex->GenerateMipSubLevels();
        dirtyGpuFlag.store(true, std::memory_order_release); // GPU updated mip levels -> CPU backup stale
    }
}

// IDirect3DResource9
HRESULT STDMETHODCALLTYPE DX9ExManagedTexture::GetDevice(IDirect3DDevice9** ppDevice)
{
    return gpuTex->GetDevice(ppDevice);
}

HRESULT STDMETHODCALLTYPE DX9ExManagedTexture::SetPrivateData(REFGUID refguid, CONST void* pData, DWORD SizeOfData, DWORD Flags)
{
    return gpuTex->SetPrivateData(refguid, pData, SizeOfData, Flags);
}

HRESULT STDMETHODCALLTYPE DX9ExManagedTexture::GetPrivateData(REFGUID refguid, void* pData, DWORD* pSizeOfData)
{
    return gpuTex->GetPrivateData(refguid, pData, pSizeOfData);
}

HRESULT STDMETHODCALLTYPE DX9ExManagedTexture::FreePrivateData(REFGUID refguid)
{
    return gpuTex->FreePrivateData(refguid);
}

DWORD STDMETHODCALLTYPE DX9ExManagedTexture::SetPriority(DWORD PriorityNew)
{
    return gpuTex->SetPriority(PriorityNew);
}

DWORD STDMETHODCALLTYPE DX9ExManagedTexture::GetPriority()
{
    return gpuTex->GetPriority();
}

void STDMETHODCALLTYPE DX9ExManagedTexture::PreLoad()
{
    gpuTex->PreLoad();
}

D3DRESOURCETYPE STDMETHODCALLTYPE DX9ExManagedTexture::GetType()
{
    return gpuTex->GetType();
}

// Emulation logic
void DX9ExManagedTexture::InvalidateCpu() { dirtyGpuFlag.store(1, std::memory_order_release); }
void DX9ExManagedTexture::InvalidateGpu() { dirtyCpuFlag.store(1, std::memory_order_release); }

void DX9ExManagedTexture::SyncIfNeeded()
{
    // Fast-path checks using atomic flags
    if (dirtyCpuFlag.load(std::memory_order_acquire)) {
        // CPU has changes -> push to GPU
        {
            // exclusive lock for creating or claiming system texture
            std::unique_lock<std::shared_mutex> lock(cs);
            // ensure system texture exists; tell it we already hold the lock
            EnsureSystemTexture(true);
            dirtyCpuFlag.store(false, std::memory_order_release);
        }

        device->UpdateTexture(systemTex, gpuTex);
        if (supportsAutogen) gpuTex->GenerateMipSubLevels();
        return;
    }

    if (dirtyGpuFlag.load(std::memory_order_acquire)) {
        // GPU has changes -> pull to system for each level
        {
            std::unique_lock<std::shared_mutex> lock(cs);
            EnsureSystemTexture(true);
            dirtyGpuFlag.store(false, std::memory_order_release);
        }

        for (UINT level = 0; level < levels; ++level)
            SyncGpuToCpu(level);
    }
}

void DX9ExManagedTexture::SyncGpuToCpu(UINT level)
{
    if (!systemTex) return; // nothing to sync into
    IDirect3DSurface9* gpuSurface = nullptr;
    IDirect3DSurface9* sysSurface = nullptr;
    if (SUCCEEDED(gpuTex->GetSurfaceLevel(level, &gpuSurface)) && SUCCEEDED(systemTex->GetSurfaceLevel(level, &sysSurface))) {
        device->UpdateSurface(gpuSurface, nullptr, sysSurface, nullptr);
        gpuSurface->Release();
        sysSurface->Release();
    } else {
        if (gpuSurface) gpuSurface->Release();
        if (sysSurface) sysSurface->Release();
    }
}

HRESULT DX9ExManagedTexture::SetTexture(IDirect3DDevice9* device, DWORD stage)
{
    SyncIfNeeded();
    return device->SetTexture(stage, gpuTex);
}

void DX9ExManagedTexture::EnsureSystemTexture(bool alreadyLocked)
{
    // Fast-path: take a short-lived shared lock just to check existence
    // and release it before taking the exclusive lock to avoid deadlock
    // when upgrading locks.
    if (!alreadyLocked)
    {
        {
            std::shared_lock<std::shared_mutex> shared(cs);
            if (systemTex != nullptr) return;
        }

        std::unique_lock<std::shared_mutex> lock(cs);
        if (systemTex == nullptr) {
            device->CreateTexture(width, height, levels, 0, format, D3DPOOL_SYSTEMMEM, &systemTex, nullptr);
            dirtyCpuFlag.store(false, std::memory_order_release);
        }
    }
    else
    {
        // Caller already holds a unique_lock on 'cs'. Create system texture directly if missing.
        if (systemTex == nullptr) {
            device->CreateTexture(width, height, levels, 0, format, D3DPOOL_SYSTEMMEM, &systemTex, nullptr);
            dirtyCpuFlag.store(false, std::memory_order_release);
        }
    }
}