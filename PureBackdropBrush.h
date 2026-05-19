#pragma once

#include "PureBackdropBrush.g.h"

namespace winrt::WUILiquidGlassDemo::implementation {
    struct PureBackdropBrush : PureBackdropBrushT<PureBackdropBrush> {
        PureBackdropBrush() = default;

        void OnConnected();
        void OnDisconnected();
    };
}

namespace winrt::WUILiquidGlassDemo::factory_implementation {
    struct PureBackdropBrush : PureBackdropBrushT<PureBackdropBrush, implementation::PureBackdropBrush> {};
}
