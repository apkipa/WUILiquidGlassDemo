#include "pch.h"
#include "CustomBlurEffect.h"
#include "CustomInvertEffect.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

using namespace winrt;
using namespace Windows::Foundation;
using namespace Microsoft::UI::Composition;
using namespace Microsoft::UI::Input;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Hosting;
using namespace Microsoft::UI::Xaml::Input;
using namespace Microsoft::UI::Xaml::Media::Animation;

namespace
{
    constexpr float kInitialBackdropWidth = 420.0f;
    constexpr float kInitialBackdropHeight = 260.0f;
    constexpr float kInitialBackdropOffsetX = 268.0f;
    constexpr float kInitialBackdropOffsetY = 80.0f;
    constexpr float kMinimumBackdropWidth = 120.0f;
    constexpr float kMinimumBackdropHeight = 80.0f;
    constexpr float kResizeGripSize = 32.0f;

}

// To learn more about WinUI, the WinUI project structure,
// and more about our project templates, see: http://aka.ms/winui-project-info.

namespace winrt::WUILiquidGlassDemo_WUI3::implementation
{
    MainWindow::MainWindow()
    {
        InitializeComponent();
        StartDynamicScene();
        InitializeBackdropCursors();

        m_borderWidth = static_cast<float>(BorderWidthSlider().Value());
        BackdropFrame().BorderThickness({ m_borderWidth, m_borderWidth, m_borderWidth, m_borderWidth });
        Controls::Canvas::SetLeft(BackdropFrame(), kInitialBackdropOffsetX);
        Controls::Canvas::SetTop(BackdropFrame(), kInitialBackdropOffsetY);
        BackdropFrame().Width(kInitialBackdropWidth);
        BackdropFrame().Height(kInitialBackdropHeight);

        auto compositor = ElementCompositionPreview::GetElementVisual(BackdropVisualHost()).Compositor();
        m_backdropVisual = compositor.CreateSpriteVisual();
        ApplyBackdropEffect();
        UpdateBackdropVisualSize();

        ElementCompositionPreview::SetElementChildVisual(BackdropVisualHost(), m_backdropVisual);
        EffectSelector().SelectionChanged([weak = get_weak()](
            IInspectable const& sender,
            Controls::SelectionChangedEventArgs const& args)
        {
            if (auto self = weak.get())
            {
                self->OnEffectSelectionChanged(sender, args);
            }
        });
        BorderWidthSlider().ValueChanged([weak = get_weak()](
            IInspectable const& sender,
            Controls::Primitives::RangeBaseValueChangedEventArgs const& args)
        {
            if (auto self = weak.get())
            {
                self->OnBorderWidthChanged(sender, args);
            }
        });
        BackdropHost().SizeChanged([weak = get_weak()](auto&&, auto&&)
        {
            if (auto self = weak.get())
            {
                self->ClampBackdropVisualRect();
            }
        });
        BackdropHost().Loaded([weak = get_weak()](auto&&, auto&&)
        {
            if (auto self = weak.get())
            {
                self->EnsurePointerSource();
                self->SetBackdropCursor(self->m_arrowCursor);
            }
        });
        BackdropHost().PointerPressed([weak = get_weak()](
            IInspectable const& sender,
            PointerRoutedEventArgs const& args)
        {
            if (auto self = weak.get())
            {
                self->OnBackdropHostPointerPressed(sender, args);
            }
        });
        BackdropHost().PointerMoved([weak = get_weak()](
            IInspectable const& sender,
            PointerRoutedEventArgs const& args)
        {
            if (auto self = weak.get())
            {
                self->OnBackdropHostPointerMoved(sender, args);
            }
        });
        BackdropHost().PointerReleased([weak = get_weak()](
            IInspectable const& sender,
            PointerRoutedEventArgs const& args)
        {
            if (auto self = weak.get())
            {
                self->OnBackdropHostPointerReleased(sender, args);
            }
        });
        BackdropHost().PointerCanceled([weak = get_weak()](
            IInspectable const& sender,
            PointerRoutedEventArgs const& args)
        {
            if (auto self = weak.get())
            {
                self->OnBackdropHostPointerCanceled(sender, args);
            }
        });
        BackdropHost().PointerCaptureLost([weak = get_weak()](
            IInspectable const& sender,
            PointerRoutedEventArgs const& args)
        {
            if (auto self = weak.get())
            {
                self->OnBackdropHostPointerCaptureLost(sender, args);
            }
        });
        BackdropHost().PointerExited([weak = get_weak()](
            IInspectable const& sender,
            PointerRoutedEventArgs const& args)
        {
            if (auto self = weak.get())
            {
                self->OnBackdropHostPointerExited(sender, args);
            }
        });

        ClampBackdropVisualRect();
    }

