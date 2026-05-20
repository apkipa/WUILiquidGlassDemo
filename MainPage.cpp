#include "pch.h"
#include "MainPage.h"
#include "MainPage.g.cpp"

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Graphics::Effects;
using namespace winrt::Windows::UI::Composition;
using namespace winrt::Windows::UI::Xaml;
using namespace winrt::Windows::UI::Xaml::Media;
using namespace winrt::Windows::UI::Xaml::Hosting;

namespace winrt::Tenkai {
    using UI::Xaml::Window;
}

namespace {
    namespace wgd = winrt::Windows::Graphics::DirectX;
    namespace wgd11 = winrt::Windows::Graphics::DirectX::Direct3D11;
    namespace wfn = winrt::Windows::Foundation::Numerics;
    namespace wuc = winrt::Windows::UI::Composition;

    constexpr GUID kGaussianBlurEffectId{
        0x1feb6d69, 0x2fe6, 0x4ac9, { 0x8c, 0x58, 0x1d, 0x7f, 0x93, 0xe7, 0xa6, 0xa5 }
    };

    constexpr char kOverlayVertexShader[] = R"(
cbuffer OverlayConstants : register(b0)
{
    float2 SurfaceSize;
    float2 RectOrigin;
    float2 RectSize;
    float2 FrameContentScale;
    float2 FrameTextureSize;
    float BlurRadius;
    float BorderThickness;
    float4 Reserved;
};

struct VSOutput
{
    float4 Position : SV_Position;
    float2 LocalPosition : TEXCOORD0;
};

VSOutput main(uint vertexId : SV_VertexID)
{
    static const float2 corners[4] =
    {
        float2(0.0f, 0.0f),
        float2(1.0f, 0.0f),
        float2(0.0f, 1.0f),
        float2(1.0f, 1.0f)
    };

    const float2 pixelPosition = RectOrigin + corners[vertexId] * RectSize;
    const float2 clipPosition = float2(
        pixelPosition.x / SurfaceSize.x * 2.0f - 1.0f,
        1.0f - pixelPosition.y / SurfaceSize.y * 2.0f);

    VSOutput output;
    output.Position = float4(clipPosition, 0.0f, 1.0f);
    output.LocalPosition = corners[vertexId] * RectSize;
    return output;
}
)";

    constexpr char kOverlayPixelShader[] = R"(
Texture2D FrameTexture : register(t0);
SamplerState FrameSampler : register(s0);

cbuffer OverlayConstants : register(b0)
{
    float2 SurfaceSize;
    float2 RectOrigin;
    float2 RectSize;
    float2 FrameContentScale;
    float2 FrameTextureSize;
    float BlurRadius;
    float BorderThickness;
    float4 Reserved;
};

struct VSOutput
{
    float4 Position : SV_Position;
    float2 LocalPosition : TEXCOORD0;
};

