#include "DX9ExManagedSurface.hpp"
#include "DX9ExManagedTexture.hpp"
#include <utility>

DX9ExManagedSurface::DX9ExManagedSurface(IDirect3DSurface9 *realSurface, DX9ExManagedTexture *parent, UINT level)
    : _refCount(1), _realSurface(realSurface), _parent(parent), _level(level)
{
    if (_realSurface) _realSurface->AddRef();
}

DX9ExManagedSurface::~DX9ExManagedSurface()
{
    if (_realSurface) _realSurface->Release();
}

ULONG STDMETHODCALLTYPE DX9ExManagedSurface::AddRef()
{
    return _refCount.fetch_add(1, std::memory_order_relaxed) + 1;
}

ULONG STDMETHODCALLTYPE DX9ExManagedSurface::Release()
{
    const ULONG prev = _refCount.fetch_sub(1, std::memory_order_acq_rel);
    const ULONG newRef = prev - 1;
    if (newRef == 0) {
        // Release owned real surface and delete wrapper
        if (_realSurface) {
            _realSurface->Release();
            _realSurface = nullptr;
        }
        delete this;
        return 0;
    }
    return newRef;
}

HRESULT STDMETHODCALLTYPE DX9ExManagedSurface::QueryInterface(REFIID riid, void **ppvObj)
{
    if (ppvObj == nullptr) return E_POINTER;
    if (riid == __uuidof(IDirect3DSurface9) || riid == IID_IUnknown) {
        *ppvObj = static_cast<IDirect3DSurface9*>(this);
        AddRef();
        return S_OK;
    }
    return _realSurface->QueryInterface(riid, ppvObj);
}

// IDirect3DResource9
HRESULT STDMETHODCALLTYPE DX9ExManagedSurface::GetDevice(IDirect3DDevice9 **ppDevice)
{
    return _realSurface->GetDevice(ppDevice);
}

HRESULT STDMETHODCALLTYPE DX9ExManagedSurface::SetPrivateData(REFGUID refguid, CONST void *pData, DWORD SizeOfData, DWORD Flags)
{
    return _realSurface->SetPrivateData(refguid, pData, SizeOfData, Flags);
}

HRESULT STDMETHODCALLTYPE DX9ExManagedSurface::GetPrivateData(REFGUID refguid, void *pData, DWORD *pSizeOfData)
{
    return _realSurface->GetPrivateData(refguid, pData, pSizeOfData);
}

HRESULT STDMETHODCALLTYPE DX9ExManagedSurface::FreePrivateData(REFGUID refguid)
{
    return _realSurface->FreePrivateData(refguid);
}

D3DRESOURCETYPE STDMETHODCALLTYPE DX9ExManagedSurface::GetType()
{
    return _realSurface->GetType();
}

DWORD STDMETHODCALLTYPE DX9ExManagedSurface::SetPriority(DWORD PriorityNew)
{
    return _realSurface->SetPriority(PriorityNew);
}

DWORD STDMETHODCALLTYPE DX9ExManagedSurface::GetPriority()
{
    return _realSurface->GetPriority();
}

void STDMETHODCALLTYPE DX9ExManagedSurface::PreLoad()
{
    _realSurface->PreLoad();
}

// IDirect3DSurface9
HRESULT STDMETHODCALLTYPE DX9ExManagedSurface::GetContainer(REFIID riid, void **ppContainer)
{
    return _realSurface->GetContainer(riid, ppContainer);
}

HRESULT STDMETHODCALLTYPE DX9ExManagedSurface::GetDesc(D3DSURFACE_DESC *pDesc)
{
    return _realSurface->GetDesc(pDesc);
}

HRESULT STDMETHODCALLTYPE DX9ExManagedSurface::LockRect(D3DLOCKED_RECT *pLockedRect, CONST RECT *pRect, DWORD Flags)
{
    // Ensure parent texture's GPU content is visible in system memory before locking
    if (_parent) {
        _parent->EnsureSystemTexture();
        _parent->SyncGpuToCpu(_level);

        // Lock the system-memory surface for CPU access
        IDirect3DSurface9 *sysSurf = nullptr;
        IDirect3DTexture9 *sysTex = _parent->GetSystemTexture();
        if (sysTex != nullptr) {
            if (SUCCEEDED(sysTex->GetSurfaceLevel(_level, &sysSurf))) {
                const HRESULT hr = sysSurf->LockRect(pLockedRect, pRect, Flags);
                sysSurf->Release();
                return hr;
            }
        }
    }

    // Fallback to real surface
    return _realSurface ? _realSurface->LockRect(pLockedRect, pRect, Flags) : D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE DX9ExManagedSurface::UnlockRect()
{
    if (_parent) {
        // Unlock system-memory surface if present
        if (IDirect3DTexture9 *sysTex = _parent->GetSystemTexture(); sysTex != nullptr) {
            IDirect3DSurface9 *sysSurf = nullptr;
            if (SUCCEEDED(sysTex->GetSurfaceLevel(_level, &sysSurf))) {
                const HRESULT hr = sysSurf->UnlockRect();
                sysSurf->Release();
                _parent->InvalidateCpu();
                return hr;
            }
        }
        _parent->InvalidateCpu();
    }
    return _realSurface ? _realSurface->UnlockRect() : D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE DX9ExManagedSurface::GetDC(HDC *phdc)
{
    if (_parent) {
        _parent->EnsureSystemTexture();
        _parent->SyncIfNeeded();

        if (IDirect3DTexture9 *sysTex = _parent->GetSystemTexture(); sysTex != nullptr) {
            IDirect3DSurface9 *sysSurf = nullptr;
            if (SUCCEEDED(sysTex->GetSurfaceLevel(_level, &sysSurf))) {
                const HRESULT hr = sysSurf->GetDC(phdc);
                sysSurf->Release();
                return hr;
            }
        }
    }
    return _realSurface ? _realSurface->GetDC(phdc) : D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE DX9ExManagedSurface::ReleaseDC(HDC hdc)
{
    if (_parent) {
        if (IDirect3DTexture9 *sysTex = _parent->GetSystemTexture(); sysTex != nullptr) {
            IDirect3DSurface9 *sysSurf = nullptr;
            if (SUCCEEDED(sysTex->GetSurfaceLevel(_level, &sysSurf))) {
                const HRESULT hr = sysSurf->ReleaseDC(hdc);
                sysSurf->Release();
                _parent->InvalidateCpu();
                return hr;
            }
        }
        _parent->InvalidateCpu();
    }
    return _realSurface ? _realSurface->ReleaseDC(hdc) : D3DERR_INVALIDCALL;
}