    void MainWindow::StartDynamicScene()
    {
        if (auto storyboard = Root().Resources()
            .Lookup(box_value(L"DynamicSceneStoryboard"))
            .try_as<Storyboard>())
        {
            storyboard.Begin();
        }
    }

    void MainWindow::ApplyBackdropEffect()
    {
        if (!m_backdropVisual)
        {
            return;
        }

        auto const compositor = ElementCompositionPreview::GetElementVisual(BackdropVisualHost()).Compositor();
        switch (m_backdropEffect)
        {
        case BackdropEffectKind::Invert:
        {
            auto factory = compositor.CreateEffectFactory(CustomInvertEffect::CreateEffect());
            auto brush = factory.CreateBrush();
            brush.SetSourceParameter(L"Backdrop", compositor.CreateBackdropBrush());
            m_backdropVisual.Brush(brush);
            EffectCaption().Text(L"Backdrop inversion");
            break;
        }
        case BackdropEffectKind::Blur:
        {
            auto factory = compositor.CreateEffectFactory(CustomBlurEffect::CreateEffect());
            auto brush = factory.CreateBrush();
            brush.SetSourceParameter(L"Backdrop", compositor.CreateBackdropBrush());
            m_backdropVisual.Brush(brush);
            EffectCaption().Text(L"Backdrop blur");
            break;
        }
        case BackdropEffectKind::Solid:
        default:
            m_backdropVisual.Brush(compositor.CreateColorBrush(winrt::Windows::UI::Color{ 0x99, 0xff, 0xff, 0xff }));
            EffectCaption().Text(L"Solid translucent brush");
            break;
        }
    }

    void MainWindow::UpdateBackdropVisualSize()
    {
        if (!m_backdropVisual)
        {
            return;
        }

        auto const innerWidth = std::max(0.0f, static_cast<float>(BackdropFrame().Width()) - (m_borderWidth * 2.0f));
        auto const innerHeight = std::max(0.0f, static_cast<float>(BackdropFrame().Height()) - (m_borderWidth * 2.0f));
        m_backdropVisual.Offset({ 0.0f, 0.0f, 0.0f });
        m_backdropVisual.Size({ innerWidth, innerHeight });
    }

    void MainWindow::ClampBackdropVisualRect()
    {
        if (!m_backdropVisual)
        {
            return;
        }

        auto const rootWidth = static_cast<float>(BackdropHost().ActualWidth());
        auto const rootHeight = static_cast<float>(BackdropHost().ActualHeight());
        if (rootWidth <= 0.0f || rootHeight <= 0.0f)
        {
            return;
        }

        auto const width = std::clamp(
            static_cast<float>(BackdropFrame().Width()),
            kMinimumBackdropWidth,
            std::max(kMinimumBackdropWidth, rootWidth));
        auto const height = std::clamp(
            static_cast<float>(BackdropFrame().Height()),
            kMinimumBackdropHeight,
            std::max(kMinimumBackdropHeight, rootHeight));
        auto const x = std::clamp(
            static_cast<float>(Controls::Canvas::GetLeft(BackdropFrame())),
            0.0f,
            std::max(0.0f, rootWidth - width));
        auto const y = std::clamp(
            static_cast<float>(Controls::Canvas::GetTop(BackdropFrame())),
            0.0f,
            std::max(0.0f, rootHeight - height));

        BackdropFrame().Width(width);
        BackdropFrame().Height(height);
        Controls::Canvas::SetLeft(BackdropFrame(), x);
        Controls::Canvas::SetTop(BackdropFrame(), y);
        UpdateBackdropVisualSize();
    }