float4 main(VSOutput input) : SV_Target
{
    const float2 samplePixel = RectOrigin + input.LocalPosition + 0.5f.xx;
    const float2 sourceUv = samplePixel / SurfaceSize * FrameContentScale;
    const float2 texelSize = 1.0f.xx / FrameTextureSize;
    const float2 blurStep = texelSize * BlurRadius;

    float3 blurred =
        FrameTexture.Sample(FrameSampler, sourceUv + blurStep * float2(-1.0f, -1.0f)).rgb * 0.0625f +
        FrameTexture.Sample(FrameSampler, sourceUv + blurStep * float2( 0.0f, -1.0f)).rgb * 0.1250f +
        FrameTexture.Sample(FrameSampler, sourceUv + blurStep * float2( 1.0f, -1.0f)).rgb * 0.0625f +
        FrameTexture.Sample(FrameSampler, sourceUv + blurStep * float2(-1.0f,  0.0f)).rgb * 0.1250f +
        FrameTexture.Sample(FrameSampler, sourceUv).rgb                                     * 0.2500f +
        FrameTexture.Sample(FrameSampler, sourceUv + blurStep * float2( 1.0f,  0.0f)).rgb * 0.1250f +
        FrameTexture.Sample(FrameSampler, sourceUv + blurStep * float2(-1.0f,  1.0f)).rgb * 0.0625f +
        FrameTexture.Sample(FrameSampler, sourceUv + blurStep * float2( 0.0f,  1.0f)).rgb * 0.1250f +
        FrameTexture.Sample(FrameSampler, sourceUv + blurStep * float2( 1.0f,  1.0f)).rgb * 0.0625f;

    const float2 edgeDistance = min(input.LocalPosition, RectSize - input.LocalPosition);
    if (min(edgeDistance.x, edgeDistance.y) <= BorderThickness)
    {
        return float4(1.0f, 0.0f, 0.0f, 1.0f);
    }

    return float4(blurred, 1.0f);
}
)";

    struct GaussianBlurEffect :
        winrt::implements<
            GaussianBlurEffect,
            IGraphicsEffect,
            IGraphicsEffectSource,
            ABI::Windows::Graphics::Effects::IGraphicsEffectD2D1Interop> {
        GaussianBlurEffect(CompositionEffectSourceParameter const& source, float blurAmount) :
            m_source(source),
            m_blurAmount(blurAmount) {}

        hstring Name() const {
            return m_name;
        }

        void Name(hstring const& value) {
            m_name = value;
        }

        HRESULT __stdcall GetEffectId(GUID* effectId) noexcept final {
            if (!effectId) {
                return E_POINTER;
            }

            *effectId = kGaussianBlurEffectId;
            return S_OK;
        }

        HRESULT __stdcall GetNamedPropertyMapping(
            LPCWSTR name,
            UINT* index,
            ABI::Windows::Graphics::Effects::GRAPHICS_EFFECT_PROPERTY_MAPPING* mapping) noexcept final {
            if (!name || !index || !mapping) {
                return E_POINTER;
            }

            if (wcscmp(name, L"BlurAmount") == 0) {
                *index = D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION;
            }
            else if (wcscmp(name, L"Optimization") == 0) {
                *index = D2D1_GAUSSIANBLUR_PROP_OPTIMIZATION;
            }
            else if (wcscmp(name, L"BorderMode") == 0) {
                *index = D2D1_GAUSSIANBLUR_PROP_BORDER_MODE;
            }
            else {
                return E_INVALIDARG;
            }

            *mapping = ABI::Windows::Graphics::Effects::GRAPHICS_EFFECT_PROPERTY_MAPPING_DIRECT;
            return S_OK;
        }

        HRESULT __stdcall GetPropertyCount(UINT* count) noexcept final {
            if (!count) {
                return E_POINTER;
            }

            *count = 3;
            return S_OK;
        }

        HRESULT __stdcall GetProperty(
            UINT index,
            ABI::Windows::Foundation::IPropertyValue** value) noexcept final {
            if (!value) {
                return E_POINTER;
            }

            *value = nullptr;

            try {
                winrt::Windows::Foundation::IPropertyValue propertyValue{ nullptr };

                switch (index) {
                case D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION:
                    propertyValue = PropertyValue::CreateSingle(m_blurAmount).as<winrt::Windows::Foundation::IPropertyValue>();
                    break;
                case D2D1_GAUSSIANBLUR_PROP_OPTIMIZATION:
                    propertyValue = PropertyValue::CreateUInt32(
                        static_cast<uint32_t>(D2D1_GAUSSIANBLUR_OPTIMIZATION_BALANCED)).as<winrt::Windows::Foundation::IPropertyValue>();
                    break;
                case D2D1_GAUSSIANBLUR_PROP_BORDER_MODE:
                    propertyValue = PropertyValue::CreateUInt32(
                        static_cast<uint32_t>(D2D1_BORDER_MODE_HARD)).as<winrt::Windows::Foundation::IPropertyValue>();
                    break;
                default:
                    return E_INVALIDARG;
                }

                *value = reinterpret_cast<ABI::Windows::Foundation::IPropertyValue*>(detach_abi(propertyValue));
                return S_OK;
            }
            catch (...) {
                return to_hresult();
            }
        }

        HRESULT __stdcall GetSource(
            UINT index,
            ABI::Windows::Graphics::Effects::IGraphicsEffectSource** source) noexcept final {
            if (!source) {
                return E_POINTER;
            }

            if (index != 0) {
                *source = nullptr;
                return E_INVALIDARG;
            }

            try {
                *source = reinterpret_cast<ABI::Windows::Graphics::Effects::IGraphicsEffectSource*>(
                    detach_abi(m_source.as<IGraphicsEffectSource>()));
                return S_OK;
            }
            catch (...) {
                *source = nullptr;
                return to_hresult();
            }
        }

        HRESULT __stdcall GetSourceCount(UINT* count) noexcept final {
            if (!count) {
                return E_POINTER;
            }

            *count = 1;
            return S_OK;
        }

    private:
        hstring m_name{ L"GaussianBlurEffect" };
        CompositionEffectSourceParameter m_source{ L"Backdrop" };
        float m_blurAmount{};
    };

    struct OverlayConstants final
    {
        wfn::float2 SurfaceSize{};
        wfn::float2 RectOrigin{};
        wfn::float2 RectSize{};
        wfn::float2 FrameContentScale{};
        wfn::float2 FrameTextureSize{};
        float BlurRadius{};
        float BorderThickness{};
        wfn::float4 Reserved{};
    };

    winrt::com_ptr<ID3DBlob> CompileShader(char const* source, char const* target)
    {
        UINT compileFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
        compileFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

        winrt::com_ptr<ID3DBlob> shaderBlob;
        winrt::com_ptr<ID3DBlob> errorBlob;
        check_hresult(D3DCompile(
            source,
            strlen(source),
            nullptr,
            nullptr,
            nullptr,
            "main",
            target,
            compileFlags,
            0,
            shaderBlob.put(),
            errorBlob.put()));
        return shaderBlob;
    }

    winrt::com_ptr<ID3D11Device5> CreateD3D11Device(winrt::com_ptr<ID3D11DeviceContext4>& deviceContext)
    {
        auto const createDevice = [&](
            D3D_DRIVER_TYPE driverType,
            winrt::com_ptr<ID3D11Device>& device,
            winrt::com_ptr<ID3D11DeviceContext>& context)
        {
            static constexpr D3D_FEATURE_LEVEL featureLevels[]{
                D3D_FEATURE_LEVEL_11_1,
                D3D_FEATURE_LEVEL_11_0,
                D3D_FEATURE_LEVEL_10_1,
                D3D_FEATURE_LEVEL_10_0,
            };

            UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
            D3D_FEATURE_LEVEL featureLevel{};
            return D3D11CreateDevice(
                nullptr,
                driverType,
                nullptr,
                creationFlags,
                featureLevels,
                ARRAYSIZE(featureLevels),
                D3D11_SDK_VERSION,
                device.put(),
                &featureLevel,
                context.put());
        };

        winrt::com_ptr<ID3D11Device> baseDevice;
        winrt::com_ptr<ID3D11DeviceContext> baseContext;
        auto hr = createDevice(D3D_DRIVER_TYPE_HARDWARE, baseDevice, baseContext);
        if (FAILED(hr))
        {
            check_hresult(createDevice(D3D_DRIVER_TYPE_WARP, baseDevice, baseContext));
        }

        winrt::com_ptr<ID3D11Device5> device;
        baseDevice.as(device);
        baseContext.as(deviceContext);
        return device;
    }

    winrt::com_ptr<ID2D1Device1> CreateD2DDevice(winrt::com_ptr<IDXGIDevice> const& dxgiDevice)
    {
        D2D1_FACTORY_OPTIONS factoryOptions{};
#if defined(_DEBUG)
        factoryOptions.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#endif

        winrt::com_ptr<ID2D1Factory2> d2dFactory;
        check_hresult(D2D1CreateFactory(
            D2D1_FACTORY_TYPE_SINGLE_THREADED,
            __uuidof(ID2D1Factory2),
            &factoryOptions,
            d2dFactory.put_void()));

        winrt::com_ptr<ID2D1Device1> d2dDevice;
        check_hresult(d2dFactory->CreateDevice(dxgiDevice.get(), d2dDevice.put()));
        return d2dDevice;
    }

    wgd11::IDirect3DDevice WrapDxgiDevice(winrt::com_ptr<IDXGIDevice> const& dxgiDevice)
    {
        winrt::com_ptr<::IInspectable> inspectableDevice;
        check_hresult(CreateDirect3D11DeviceFromDXGIDevice(
            dxgiDevice.get(),
            inspectableDevice.put()));
        return inspectableDevice.as<wgd11::IDirect3DDevice>();
    }

    winrt::com_ptr<ID3D11Texture2D> GetTextureFromDirect3DSurface(wgd11::IDirect3DSurface const& surface)
    {
        auto access = surface.as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
        winrt::com_ptr<ID3D11Texture2D> texture;
        check_hresult(access->GetInterface(__uuidof(ID3D11Texture2D), texture.put_void()));
        return texture;
    }

}

