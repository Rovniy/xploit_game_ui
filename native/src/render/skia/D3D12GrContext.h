#pragma once

#include <include/core/SkRefCnt.h>
#include <include/gpu/ganesh/GrDirectContext.h>

#include <d3d12.h>
#include <wrl/client.h>

namespace xgu::render {

// Owns the Skia Ganesh D3D12 context created on top of a device/queue pair the
// host controls (Unity's device or the standalone host's device).
//
// All member functions except device()/queue() must run on the single thread
// that submits Skia work (Unity's submission thread or the host main thread).
class D3D12GrContext {
public:
    D3D12GrContext() = default;
    ~D3D12GrContext();

    D3D12GrContext(const D3D12GrContext&) = delete;
    D3D12GrContext& operator=(const D3D12GrContext&) = delete;

    // Stores the device/queue (AddRef). Any thread; call before ensureContext().
    void setDevice(ID3D12Device* device, ID3D12CommandQueue* queue);
    bool hasDevice() const { return device_ != nullptr && queue_ != nullptr; }

    // Lazily creates the GrDirectContext. Submission thread only.
    bool ensureContext();
    GrDirectContext* context() const { return context_.get(); }
    bool isDeviceLost() const;

    // Waits for the GPU (optional) and drops the Skia context. Keeps the device.
    void releaseContext(bool waitIdle);
    // Drops the context without touching the GPU (device removed).
    void abandonContext();
    // Releases the context and the device references.
    void reset();

    ID3D12Device* device() const { return device_.Get(); }
    ID3D12CommandQueue* queue() const { return queue_.Get(); }

private:
    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue_;
    sk_sp<GrDirectContext> context_;
};

} // namespace xgu::render