    bool MainWindow::HitTestBackdropVisual(Point const& position)
    {
        if (!m_backdropVisual)
        {
            return false;
        }

        auto const x = static_cast<float>(Controls::Canvas::GetLeft(BackdropFrame()));
        auto const y = static_cast<float>(Controls::Canvas::GetTop(BackdropFrame()));
        auto const width = static_cast<float>(BackdropFrame().Width());
        auto const height = static_cast<float>(BackdropFrame().Height());
        return position.X >= x &&
            position.X <= x + width &&
            position.Y >= y &&
            position.Y <= y + height;
    }

    bool MainWindow::HitTestResizeGrip(Point const& position)
    {
        if (!m_backdropVisual || !HitTestBackdropVisual(position))
        {
            return false;
        }

        auto const x = static_cast<float>(Controls::Canvas::GetLeft(BackdropFrame()));
        auto const y = static_cast<float>(Controls::Canvas::GetTop(BackdropFrame()));
        auto const width = static_cast<float>(BackdropFrame().Width());
        auto const height = static_cast<float>(BackdropFrame().Height());
        return position.X >= x + width - kResizeGripSize &&
            position.Y >= y + height - kResizeGripSize;
    }

    void MainWindow::InitializeBackdropCursors()
    {
        m_arrowCursor = InputSystemCursor::Create(InputSystemCursorShape::Arrow);
        m_moveCursor = InputSystemCursor::Create(InputSystemCursorShape::SizeAll);
        m_resizeCursor = InputSystemCursor::Create(InputSystemCursorShape::SizeNorthwestSoutheast);
        SetBackdropCursor(m_arrowCursor);
    }

    void MainWindow::EnsurePointerSource()
    {
        if (m_pointerSource)
        {
            return;
        }

        auto const xamlRoot = BackdropHost().XamlRoot();
        if (!xamlRoot)
        {
            return;
        }

        auto const island = xamlRoot.ContentIsland();
        if (!island)
        {
            return;
        }

        m_pointerSource = InputPointerSource::GetForIsland(island);
    }

    void MainWindow::SetBackdropCursor(InputCursor const& cursor)
    {
        if (!cursor)
        {
            return;
        }

        if (auto protectedHost = BackdropHost().try_as<IUIElementProtected>())
        {
            protectedHost.ProtectedCursor(cursor);
        }

        // SpriteVisual is not a XAML element, so setting the cursor only through
        // UIElement.ProtectedCursor can be lost when XAML children are hit-tested.
        // Updating the island input source keeps this code-only visual path intact.
        EnsurePointerSource();
        if (m_pointerSource)
        {
            m_pointerSource.Cursor(cursor);
        }
    }

    void MainWindow::UpdateBackdropCursor(Point const& position)
    {
        if (m_backdropInteraction == BackdropInteraction::Resize || HitTestResizeGrip(position))
        {
            SetBackdropCursor(m_resizeCursor);
            return;
        }

        if (m_backdropInteraction == BackdropInteraction::Drag || HitTestBackdropVisual(position))
        {
            SetBackdropCursor(m_moveCursor);
            return;
        }

        SetBackdropCursor(m_arrowCursor);
    }

    void MainWindow::OnBackdropHostPointerPressed(IInspectable const&, PointerRoutedEventArgs const& args)
    {
        if (!m_backdropVisual)
        {
            SetBackdropCursor(m_arrowCursor);
            return;
        }

        auto const point = args.GetCurrentPoint(BackdropHost());
        auto const position = point.Position();
        if (!HitTestBackdropVisual(position))
        {
            SetBackdropCursor(m_arrowCursor);
            return;
        }

        m_backdropInteraction = HitTestResizeGrip(position)
            ? BackdropInteraction::Resize
            : BackdropInteraction::Drag;
        m_activePointerId = point.PointerId();
        m_startPointer = position;
        m_startOffsetX = static_cast<float>(Controls::Canvas::GetLeft(BackdropFrame()));
        m_startOffsetY = static_cast<float>(Controls::Canvas::GetTop(BackdropFrame()));
        m_startWidth = static_cast<float>(BackdropFrame().Width());
        m_startHeight = static_cast<float>(BackdropFrame().Height());

        UpdateBackdropCursor(position);
        BackdropHost().CapturePointer(args.Pointer());
        args.Handled(true);
    }