namespace winrt::WUILiquidGlassDemo::implementation {
    MainPage::~MainPage()
    {
        CleanupOverlayRenderer();
    }

    int32_t MainPage::MyProperty()
    {
        throw hresult_not_implemented();
    }

    void MainPage::MyProperty(int32_t /* value */)
    {
        throw hresult_not_implemented();
    }

    void MainPage::ClickHandler(IInspectable const&, RoutedEventArgs const&)
    {
        myButton().Content(box_value(L"Clicked"));
    }

    void MainPage::MainPageLoaded(IInspectable const&, RoutedEventArgs const&)
    {
        InitializeOverlayRenderer();
    }

    void MainPage::MainPageUnloaded(IInspectable const&, RoutedEventArgs const&)
    {
        CleanupOverlayRenderer();
    }

    void MainPage::RenderSurfaceHostSizeChanged(IInspectable const&, SizeChangedEventArgs const&)
    {
        if (!m_isOverlayInitialized)
        {
            return;
        }

        UpdateOverlaySurfaceSize();
    }

    void MainPage::OverlaySliderValueChanged(
        IInspectable const&,
        winrt::Windows::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const&)
    {
        if (!m_isOverlayInitialized)
        {
            return;
        }

        ScheduleOverlayRender();
    }

    void MainPage::InitializeOverlayRenderer()
    {
        if (m_isOverlayInitialized)
        {
            UpdateOverlaySurfaceSize();
            ScheduleOverlayRender();
            return;
        }

        EnsureD3DResources();
        EnsureOverlaySurface();
        EnsureBackdropCaptureSource();
        UpdateBackdropCaptureSourceSize();

        m_backdropImageGenerator = WUILiquidGlassDemo::BackdropImageGenerator();
        m_backdropImageGenerator.D3DDevice(m_winrtD3DDevice);

        auto weakThis = get_weak();
        m_frameUpdatedRevoker = m_backdropImageGenerator.FrameUpdated(auto_revoke, [weakThis](
            WUILiquidGlassDemo::BackdropImageGenerator const&,
            Windows::Graphics::Capture::Direct3D11CaptureFrame const&)
            {
                if (auto self = weakThis.get())
                {
                    self->ScheduleOverlayRender();
                }
            });

        m_backdropImageGenerator.SetBackdropImageSourceVisual(m_backdropCaptureVisual);
        m_isOverlayInitialized = true;
        UpdateOverlaySurfaceSize();
        ScheduleOverlayRender();
    }

