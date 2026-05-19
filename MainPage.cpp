#include "pch.h"
#include "MainPage.h"
#include "MainPage.g.cpp"

#include <winrt/Tenkai.UI.Xaml.h>
#include <winrt/Tenkai.UI.ViewManagement.h>

#include <Tenkai.hpp>

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Media;

namespace winrt::Tenkai {
    using UI::Xaml::Window;
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

        if (cur_is_transparent) {
#if 1
            auto cb = winrt::get_activation_factory<IActivationFactory>(xaml_typename<XamlCompositionBrushBase>().Name)
                .ActivateInstance<XamlCompositionBrushBase>();
            auto bb = wnd.Compositor().CreateBackdropBrush();
            cb.as<IXamlCompositionBrushBaseProtected>().CompositionBrush(bb);
            LayoutRoot().Background(cb);
#else
            LayoutRoot().Background(AcrylicBrush());
#endif
        }
        else {
            LayoutRoot().Background(nullptr);
        }
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
