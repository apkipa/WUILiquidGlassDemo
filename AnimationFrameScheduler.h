#pragma once

#include <winrt/Windows.Foundation.h>

#include "AnimationFrameScheduler.g.h"

namespace winrt::WUILiquidGlassDemo::implementation
{
    struct AnimationFrameScheduler
    {
        AnimationFrameScheduler() = delete;

        static int64_t RequestAnimationFrame(WUILiquidGlassDemo::RequestAnimationFrameCallback const& callback);
        static void CancelAnimationFrame(int64_t requestId);
        static bool HasPendingCallbacks();
    };
}

namespace winrt::WUILiquidGlassDemo::factory_implementation
{
    struct AnimationFrameScheduler :
        AnimationFrameSchedulerT<AnimationFrameScheduler, implementation::AnimationFrameScheduler>
    {
    };
}