    void MainPage::CleanupOverlayRenderer() noexcept
    {
        try
        {
            if (m_renderRequestId != 0)
            {
                WUILiquidGlassDemo::AnimationFrameScheduler::CancelAnimationFrame(m_renderRequestId);
                m_renderRequestId = 0;
            }

            m_frameUpdatedRevoker.revoke();

            if (m_backdropImageGenerator)
            {
                m_backdropImageGenerator.ClearBackdropImageSource();
                m_backdropImageGenerator = nullptr;
            }

            try
            {
                if (RenderSurfaceHost())
                {
                    ElementCompositionPreview::SetElementChildVisual(RenderSurfaceHost(), nullptr);
                }
            }
            catch (...)
            {
            }

            m_surfaceVisual = nullptr;
            m_surfaceBrush = nullptr;
            m_backdropCaptureVisual = nullptr;
            m_backdropCaptureBrush = nullptr;
            m_backdropCaptureSurface = nullptr;
            m_drawingSurface = nullptr;
            m_compositionGraphicsDevice = nullptr;
            m_compositor = nullptr;
            m_surfaceSize = {};
            m_winrtD3DDevice = nullptr;
            m_d3dContext = nullptr;
            m_d3dDevice = nullptr;
            m_d2dDevice = nullptr;
            m_vertexShader = nullptr;
            m_pixelShader = nullptr;
            m_constantBuffer = nullptr;
            m_rasterizerState = nullptr;
            m_samplerState = nullptr;
            m_isOverlayInitialized = false;
        }
        catch (...)
        {
        }
    }

