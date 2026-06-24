#pragma once

#include "MainWindow.g.h"

namespace winrt::WUILiquidGlassDemo_WUI3::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow();

    private:
        enum class BackdropInteraction
        {
            None,
            Drag,
            Resize,
        };

        winrt::Microsoft::UI::Composition::SpriteVisual m_backdropVisual{ nullptr };
        winrt::Microsoft::UI::Input::InputPointerSource m_pointerSource{ nullptr };
        winrt::Microsoft::UI::Input::InputCursor m_arrowCursor{ nullptr };
        winrt::Microsoft::UI::Input::InputCursor m_moveCursor{ nullptr };
        winrt::Microsoft::UI::Input::InputCursor m_resizeCursor{ nullptr };
        BackdropInteraction m_backdropInteraction{ BackdropInteraction::None };
        uint32_t m_activePointerId{};
        winrt::Windows::Foundation::Point m_startPointer{};
        float m_startOffsetX{};
        float m_startOffsetY{};
        float m_startWidth{};
        float m_startHeight{};

        void StartDynamicScene();
        void ClampBackdropVisualRect();
        bool HitTestBackdropVisual(winrt::Windows::Foundation::Point const& position) const;
        bool HitTestResizeGrip(winrt::Windows::Foundation::Point const& position) const;
        void InitializeBackdropCursors();
        void EnsurePointerSource();
        void SetBackdropCursor(winrt::Microsoft::UI::Input::InputCursor const& cursor);
        void UpdateBackdropCursor(winrt::Windows::Foundation::Point const& position);
        void OnRootPointerPressed(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        void OnRootPointerMoved(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        void OnRootPointerReleased(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        void OnRootPointerCanceled(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        void OnRootPointerCaptureLost(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        void OnRootPointerExited(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        void EndBackdropInteraction();
    };
}

namespace winrt::WUILiquidGlassDemo_WUI3::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
