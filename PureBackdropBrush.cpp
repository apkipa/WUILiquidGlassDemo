#include "pch.h"
#include "PureBackdropBrush.h"
#if __has_include("PureBackdropBrush.g.cpp")
#include "PureBackdropBrush.g.cpp"
#endif

namespace winrt::WUILiquidGlassDemo::implementation {
    void PureBackdropBrush::OnConnected() {
        auto compositor = Tenkai::UI::Xaml::Window::GetCurrentMain().Compositor();
        auto brush = compositor.CreateBackdropBrush();
        CompositionBrush(brush);
    }

    void PureBackdropBrush::OnDisconnected()
    {
        throw hresult_not_implemented();
    }
}