    void MainPage::EnsureD3DResources()
    {
        if (m_d3dDevice)
        {
            return;
        }

        m_d3dDevice = CreateD3D11Device(m_d3dContext);
        auto dxgiDevice = m_d3dDevice.as<IDXGIDevice>();
        m_d2dDevice = CreateD2DDevice(dxgiDevice);
        m_winrtD3DDevice = WrapDxgiDevice(dxgiDevice);

        auto vertexShaderBlob = CompileShader(kOverlayVertexShader, "vs_5_0");
        auto pixelShaderBlob = CompileShader(kOverlayPixelShader, "ps_5_0");

        check_hresult(m_d3dDevice->CreateVertexShader(
            vertexShaderBlob->GetBufferPointer(),
            vertexShaderBlob->GetBufferSize(),
            nullptr,
            m_vertexShader.put()));
        check_hresult(m_d3dDevice->CreatePixelShader(
            pixelShaderBlob->GetBufferPointer(),
            pixelShaderBlob->GetBufferSize(),
            nullptr,
            m_pixelShader.put()));

        D3D11_BUFFER_DESC constantBufferDesc{};
        constantBufferDesc.ByteWidth = sizeof(OverlayConstants);
        constantBufferDesc.Usage = D3D11_USAGE_DEFAULT;
        constantBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        check_hresult(m_d3dDevice->CreateBuffer(&constantBufferDesc, nullptr, m_constantBuffer.put()));

        D3D11_RASTERIZER_DESC rasterizerDesc{};
        rasterizerDesc.FillMode = D3D11_FILL_SOLID;
        rasterizerDesc.CullMode = D3D11_CULL_NONE;
        rasterizerDesc.DepthClipEnable = TRUE;
        check_hresult(m_d3dDevice->CreateRasterizerState(&rasterizerDesc, m_rasterizerState.put()));

        D3D11_SAMPLER_DESC samplerDesc{};
        samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
        check_hresult(m_d3dDevice->CreateSamplerState(&samplerDesc, m_samplerState.put()));
    }

    void MainPage::EnsureOverlaySurface()
    {
        if (m_surfaceVisual)
        {
            return;
        }

        m_compositor = ElementCompositionPreview::GetElementVisual(RenderSurfaceHost()).Compositor();

        auto compositorInterop = m_compositor.as<ABI::Windows::UI::Composition::ICompositorInterop>();
        winrt::com_ptr<ABI::Windows::UI::Composition::ICompositionGraphicsDevice> interopGraphicsDevice;
        check_hresult(compositorInterop->CreateGraphicsDevice(
            m_d2dDevice.get(),
            interopGraphicsDevice.put()));

        m_compositionGraphicsDevice = interopGraphicsDevice.as<CompositionGraphicsDevice>();
        m_drawingSurface = m_compositionGraphicsDevice.as<wuc::ICompositionGraphicsDevice2>().CreateDrawingSurface2(
            { 1, 1 },
            wgd::DirectXPixelFormat::B8G8R8A8UIntNormalized,
            wgd::DirectXAlphaMode::Premultiplied);

        m_surfaceBrush = m_compositor.CreateSurfaceBrush(m_drawingSurface);
        m_surfaceBrush.Stretch(CompositionStretch::Fill);
        m_surfaceVisual = m_compositor.CreateSpriteVisual();
        m_surfaceVisual.Brush(m_surfaceBrush);

        ElementCompositionPreview::SetElementChildVisual(RenderSurfaceHost(), m_surfaceVisual);
    }

    void MainPage::UpdateOverlaySurfaceSize()
    {
        EnsureOverlaySurface();

        auto const widthDip = std::max(1.0f, static_cast<float>(RenderSurfaceHost().ActualWidth()));
        auto const heightDip = std::max(1.0f, static_cast<float>(RenderSurfaceHost().ActualHeight()));
        auto const scale = GetOverlayRasterizationScale();
        auto const width = std::max(1, static_cast<int32_t>(std::lround(widthDip * scale)));
        auto const height = std::max(1, static_cast<int32_t>(std::lround(heightDip * scale)));

        m_surfaceVisual.Size({ widthDip, heightDip });
        UpdateBackdropCaptureSourceSize();

        if (m_surfaceSize.Width == width && m_surfaceSize.Height == height)
        {
            UpdateSliderRanges();
            return;
        }

        auto surfaceInterop = m_drawingSurface.as<ABI::Windows::UI::Composition::ICompositionDrawingSurfaceInterop>();
        check_hresult(surfaceInterop->Resize({ width, height }));

        m_surfaceSize = { width, height };
        UpdateSliderRanges();
        ScheduleOverlayRender();
    }

    void MainPage::EnsureBackdropCaptureSource()
    {
        if (m_backdropCaptureVisual)
        {
            return;
        }

        auto sourceVisual = ElementCompositionPreview::GetElementVisual(BottomLayoutRoot());
        m_backdropCaptureSurface = m_compositor.CreateVisualSurface();
        m_backdropCaptureSurface.SourceVisual(sourceVisual);
        m_backdropCaptureBrush = m_compositor.CreateSurfaceBrush(m_backdropCaptureSurface);
        m_backdropCaptureVisual = m_compositor.CreateSpriteVisual();
        m_backdropCaptureVisual.Brush(m_backdropCaptureBrush);
    }

