#include "render/skia/D3D12GrContext.h"

#include "core/Log.h"

#include <include/gpu/ganesh/GrContextOptions.h>
#include <include/gpu/ganesh/GrTypes.h>
#include <include/gpu/ganesh/d3d/GrD3DBackendContext.h>
#include <include/gpu/ganesh/d3d/GrD3DDirectContext.h>

#include <dxgi1_4.h>

namespace xgu::render {

D3D12GrContext::~D3D12GrContext() { reset(); }

void D3D12GrContext::setDevice(ID3D12Device* device, ID3D12CommandQueue* queue) {
    device_ = device;
    queue_ = queue;
}

bool D3D12GrContext::ensureContext() {
    if (context_) {
        return true;
    }
    if (!hasDevice()) {
        XGU_LOG_ERROR("D3D12GrContext: no device/queue configured");
        return false;
    }

    // Resolve the adapter that owns the device (never assume adapter 0: the dev
    // machine has a discrete NVIDIA GPU and an AMD iGPU).
    Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    if (SUCCEEDED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)))) {
        const LUID luid = device_->GetAdapterLuid();
        Microsoft::WRL::ComPtr<IDXGIAdapter> adapterBase;
        if (SUCCEEDED(factory->EnumAdapterByLuid(luid, IID_PPV_ARGS(&adapterBase)))) {
            adapterBase.As(&adapter);
        }
    }
    if (!adapter) {
        XGU_LOG_ERROR("D3D12GrContext: failed to resolve the DXGI adapter for the device");
        return false;
    }

    GrD3DBackendContext backend;
    adapter->AddRef();
    backend.fAdapter.reset(adapter.Get());
    device_->AddRef();
    backend.fDevice.reset(device_.Get());
    queue_->AddRef();
    backend.fQueue.reset(queue_.Get());
    backend.fMemoryAllocator = nullptr; // Skia creates its D3D12MA-based allocator
    backend.fProtectedContext = skgpu::Protected::kNo;

    GrContextOptions options;
    options.fSuppressPrints = false;
    options.fReducedShaderVariations = true;

    context_ = GrDirectContexts::MakeD3D(backend, options);
    if (!context_) {
        XGU_LOG_ERROR("D3D12GrContext: GrDirectContexts::MakeD3D failed");
        return false;
    }

    DXGI_ADAPTER_DESC1 desc{};
    adapter->GetDesc1(&desc);
    char name[128];
    WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, name, sizeof(name), nullptr, nullptr);
    XGU_LOG_INFO("Skia Ganesh D3D12 context created on adapter \"%s\"", name);
    return true;
}

bool D3D12GrContext::isDeviceLost() const {
    if (!context_) {
        return false;
    }
    if (context_->isDeviceLost()) {
        return true;
    }
    if (device_) {
        const HRESULT reason = device_->GetDeviceRemovedReason();
        return reason != S_OK;
    }
    return false;
}

void D3D12GrContext::releaseContext(bool waitIdle) {
    if (!context_) {
        return;
    }
    if (waitIdle) {
        context_->flush();
        context_->submit(GrSyncCpu::kYes);
    }
    context_->releaseResourcesAndAbandonContext();
    context_.reset();
    XGU_LOG_INFO("Skia Ganesh D3D12 context released");
}

void D3D12GrContext::abandonContext() {
    if (!context_) {
        return;
    }
    context_->abandonContext();
    context_.reset();
    XGU_LOG_WARNING("Skia Ganesh D3D12 context abandoned (device lost)");
}

void D3D12GrContext::reset() {
    releaseContext(false);
    queue_.Reset();
    device_.Reset();
}

} // namespace xgu::render