    void MainWindow::OnBackdropHostPointerMoved(IInspectable const&, PointerRoutedEventArgs const& args)
    {
        auto const point = args.GetCurrentPoint(BackdropHost());
        auto const position = point.Position();
        if (!m_backdropVisual)
        {
            SetBackdropCursor(m_arrowCursor);
            return;
        }

        if (m_backdropInteraction == BackdropInteraction::None)
        {
            UpdateBackdropCursor(position);
            return;
        }

        if (point.PointerId() != m_activePointerId)
        {
            return;
        }

        auto const deltaX = position.X - m_startPointer.X;
        auto const deltaY = position.Y - m_startPointer.Y;
        auto const rootWidth = static_cast<float>(BackdropHost().ActualWidth());
        auto const rootHeight = static_cast<float>(BackdropHost().ActualHeight());
        UpdateBackdropCursor(position);

        if (m_backdropInteraction == BackdropInteraction::Resize)
        {
            auto const maxWidth = std::max(kMinimumBackdropWidth, rootWidth - m_startOffsetX);
            auto const maxHeight = std::max(kMinimumBackdropHeight, rootHeight - m_startOffsetY);
            BackdropFrame().Width(std::clamp(m_startWidth + deltaX, kMinimumBackdropWidth, maxWidth));
            BackdropFrame().Height(std::clamp(m_startHeight + deltaY, kMinimumBackdropHeight, maxHeight));
            UpdateBackdropVisualSize();
        }
        else
        {
            auto const width = static_cast<float>(BackdropFrame().Width());
            auto const height = static_cast<float>(BackdropFrame().Height());
            Controls::Canvas::SetLeft(
                BackdropFrame(),
                std::clamp(m_startOffsetX + deltaX, 0.0f, std::max(0.0f, rootWidth - width)));
            Controls::Canvas::SetTop(
                BackdropFrame(),
                std::clamp(m_startOffsetY + deltaY, 0.0f, std::max(0.0f, rootHeight - height)));
        }

        args.Handled(true);
    }

    void MainWindow::OnBackdropHostPointerReleased(IInspectable const&, PointerRoutedEventArgs const& args)
    {
        if (m_backdropInteraction != BackdropInteraction::None)
        {
            auto const position = args.GetCurrentPoint(BackdropHost()).Position();
            BackdropHost().ReleasePointerCapture(args.Pointer());
            EndBackdropInteraction();
            UpdateBackdropCursor(position);
            args.Handled(true);
        }
    }

    void MainWindow::OnBackdropHostPointerCanceled(IInspectable const&, PointerRoutedEventArgs const& args)
    {
        if (m_backdropInteraction != BackdropInteraction::None)
        {
            BackdropHost().ReleasePointerCapture(args.Pointer());
            EndBackdropInteraction();
            SetBackdropCursor(m_arrowCursor);
            args.Handled(true);
        }
    }

    void MainWindow::OnBackdropHostPointerCaptureLost(IInspectable const&, PointerRoutedEventArgs const&)
    {
        EndBackdropInteraction();
        SetBackdropCursor(m_arrowCursor);
    }

    void MainWindow::OnBackdropHostPointerExited(IInspectable const&, PointerRoutedEventArgs const&)
    {
        if (m_backdropInteraction == BackdropInteraction::None)
        {
            SetBackdropCursor(m_arrowCursor);
        }
    }

    void MainWindow::OnEffectSelectionChanged(IInspectable const&, Controls::SelectionChangedEventArgs const&)
    {
        auto const selectedIndex = EffectSelector().SelectedIndex();
        if (selectedIndex == 1)
        {
            m_backdropEffect = BackdropEffectKind::Blur;
        }
        else if (selectedIndex == 2)
        {
            m_backdropEffect = BackdropEffectKind::Invert;
        }
        else
        {
            m_backdropEffect = BackdropEffectKind::Solid;
        }

        ApplyBackdropEffect();
    }

    void MainWindow::OnBorderWidthChanged(
        IInspectable const&,
        Controls::Primitives::RangeBaseValueChangedEventArgs const& args)
    {
        m_borderWidth = static_cast<float>(args.NewValue());
        BackdropFrame().BorderThickness({ m_borderWidth, m_borderWidth, m_borderWidth, m_borderWidth });
        UpdateBackdropVisualSize();
    }

    void MainWindow::EndBackdropInteraction()
    {
        m_backdropInteraction = BackdropInteraction::None;
        m_activePointerId = 0;
    }
}