    void MainPage::UpdateBackdropCaptureSourceSize()
    {
        if (!m_backdropCaptureVisual || !m_backdropCaptureSurface)
        {
            return;
        }

        auto const width = std::max(1.0f, static_cast<float>(BottomLayoutRoot().ActualWidth()));
        auto const height = std::max(1.0f, static_cast<float>(BottomLayoutRoot().ActualHeight()));
        wfn::float2 const size{ width, height };
        m_backdropCaptureSurface.SourceOffset({ 0.0f, 0.0f });
        m_backdropCaptureSurface.SourceSize(size);
        m_backdropCaptureVisual.Size(size);
    }

    void MainPage::UpdateSliderRanges()
    {
        auto const maxX = std::max(0.0, RenderSurfaceHost().ActualWidth() - kOverlayRectWidth);
        auto const maxY = std::max(0.0, RenderSurfaceHost().ActualHeight() - kOverlayRectHeight);

        if (XOffsetSlider().Maximum() != maxX)
        {
            XOffsetSlider().Maximum(maxX);
        }
        if (YOffsetSlider().Maximum() != maxY)
        {
            YOffsetSlider().Maximum(maxY);
        }

        if (XOffsetSlider().Value() > maxX)
        {
            XOffsetSlider().Value(maxX);
        }
        if (YOffsetSlider().Value() > maxY)
        {
            YOffsetSlider().Value(maxY);
        }
    }

    void MainPage::ScheduleOverlayRender()
    {
        if (!m_isOverlayInitialized || !m_drawingSurface || m_renderRequestId != 0)
        {
            return;
        }

        auto weakThis = get_weak();
        m_renderRequestId = WUILiquidGlassDemo::AnimationFrameScheduler::RequestAnimationFrame([weakThis](double)
            {
                if (auto self = weakThis.get())
                {
                    self->m_renderRequestId = 0;
                    self->RenderOverlaySurface();
                }
            });
    }

