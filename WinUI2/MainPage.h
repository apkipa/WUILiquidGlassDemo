#pragma once

#include "MainPage.g.h"

namespace winrt::WUILiquidGlassDemo::implementation
{
    struct MainPage : MainPageT<MainPage>
    {
        MainPage()
        {
            // Xaml objects should not call InitializeComponent during construction.
            // See https://github.com/microsoft/cppwinrt/tree/master/nuget#initializecomponent
            Loaded({ this, &MainPage::MainPageLoaded });
            Unloaded({ this, &MainPage::MainPageUnloaded });
        }
        ~MainPage();

        int32_t MyProperty();
        void MyProperty(int32_t value);

        void ClickHandler(Windows::Foundation::IInspectable const& sender, Windows::UI::Xaml::RoutedEventArgs const& args);
        void SwitchTitleBarButtonClick(Windows::Foundation::IInspectable const& sender, Windows::UI::Xaml::RoutedEventArgs const& args);
        void SwitchBackgroundTransparencyButtonClick(Windows::Foundation::IInspectable const& sender, Windows::UI::Xaml::RoutedEventArgs const& args);
        void AskBeforeExitButtonClick(Windows::Foundation::IInspectable const& sender, Windows::UI::Xaml::RoutedEventArgs const& args);
        fire_and_forget RestartAppButtonClick(Windows::Foundation::IInspectable const& sender, Windows::UI::Xaml::RoutedEventArgs const& args);
        void MainPageLoaded(Windows::Foundation::IInspectable const& sender, Windows::UI::Xaml::RoutedEventArgs const& args);
        void MainPageUnloaded(Windows::Foundation::IInspectable const& sender, Windows::UI::Xaml::RoutedEventArgs const& args);
        fire_and_forget BottomLayoutRootDragEnter(
            Windows::Foundation::IInspectable const& sender,
            Windows::UI::Xaml::DragEventArgs args);
        fire_and_forget BottomLayoutRootDrop(
            Windows::Foundation::IInspectable const& sender,
            Windows::UI::Xaml::DragEventArgs args);
        fire_and_forget BottomLayoutRootDragOver(
            Windows::Foundation::IInspectable const& sender,
            Windows::UI::Xaml::DragEventArgs args);
        void RenderSurfaceHostSizeChanged(Windows::Foundation::IInspectable const& sender, Windows::UI::Xaml::SizeChangedEventArgs const& args);
        void OverlaySliderValueChanged(
            Windows::Foundation::IInspectable const& sender,
            Windows::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const& args);

    private:
        Windows::Foundation::IAsyncAction TrySetBottomLayoutRootBackgroundImageAsync(
            Windows::ApplicationModel::DataTransfer::DataPackageView dataView);
        void InitializeOverlayRenderer();
        void CleanupOverlayRenderer() noexcept;
        void EnsureD3DResources();
        void EnsureOverlaySurface();
        void EnsureBackdropCaptureSource();
        void UpdateBackdropCaptureSourceSize();
        void UpdateOverlaySurfaceSize();
        void EnsureBlurResources(DXGI_FORMAT format, uint32_t width, uint32_t height);
        void UpdateSliderRanges();
        void ScheduleOverlayRender();
        void RunBackdropBlur(ID3D11ShaderResourceView* sourceShaderResourceView, float blurRadius);
        void RenderOverlaySurface();
        float GetOverlayRasterizationScale();

        bool m_ask_before_close{};
        bool m_is_asking{};
        bool m_isOverlayInitialized{};
        int64_t m_renderRequestId{};
        Windows::Graphics::SizeInt32 m_surfaceSize{};
        Windows::UI::Composition::Compositor m_compositor{ nullptr };
        Windows::UI::Composition::CompositionGraphicsDevice m_compositionGraphicsDevice{ nullptr };
        Windows::UI::Composition::CompositionDrawingSurface m_drawingSurface{ nullptr };
        Windows::UI::Composition::CompositionVisualSurface m_backdropCaptureSurface{ nullptr };
        Windows::UI::Composition::CompositionSurfaceBrush m_backdropCaptureBrush{ nullptr };
        Windows::UI::Composition::SpriteVisual m_backdropCaptureVisual{ nullptr };
        Windows::UI::Composition::CompositionSurfaceBrush m_surfaceBrush{ nullptr };
        Windows::UI::Composition::SpriteVisual m_surfaceVisual{ nullptr };
        Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice m_winrtD3DDevice{ nullptr };
        WUILiquidGlassDemo::BackdropImageGenerator m_backdropImageGenerator{ nullptr };
        WUILiquidGlassDemo::BackdropImageGenerator::FrameUpdated_revoker m_frameUpdatedRevoker{};
        winrt::com_ptr<ID3D11Device5> m_d3dDevice;
        winrt::com_ptr<ID3D11DeviceContext4> m_d3dContext;
        winrt::com_ptr<ID2D1Device1> m_d2dDevice;
        winrt::com_ptr<ID3D11VertexShader> m_vertexShader;
        winrt::com_ptr<ID3D11VertexShader> m_fullscreenVertexShader;
        winrt::com_ptr<ID3D11PixelShader> m_pixelShader;
        winrt::com_ptr<ID3D11PixelShader> m_blurPixelShader;
        winrt::com_ptr<ID3D11Buffer> m_constantBuffer;
        winrt::com_ptr<ID3D11Buffer> m_blurConstantBuffer;
        winrt::com_ptr<ID3D11RasterizerState> m_rasterizerState;
        winrt::com_ptr<ID3D11SamplerState> m_samplerState;
        DXGI_FORMAT m_blurTextureFormat{ DXGI_FORMAT_UNKNOWN };
        Windows::Graphics::SizeInt32 m_blurTextureSize{};
        winrt::com_ptr<ID3D11Texture2D> m_blurTextureA;
        winrt::com_ptr<ID3D11Texture2D> m_blurTextureB;
        winrt::com_ptr<ID3D11RenderTargetView> m_blurRenderTargetViewA;
        winrt::com_ptr<ID3D11RenderTargetView> m_blurRenderTargetViewB;
        winrt::com_ptr<ID3D11ShaderResourceView> m_blurShaderResourceViewA;
        winrt::com_ptr<ID3D11ShaderResourceView> m_blurShaderResourceViewB;
    };
}

namespace winrt::WUILiquidGlassDemo::factory_implementation
{
    struct MainPage : MainPageT<MainPage, implementation::MainPage>
    {
    };
}
