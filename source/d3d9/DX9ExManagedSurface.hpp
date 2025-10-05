#pragma once

#include <d3d9.h>
#include <atomic>

class DX9ExManagedTexture;


class DX9ExManagedSurface : public IDirect3DSurface9
{
public:
    DX9ExManagedSurface(IDirect3DSurface9 *realSurface, DX9ExManagedTexture *parent, UINT level);
    ~DX9ExManagedSurface();

    // IUnknown
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObj) override;

    // IDirect3DResource9
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9 **ppDevice) override;
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID refguid, CONST void *pData, DWORD SizeOfData, DWORD Flags) override;
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID refguid, void *pData, DWORD *pSizeOfData) override;
    HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID refguid) override;
    D3DRESOURCETYPE STDMETHODCALLTYPE GetType() override;
    DWORD STDMETHODCALLTYPE SetPriority(DWORD PriorityNew) override;
    DWORD STDMETHODCALLTYPE GetPriority() override;
    void STDMETHODCALLTYPE PreLoad() override;

    // IDirect3DSurface9
    HRESULT STDMETHODCALLTYPE GetContainer(REFIID riid, void **ppContainer) override;
    HRESULT STDMETHODCALLTYPE GetDesc(D3DSURFACE_DESC *pDesc) override;
    HRESULT STDMETHODCALLTYPE LockRect(D3DLOCKED_RECT *pLockedRect, CONST RECT *pRect, DWORD Flags) override;
    HRESULT STDMETHODCALLTYPE UnlockRect() override;
    HRESULT STDMETHODCALLTYPE GetDC(HDC *phdc) override;
    HRESULT STDMETHODCALLTYPE ReleaseDC(HDC hdc) override;

    // Helper
    IDirect3DSurface9 *GetRealSurface() const { return _realSurface; }

private:
    std::atomic<ULONG> _refCount;
    IDirect3DSurface9 *_realSurface;
    DX9ExManagedTexture *_parent; // parent texture that owns the CPU/GPU storage
    UINT _level;
};