    void MainPage::RenderOverlaySurface()
    {
        if (!m_drawingSurface)
        {
            return;
        }

        auto const widthDip = std::max(1.0f, static_cast<float>(RenderSurfaceHost().ActualWidth()));
        auto const heightDip = std::max(1.0f, static_cast<float>(RenderSurfaceHost().ActualHeight()));
        auto const scale = GetOverlayRasterizationScale();
        auto const width = std::max(1, static_cast<int32_t>(std::lround(widthDip * scale)));
        auto const height = std::max(1, static_cast<int32_t>(std::lround(heightDip * scale)));
        if (m_surfaceSize.Width != width || m_surfaceSize.Height != height)
        {
            auto surfaceInterop = m_drawingSurface.as<ABI::Windows::UI::Composition::ICompositionDrawingSurfaceInterop>();
            check_hresult(surfaceInterop->Resize({ width, height }));
            m_surfaceSize = { width, height };
            UpdateSliderRanges();
        }

        if (m_surfaceSize.Width <= 0 || m_surfaceSize.Height <= 0)
        {
            return;
        }

        auto surfaceInterop = m_drawingSurface.as<ABI::Windows::UI::Composition::ICompositionDrawingSurfaceInterop>();
        POINT updateOffset{};
        winrt::com_ptr<ID3D11Texture2D> surfaceTexture;
        check_hresult(surfaceInterop->BeginDraw(
            nullptr,
            __uuidof(ID3D11Texture2D),
            surfaceTexture.put_void(),
            &updateOffset));

        auto endDraw = tenkai::cpp_utils::scope_exit([&]() noexcept
            {
                try
                {
                    check_hresult(surfaceInterop->EndDraw());
                }
                catch (...)
                {
                }
            });

        winrt::com_ptr<ID3D11RenderTargetView> renderTargetView;
        check_hresult(m_d3dDevice->CreateRenderTargetView(
            surfaceTexture.get(),
            nullptr,
            renderTargetView.put()));

        auto const lastFrame = m_backdropImageGenerator ? m_backdropImageGenerator.LastFrame() : nullptr;
        if (!lastFrame)
        {
            constexpr float clearColor[]{ 0.0f, 0.0f, 0.0f, 0.0f };
            m_d3dContext->ClearRenderTargetView(renderTargetView.get(), clearColor);
            return;
        }

        auto frameTexture = GetTextureFromDirect3DSurface(lastFrame.Surface());
        D3D11_TEXTURE2D_DESC frameTextureDesc{};
        frameTexture->GetDesc(&frameTextureDesc);

        winrt::com_ptr<ID3D11ShaderResourceView> frameShaderResourceView;
        check_hresult(m_d3dDevice->CreateShaderResourceView(
            frameTexture.get(),
            nullptr,
            frameShaderResourceView.put()));

        auto const rectWidth = kOverlayRectWidth * scale;
        auto const rectHeight = kOverlayRectHeight * scale;
        auto const rectX = std::clamp(
            static_cast<float>(XOffsetSlider().Value()) * scale,
            0.0f,
            std::max(0.0f, static_cast<float>(m_surfaceSize.Width) - rectWidth));
        auto const rectY = std::clamp(
            static_cast<float>(YOffsetSlider().Value()) * scale,
            0.0f,
            std::max(0.0f, static_cast<float>(m_surfaceSize.Height) - rectHeight));

        OverlayConstants constants{};
        constants.SurfaceSize = { static_cast<float>(m_surfaceSize.Width), static_cast<float>(m_surfaceSize.Height) };
        constants.RectOrigin = { rectX, rectY };
        constants.RectSize = { rectWidth, rectHeight };
        constants.FrameContentScale = {
            static_cast<float>(lastFrame.ContentSize().Width) / static_cast<float>(frameTextureDesc.Width),
            static_cast<float>(lastFrame.ContentSize().Height) / static_cast<float>(frameTextureDesc.Height)
        };
        constants.FrameTextureSize = {
            static_cast<float>(frameTextureDesc.Width),
            static_cast<float>(frameTextureDesc.Height)
        };
        constants.BlurRadius = 2.0f;
        constants.BorderThickness = scale;

        D3D11_VIEWPORT viewport{};
        viewport.TopLeftX = static_cast<float>(updateOffset.x);
        viewport.TopLeftY = static_cast<float>(updateOffset.y);
        viewport.Width = static_cast<float>(m_surfaceSize.Width);
        viewport.Height = static_cast<float>(m_surfaceSize.Height);
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;

        ID3D11RenderTargetView* const renderTargets[] = { renderTargetView.get() };
        ID3D11Buffer* const constantBuffers[] = { m_constantBuffer.get() };
        ID3D11ShaderResourceView* const shaderResources[] = { frameShaderResourceView.get() };
        ID3D11SamplerState* const samplers[] = { m_samplerState.get() };
        ID3D11ShaderResourceView* const nullShaderResources[] = { nullptr };
        ID3D11Buffer* nullVertexBuffer = nullptr;
        UINT stride = 0;
        UINT offset = 0;
        constexpr float clearColor[]{ 0.0f, 0.0f, 0.0f, 0.0f };

        m_d3dContext->OMSetRenderTargets(1, renderTargets, nullptr);
        m_d3dContext->ClearRenderTargetView(renderTargetView.get(), clearColor);
        m_d3dContext->RSSetViewports(1, &viewport);
        m_d3dContext->RSSetState(m_rasterizerState.get());
        m_d3dContext->IASetInputLayout(nullptr);
        m_d3dContext->IASetVertexBuffers(0, 1, &nullVertexBuffer, &stride, &offset);
        m_d3dContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        m_d3dContext->UpdateSubresource(m_constantBuffer.get(), 0, nullptr, &constants, 0, 0);
        m_d3dContext->VSSetShader(m_vertexShader.get(), nullptr, 0);
        m_d3dContext->VSSetConstantBuffers(0, 1, constantBuffers);
        m_d3dContext->PSSetShader(m_pixelShader.get(), nullptr, 0);
        m_d3dContext->PSSetConstantBuffers(0, 1, constantBuffers);
        m_d3dContext->PSSetShaderResources(0, 1, shaderResources);
        m_d3dContext->PSSetSamplers(0, 1, samplers);
        m_d3dContext->Draw(4, 0);
        m_d3dContext->PSSetShaderResources(0, 1, nullShaderResources);
    }

    float MainPage::GetOverlayRasterizationScale()
    {
        if (auto xamlRoot = RenderSurfaceHost().XamlRoot())
        {
            return std::max(1.0f, static_cast<float>(xamlRoot.RasterizationScale()));
        }

        return 1.0f;
    }

