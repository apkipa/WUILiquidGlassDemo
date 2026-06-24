#include "pch.h"
#include "CustomBlurEffect.h"
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
    constexpr float kInitialBackdropOffsetX = 80.0f;
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

        auto compositor = ElementCompositionPreview::GetElementVisual(Root()).Compositor();
        m_backdropVisual = compositor.CreateSpriteVisual();
        // Keep the invert call commented instead of deleting it so the active effect is
        // obvious while comparing the old color-only path with the new custom-sampler
        // blur path.
        // m_backdropVisual.Brush(CustomInvertEffect::CreateBackdropBrush(compositor));
        m_backdropVisual.Brush(CustomBlurEffect::CreateBackdropBrush(compositor));
        m_backdropVisual.Offset({ kInitialBackdropOffsetX, kInitialBackdropOffsetY, 0.0f });
        m_backdropVisual.Size({ kInitialBackdropWidth, kInitialBackdropHeight });

        ElementCompositionPreview::SetElementChildVisual(Root(), m_backdropVisual);
        Root().SizeChanged([weak = get_weak()](auto&&, auto&&)
        {
            if (auto self = weak.get())
            {
                self->ClampBackdropVisualRect();
            }
        });
        Root().Loaded([weak = get_weak()](auto&&, auto&&)
        {
            if (auto self = weak.get())
            {
                self->EnsurePointerSource();
                self->SetBackdropCursor(self->m_arrowCursor);
            }
        });
        Root().PointerPressed([weak = get_weak()](
            IInspectable const& sender,
            PointerRoutedEventArgs const& args)
        {
            if (auto self = weak.get())
            {
                self->OnRootPointerPressed(sender, args);
            }
        });
        Root().PointerMoved([weak = get_weak()](
            IInspectable const& sender,
            PointerRoutedEventArgs const& args)
        {
            if (auto self = weak.get())
            {
                self->OnRootPointerMoved(sender, args);
            }
        });
        Root().PointerReleased([weak = get_weak()](
            IInspectable const& sender,
            PointerRoutedEventArgs const& args)
        {
            if (auto self = weak.get())
            {
                self->OnRootPointerReleased(sender, args);
            }
        });
        Root().PointerCanceled([weak = get_weak()](
            IInspectable const& sender,
            PointerRoutedEventArgs const& args)
        {
            if (auto self = weak.get())
            {
                self->OnRootPointerCanceled(sender, args);
            }
        });
        Root().PointerCaptureLost([weak = get_weak()](
            IInspectable const& sender,
            PointerRoutedEventArgs const& args)
        {
            if (auto self = weak.get())
            {
                self->OnRootPointerCaptureLost(sender, args);
            }
        });
        Root().PointerExited([weak = get_weak()](
            IInspectable const& sender,
            PointerRoutedEventArgs const& args)
        {
            if (auto self = weak.get())
            {
                self->OnRootPointerExited(sender, args);
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

    void MainWindow::ClampBackdropVisualRect()
    {
        if (!m_backdropVisual)
        {
            return;
        }

        auto const rootWidth = static_cast<float>(Root().ActualWidth());
        auto const rootHeight = static_cast<float>(Root().ActualHeight());
        if (rootWidth <= 0.0f || rootHeight <= 0.0f)
        {
            return;
        }

        auto const size = m_backdropVisual.Size();
        auto const offset = m_backdropVisual.Offset();
        auto const width = std::clamp(size.x, kMinimumBackdropWidth, std::max(kMinimumBackdropWidth, rootWidth));
        auto const height = std::clamp(size.y, kMinimumBackdropHeight, std::max(kMinimumBackdropHeight, rootHeight));
        auto const x = std::clamp(offset.x, 0.0f, std::max(0.0f, rootWidth - width));
        auto const y = std::clamp(offset.y, 0.0f, std::max(0.0f, rootHeight - height));

        m_backdropVisual.Size({ width, height });
        m_backdropVisual.Offset({ x, y, 0.0f });
    }

    bool MainWindow::HitTestBackdropVisual(Point const& position) const
    {
        if (!m_backdropVisual)
        {
            return false;
        }

        auto const offset = m_backdropVisual.Offset();
        auto const size = m_backdropVisual.Size();
        return position.X >= offset.x &&
            position.X <= offset.x + size.x &&
            position.Y >= offset.y &&
            position.Y <= offset.y + size.y;
    }

    bool MainWindow::HitTestResizeGrip(Point const& position) const
    {
        if (!m_backdropVisual || !HitTestBackdropVisual(position))
        {
            return false;
        }

        auto const offset = m_backdropVisual.Offset();
        auto const size = m_backdropVisual.Size();
        return position.X >= offset.x + size.x - kResizeGripSize &&
            position.Y >= offset.y + size.y - kResizeGripSize;
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

        auto const xamlRoot = Root().XamlRoot();
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

        if (auto protectedRoot = Root().try_as<IUIElementProtected>())
        {
            protectedRoot.ProtectedCursor(cursor);
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

    void MainWindow::OnRootPointerPressed(IInspectable const&, PointerRoutedEventArgs const& args)
    {
        if (!m_backdropVisual)
        {
            SetBackdropCursor(m_arrowCursor);
            return;
        }

        auto const point = args.GetCurrentPoint(Root());
        auto const position = point.Position();
        if (!HitTestBackdropVisual(position))
        {
            SetBackdropCursor(m_arrowCursor);
            return;
        }

        auto const offset = m_backdropVisual.Offset();
        auto const size = m_backdropVisual.Size();
        m_backdropInteraction = HitTestResizeGrip(position)
            ? BackdropInteraction::Resize
            : BackdropInteraction::Drag;
        m_activePointerId = point.PointerId();
        m_startPointer = position;
        m_startOffsetX = offset.x;
        m_startOffsetY = offset.y;
        m_startWidth = size.x;
        m_startHeight = size.y;

        UpdateBackdropCursor(position);
        Root().CapturePointer(args.Pointer());
        args.Handled(true);
    }

    void MainWindow::OnRootPointerMoved(IInspectable const&, PointerRoutedEventArgs const& args)
    {
        auto const point = args.GetCurrentPoint(Root());
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
        auto const rootWidth = static_cast<float>(Root().ActualWidth());
        auto const rootHeight = static_cast<float>(Root().ActualHeight());
        UpdateBackdropCursor(position);

        if (m_backdropInteraction == BackdropInteraction::Resize)
        {
            auto const maxWidth = std::max(kMinimumBackdropWidth, rootWidth - m_startOffsetX);
            auto const maxHeight = std::max(kMinimumBackdropHeight, rootHeight - m_startOffsetY);
            m_backdropVisual.Size({
                std::clamp(m_startWidth + deltaX, kMinimumBackdropWidth, maxWidth),
                std::clamp(m_startHeight + deltaY, kMinimumBackdropHeight, maxHeight),
            });
        }
        else
        {
            auto const size = m_backdropVisual.Size();
            m_backdropVisual.Offset({
                std::clamp(m_startOffsetX + deltaX, 0.0f, std::max(0.0f, rootWidth - size.x)),
                std::clamp(m_startOffsetY + deltaY, 0.0f, std::max(0.0f, rootHeight - size.y)),
                0.0f,
            });
        }

        args.Handled(true);
    }

    void MainWindow::OnRootPointerReleased(IInspectable const&, PointerRoutedEventArgs const& args)
    {
        if (m_backdropInteraction != BackdropInteraction::None)
        {
            auto const position = args.GetCurrentPoint(Root()).Position();
            Root().ReleasePointerCapture(args.Pointer());
            EndBackdropInteraction();
            UpdateBackdropCursor(position);
            args.Handled(true);
        }
    }

    void MainWindow::OnRootPointerCanceled(IInspectable const&, PointerRoutedEventArgs const& args)
    {
        if (m_backdropInteraction != BackdropInteraction::None)
        {
            Root().ReleasePointerCapture(args.Pointer());
            EndBackdropInteraction();
            SetBackdropCursor(m_arrowCursor);
            args.Handled(true);
        }
    }

    void MainWindow::OnRootPointerCaptureLost(IInspectable const&, PointerRoutedEventArgs const&)
    {
        EndBackdropInteraction();
        SetBackdropCursor(m_arrowCursor);
    }

    void MainWindow::OnRootPointerExited(IInspectable const&, PointerRoutedEventArgs const&)
    {
        if (m_backdropInteraction == BackdropInteraction::None)
        {
            SetBackdropCursor(m_arrowCursor);
        }
    }

    void MainWindow::EndBackdropInteraction()
    {
        m_backdropInteraction = BackdropInteraction::None;
        m_activePointerId = 0;
    }
}
