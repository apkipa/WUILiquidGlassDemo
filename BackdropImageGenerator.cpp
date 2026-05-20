#include "pch.h"
#include "BackdropImageGenerator.h"
#if __has_include("BackdropImageGenerator.g.cpp")
#include "BackdropImageGenerator.g.cpp"
#endif

#include <Tenkai.hpp>

#include <d3d11_4.h>
#include <dxgi1_2.h>
#include <windows.graphics.directx.direct3d11.interop.h>

#include <winrt/Windows.Foundation.Metadata.h>

using namespace winrt;

namespace winrt::WUILiquidGlassDemo::implementation
{
    namespace
    {
        namespace wf = winrt::Windows::Foundation;
        namespace wfm = winrt::Windows::Foundation::Metadata;
        namespace wg = winrt::Windows::Graphics;
        namespace wgc = winrt::Windows::Graphics::Capture;
        namespace wgd = winrt::Windows::Graphics::DirectX;
        namespace wgd11 = winrt::Windows::Graphics::DirectX::Direct3D11;
        namespace wuc = winrt::Windows::UI::Composition;

        constexpr std::wstring_view kGraphicsCaptureItemRuntimeClassName =
            L"Windows.Graphics.Capture.GraphicsCaptureItem";
        constexpr std::wstring_view kCreateFromVisualMethodName = L"CreateFromVisual";

        wgd11::IDirect3DDevice WrapDxgiDevice(winrt::com_ptr<IDXGIDevice> const& dxgiDevice)
        {
            com_ptr<::IInspectable> inspectableDevice;
            check_hresult(CreateDirect3D11DeviceFromDXGIDevice(
                dxgiDevice.get(),
                inspectableDevice.put()));
            return inspectableDevice.as<wgd11::IDirect3DDevice>();
        }

        void CloseCaptureResources(
            wgc::Direct3D11CaptureFramePool::FrameArrived_revoker& frameArrivedRevoker,
            wgc::GraphicsCaptureSession& captureSession,
            wgc::Direct3D11CaptureFramePool& framePool)
        {
            frameArrivedRevoker.revoke();

            if (captureSession)
            {
                captureSession.Close();
                captureSession = nullptr;
            }

            if (framePool)
            {
                framePool.Close();
                framePool = nullptr;
            }
        }

    }

    BackdropImageGenerator::BackdropImageGenerator() = default;

    BackdropImageGenerator::~BackdropImageGenerator()
    {
        ClearBackdropImageSource();
    }

    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice BackdropImageGenerator::D3DDevice() const
    {
        std::scoped_lock lock(m_mutex);
        return m_d3dDevice;
    }

    void BackdropImageGenerator::D3DDevice(
        winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice const& value)
    {
        wgc::Direct3D11CaptureFramePool::FrameArrived_revoker frameArrivedRevoker{};
        wgc::GraphicsCaptureSession captureSession{ nullptr };
        wgc::Direct3D11CaptureFramePool framePool{ nullptr };

        {
            std::scoped_lock lock(m_mutex);
            ++m_captureGeneration;
            frameArrivedRevoker = std::move(m_frameArrivedRevoker);
            captureSession = std::move(m_captureSession);
            framePool = std::move(m_framePool);
            m_captureItem = nullptr;
            m_lastFrame = nullptr;
            m_lastSize = {};
            m_d3dDevice = value;
        }

        CloseCaptureResources(frameArrivedRevoker, captureSession, framePool);
    }

