#pragma once

#include "BackdropImageGenerator.g.h"

namespace winrt::WUILiquidGlassDemo::implementation
{
    struct BackdropImageGenerator : BackdropImageGeneratorT<BackdropImageGenerator>
    {
        BackdropImageGenerator();
        ~BackdropImageGenerator();

        Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice D3DDevice() const;
        void D3DDevice(Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice const& value);
        void SetBackdropImageSourceVisual(Windows::UI::Composition::Visual const& visual);
        void ClearBackdropImageSource();
        Windows::Graphics::Capture::Direct3D11CaptureFrame LastFrame() const;

        winrt::event_token FrameUpdated(
            Windows::Foundation::TypedEventHandler<
                WUILiquidGlassDemo::BackdropImageGenerator,
                Windows::Graphics::Capture::Direct3D11CaptureFrame> const& handler);
        void FrameUpdated(winrt::event_token const& token) noexcept;

    private:
        void OnFrameArrived(Windows::Graphics::Capture::Direct3D11CaptureFramePool const& sender);

        static constexpr Windows::Graphics::DirectX::DirectXPixelFormat kCapturePixelFormat =
            Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized;
        static constexpr int32_t kFrameBufferCount = 2;

        mutable std::mutex m_mutex;
        Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice m_d3dDevice{ nullptr };
        Windows::UI::Composition::Visual m_sourceVisual{ nullptr };
        Windows::Graphics::Capture::GraphicsCaptureItem m_captureItem{ nullptr };
        Windows::Graphics::Capture::Direct3D11CaptureFramePool m_framePool{ nullptr };
        Windows::Graphics::Capture::GraphicsCaptureSession m_captureSession{ nullptr };
        Windows::Graphics::Capture::Direct3D11CaptureFrame m_lastFrame{ nullptr };
        Windows::Graphics::SizeInt32 m_lastSize{};
        Windows::Graphics::Capture::Direct3D11CaptureFramePool::FrameArrived_revoker m_frameArrivedRevoker{};
        uint64_t m_captureGeneration{};
        winrt::event<
            Windows::Foundation::TypedEventHandler<
                WUILiquidGlassDemo::BackdropImageGenerator,
                Windows::Graphics::Capture::Direct3D11CaptureFrame>> m_frameUpdated;
    };
}

namespace winrt::WUILiquidGlassDemo::factory_implementation
{
    struct BackdropImageGenerator :
        BackdropImageGeneratorT<BackdropImageGenerator, implementation::BackdropImageGenerator>
    {
    };
}
