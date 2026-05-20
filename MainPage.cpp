#include "pch.h"
#include "MainPage.h"
#include "MainPage.g.cpp"

#include <d2d1effects.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Effects.h>
#include <winrt/Windows.UI.Composition.h>
#include <windows.graphics.effects.interop.h>

#include <winrt/Tenkai.UI.Xaml.h>
#include <winrt/Tenkai.UI.ViewManagement.h>

#include <Tenkai.hpp>

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::Graphics::Effects;
using namespace Windows::UI::Composition;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Media;
using namespace Windows::UI::Xaml::Hosting;

namespace winrt::Tenkai {
    using UI::Xaml::Window;
}

namespace {
    constexpr GUID kGaussianBlurEffectId{
        0x1feb6d69, 0x2fe6, 0x4ac9, { 0x8c, 0x58, 0x1d, 0x7f, 0x93, 0xe7, 0xa6, 0xa5 }
    };

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
                Windows::Foundation::IPropertyValue propertyValue{ nullptr };

                switch (index) {
                case D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION:
                    propertyValue = PropertyValue::CreateSingle(m_blurAmount).as<Windows::Foundation::IPropertyValue>();
                    break;
                case D2D1_GAUSSIANBLUR_PROP_OPTIMIZATION:
                    propertyValue = PropertyValue::CreateUInt32(
                        static_cast<uint32_t>(D2D1_GAUSSIANBLUR_OPTIMIZATION_BALANCED)).as<Windows::Foundation::IPropertyValue>();
                    break;
                case D2D1_GAUSSIANBLUR_PROP_BORDER_MODE:
                    propertyValue = PropertyValue::CreateUInt32(
                        static_cast<uint32_t>(D2D1_BORDER_MODE_HARD)).as<Windows::Foundation::IPropertyValue>();
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
}

namespace winrt::WUILiquidGlassDemo::implementation {
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
    }
    void MainPage::AskBeforeExitButtonClick(IInspectable const&, RoutedEventArgs const&) {
        if (m_ask_before_close) { return; }
        m_ask_before_close = true;

        auto wnd = Tenkai::Window::GetCurrentMain();
        auto wv = wnd.View();
        wv.Closing([this](auto&& sender, Tenkai::UI::ViewManagement::WindowViewClosingEventArgs const& e) -> fire_and_forget {
            using namespace Windows::UI::Xaml::Controls;

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