    void BackdropImageGenerator::SetBackdropImageSourceVisual(
        winrt::Windows::UI::Composition::Visual const& visual)
    {
        if (!visual)
        {
            throw hresult_invalid_argument(L"visual must not be null.");
        }

        if (!wgc::GraphicsCaptureSession::IsSupported())
        {
            throw hresult_not_implemented(L"Windows.Graphics.Capture is not supported on this system.");
        }

        if (!wfm::ApiInformation::IsMethodPresent(
            kGraphicsCaptureItemRuntimeClassName.data(),
            kCreateFromVisualMethodName.data()))
        {
            throw hresult_not_implemented(
                L"GraphicsCaptureItem.CreateFromVisual requires Windows 10, version 1809 or later.");
        }

        wgd11::IDirect3DDevice d3dDevice{ nullptr };
        uint64_t expectedGeneration{};
        {
            std::scoped_lock lock(m_mutex);
            d3dDevice = m_d3dDevice;
            if (!d3dDevice)
            {
                throw hresult_illegal_method_call(L"D3DDevice must be set before starting capture.");
            }

            expectedGeneration = m_captureGeneration;
        }

        auto captureItem = wgc::GraphicsCaptureItem::CreateFromVisual(visual);
        if (!captureItem)
        {
            throw hresult_error(E_FAIL, L"Failed to create GraphicsCaptureItem from the supplied Visual.");
        }

        auto initialSize = captureItem.Size();
        if (initialSize.Width <= 0 || initialSize.Height <= 0)
        {
            throw hresult_error(E_INVALIDARG, L"The supplied Visual has an invalid capture size.");
        }

        auto framePool = wgc::Direct3D11CaptureFramePool::Create(
            d3dDevice,
            kCapturePixelFormat,
            kFrameBufferCount,
            initialSize);
        auto captureSession = framePool.CreateCaptureSession(captureItem);

        auto weakThis = get_weak();
        auto frameArrivedRevoker = framePool.FrameArrived(auto_revoke, [weakThis](
            wgc::Direct3D11CaptureFramePool const& sender,
            wf::IInspectable const&)
            {
                if (auto self = weakThis.get())
                {
                    self->OnFrameArrived(sender);
                }
            });

        try
        {
            captureSession.StartCapture();
        }
        catch (...)
        {
            CloseCaptureResources(frameArrivedRevoker, captureSession, framePool);
            throw;
        }

        wgc::GraphicsCaptureItem oldCaptureItem{ nullptr };
        wgc::Direct3D11CaptureFramePool::FrameArrived_revoker oldFrameArrivedRevoker{};
        wgc::GraphicsCaptureSession oldCaptureSession{ nullptr };
        wgc::Direct3D11CaptureFramePool oldFramePool{ nullptr };
        bool canceled = false;
        {
            std::scoped_lock lock(m_mutex);
            canceled =
                expectedGeneration != m_captureGeneration ||
                m_d3dDevice != d3dDevice;

            if (!canceled)
            {
                ++m_captureGeneration;
                oldCaptureItem = std::move(m_captureItem);
                oldFrameArrivedRevoker = std::move(m_frameArrivedRevoker);
                oldCaptureSession = std::move(m_captureSession);
                oldFramePool = std::move(m_framePool);
                m_captureItem = std::move(captureItem);
                m_framePool = framePool;
                m_captureSession = captureSession;
                m_lastFrame = nullptr;
                m_lastSize = initialSize;
                m_frameArrivedRevoker = std::move(frameArrivedRevoker);
            }
        }

        if (canceled)
        {
            CloseCaptureResources(frameArrivedRevoker, captureSession, framePool);
            return;
        }

        CloseCaptureResources(oldFrameArrivedRevoker, oldCaptureSession, oldFramePool);
    }

    void BackdropImageGenerator::ClearBackdropImageSource()
    {
        wgc::Direct3D11CaptureFramePool::FrameArrived_revoker frameArrivedRevoker{};
        wgc::GraphicsCaptureSession captureSession{ nullptr };
        wgc::Direct3D11CaptureFramePool framePool{ nullptr };

        {
            std::scoped_lock lock(m_mutex);
            ++m_captureGeneration;
            frameArrivedRevoker = std::move(m_frameArrivedRevoker);
            captureSession = std::move(m_captureSession);
            framePool = std::move(m_framePool);
            m_captureItem = nullptr;
            m_lastFrame = nullptr;
            m_lastSize = {};
        }

        CloseCaptureResources(frameArrivedRevoker, captureSession, framePool);
    }

    winrt::Windows::Graphics::Capture::Direct3D11CaptureFrame BackdropImageGenerator::LastFrame() const
    {
        std::scoped_lock lock(m_mutex);
        return m_lastFrame;
    }

    winrt::event_token BackdropImageGenerator::FrameUpdated(
        winrt::Windows::Foundation::TypedEventHandler<
            WUILiquidGlassDemo::BackdropImageGenerator,
            winrt::Windows::Graphics::Capture::Direct3D11CaptureFrame> const& handler)
    {
        return m_frameUpdated.add(handler);
    }

    void BackdropImageGenerator::FrameUpdated(winrt::event_token const& token) noexcept
    {
        m_frameUpdated.remove(token);
    }

    void BackdropImageGenerator::OnFrameArrived(
        winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool const& sender)
    {
        wgc::Direct3D11CaptureFrame frame{ nullptr };
        while (auto nextFrame = sender.TryGetNextFrame())
        {
            frame = std::move(nextFrame);
        }

        if (!frame)
        {
            return;
        }

        auto contentSize = frame.ContentSize();
        if (contentSize.Width <= 0 || contentSize.Height <= 0)
        {
            return;
        }

        bool recreateRequired = false;
        wgd11::IDirect3DDevice d3dDevice{ nullptr };
        wgc::Direct3D11CaptureFrame deliveredFrame{ nullptr };
        {
            std::scoped_lock lock(m_mutex);
            if (sender != m_framePool)
            {
                return;
            }

            recreateRequired =
                contentSize.Width != m_lastSize.Width ||
                contentSize.Height != m_lastSize.Height;

            if (recreateRequired)
            {
                m_lastSize = contentSize;
            }

            m_lastFrame = frame;
            deliveredFrame = frame;

            if (recreateRequired)
            {
                d3dDevice = m_d3dDevice;
            }
        }

        auto recreateFramePool = [&]()
        {
            if (!recreateRequired)
            {
                return;
            }

            recreateRequired = false;

            {
                std::scoped_lock lock(m_mutex);
                if (sender != m_framePool)
                {
                    return;
                }

                d3dDevice = m_d3dDevice;
                if (!d3dDevice)
                {
                    return;
                }
            }

            frame = nullptr;
            sender.Recreate(d3dDevice, kCapturePixelFormat, kFrameBufferCount, contentSize);
        };

        auto recreateOnExit = tenkai::cpp_utils::scope_exit([&]() noexcept
            {
                try
                {
                    recreateFramePool();
                }
                catch (...)
                {
                }
            });

        m_frameUpdated(*this, deliveredFrame);
        recreateFramePool();
    }
}