    void MainPage::SwitchTitleBarButtonClick(IInspectable const&, RoutedEventArgs const&) {
        auto wnd = Tenkai::Window::GetCurrentMain();
        auto cur_is_extended = !wnd.ExtendsContentIntoTitleBar();
        wnd.ExtendsContentIntoTitleBar(cur_is_extended);
        auto tb = wnd.View().TitleBar();
        auto tb_ex = tb.try_as<Tenkai::UI::ViewManagement::ISupportsAdvancedCaptionCustomization>();
        if (cur_is_extended) {
            wnd.SetTitleBar(TopDragRectangle());
            if (tb_ex) {
                //tb_ex.ButtonShape({ 46, 40 });
            }
            TopElasticRightSpace().Width(GridLengthHelper::FromPixels(tb.RightInset() - 20));
        }
        else {
            wnd.SetTitleBar(nullptr);
            TopElasticRightSpace().Width(GridLengthHelper::FromPixels(0));
        }
    }
    void MainPage::SwitchBackgroundTransparencyButtonClick(IInspectable const&, RoutedEventArgs const&) {
        auto wnd = Tenkai::Window::GetCurrentMain();
        auto wv = wnd.View();
        auto cur_is_transparent = !wv.IsBackgroundTransparent();
        wv.IsBackgroundTransparent(cur_is_transparent);

        /*if (cur_is_transparent) {
            auto compositor = wnd.Compositor();
        #if 1
            auto cb = winrt::get_activation_factory<IActivationFactory>(xaml_typename<XamlCompositionBrushBase>().Name)
                .ActivateInstance<XamlCompositionBrushBase>();
            auto source = CompositionEffectSourceParameter(L"Backdrop");
            auto effect = winrt::make<GaussianBlurEffect>(source, 16.0f);
            auto effectFactory = wnd.Compositor().CreateEffectFactory(effect);
            auto effectBrush = effectFactory.CreateBrush();
        #if 1
            effectBrush.SetSourceParameter(L"Backdrop", wnd.Compositor().CreateBackdropBrush());
            cb.as<IXamlCompositionBrushBaseProtected>().CompositionBrush(effectBrush);
        #else
            auto brush = compositor.CreateHostBackdropBrush();
            cb.as<IXamlCompositionBrushBaseProtected>().CompositionBrush(brush);
        #endif
            BottomLayoutRoot().Background(cb);
            {
                auto cb = winrt::get_activation_factory<IActivationFactory>(xaml_typename<XamlCompositionBrushBase>().Name)
                    .ActivateInstance<XamlCompositionBrushBase>();
                auto cvs = compositor.CreateVisualSurface();
                cvs.SourceVisual(ElementCompositionPreview::GetElementVisual(BottomLayoutRoot()));
                auto brush = compositor.CreateSurfaceBrush(cvs);
                cb.as<IXamlCompositionBrushBaseProtected>().CompositionBrush(brush);
                BottomBorder().Background(cb);
            }
        #else
            auto ab = AcrylicBrush();
            ab.TintOpacity(0.0);
            ab.BackgroundSource(AcrylicBackgroundSource::Backdrop);
            BottomLayoutRoot().Background(ab);
        #endif
        }
        else {
            BottomLayoutRoot().Background(nullptr);
        }*/

        if (cur_is_transparent) {
            auto ab = AcrylicBrush();
            ab.TintOpacity(0.0);
            ab.BackgroundSource(AcrylicBackgroundSource::HostBackdrop);
            BottomLayoutRoot().Background(ab);
        }
        else {
            auto brush = SolidColorBrush();
            brush.Color(Windows::UI::Colors::White());
            BottomLayoutRoot().Background(brush);
        }
    }
    void MainPage::AskBeforeExitButtonClick(IInspectable const&, RoutedEventArgs const&) {
        if (m_ask_before_close) { return; }
        m_ask_before_close = true;

        auto wnd = Tenkai::Window::GetCurrentMain();
        auto wv = wnd.View();
        wv.Closing([this](auto&& sender, Tenkai::UI::ViewManagement::WindowViewClosingEventArgs const& e) -> fire_and_forget {
            using namespace winrt::Windows::UI::Xaml::Controls;

            auto that = get_strong();

            e.Handled(true);
            if (m_is_asking) { co_return; }
            m_is_asking = true;
            ContentDialog cd;
            cd.XamlRoot(this->myButton().XamlRoot());
            cd.Title(box_value(L"Are you sure you want to exit?"));
            cd.PrimaryButtonText(L"Yes");
            cd.CloseButtonText(L"No");
            auto result = co_await cd.ShowAsync();
            that->m_is_asking = false;
            if (result == ContentDialogResult::Primary) {
                Tenkai::AppService::Quit();
            }
        });
    }
    fire_and_forget MainPage::RestartAppButtonClick(IInspectable const&, RoutedEventArgs const&) {
        if (co_await Tenkai::AppService::RequestRestartAsync({})) {
            Tenkai::AppService::Quit();
        }
    }
}
