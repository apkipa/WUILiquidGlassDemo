#include "pch.h"
#include "AnimationFrameScheduler.h"
#if __has_include("AnimationFrameScheduler.g.cpp")
#include "AnimationFrameScheduler.g.cpp"
#endif

#include <Tenkai.hpp>

#include <algorithm>
#include <chrono>
#include <utility>
#include <vector>

using namespace winrt;

namespace winrt::WUILiquidGlassDemo::implementation
{
    namespace
    {
        namespace wf = winrt::Windows::Foundation;
        namespace wuxm = winrt::Windows::UI::Xaml::Media;

        struct AnimationFrameSchedulerState;
        AnimationFrameSchedulerState& GetAnimationFrameSchedulerState();

        struct AnimationFrameSchedulerState final
        {
            struct PendingCallback final
            {
                int64_t Id{};
                WUILiquidGlassDemo::RequestAnimationFrameCallback Callback{ nullptr };
            };

            int64_t RequestAnimationFrame(WUILiquidGlassDemo::RequestAnimationFrameCallback const& callback)
            {
                if (!callback)
                {
                    throw hresult_invalid_argument(L"callback must not be null.");
                }

                auto requestId = m_nextRequestId++;
                auto const subscribedForThisRequest = !m_renderingToken;
                if (subscribedForThisRequest)
                {
                    SubscribeRendering();
                }

                try
                {
                    m_pendingCallbacks.push_back({ requestId, callback });
                }
                catch (...)
                {
                    if (subscribedForThisRequest && m_pendingCallbacks.empty())
                    {
                        UnsubscribeRendering();
                    }
                    throw;
                }

                return requestId;
            }

            void CancelAnimationFrame(int64_t requestId)
            {
                auto const startIndex = m_isDispatching ? m_dispatchingIndex : size_t{};
                for (size_t callbackIndex = startIndex; callbackIndex < m_pendingCallbacks.size(); ++callbackIndex)
                {
                    auto& pending = m_pendingCallbacks[callbackIndex];
                    if (pending.Id != requestId)
                    {
                        continue;
                    }

                    if (m_isDispatching)
                    {
                        pending.Callback = nullptr;
                    }
                    else
                    {
                        m_pendingCallbacks.erase(m_pendingCallbacks.begin() + static_cast<std::ptrdiff_t>(callbackIndex));
                    }

                    break;
                }

                if (!m_isDispatching && !HasPendingCallbacks())
                {
                    m_pendingCallbacks.clear();
                    UnsubscribeRendering();
                }
            }

            void Reset() noexcept
            {
                if (m_isDispatching)
                {
                    for (size_t callbackIndex = m_dispatchingIndex; callbackIndex < m_pendingCallbacks.size(); ++callbackIndex)
                    {
                        m_pendingCallbacks[callbackIndex].Callback = nullptr;
                    }
                }
                else
                {
                    m_pendingCallbacks.clear();
                    UnsubscribeRendering();
                }
            }

            bool HasPendingCallbacks() const noexcept
            {
                auto const startIndex = m_isDispatching ? m_dispatchingIndex : size_t{};
                for (size_t callbackIndex = startIndex; callbackIndex < m_pendingCallbacks.size(); ++callbackIndex)
                {
                    if (m_pendingCallbacks[callbackIndex].Callback)
                    {
                        return true;
                    }
                }

                return false;
            }

            void OnRendering(wuxm::RenderingEventArgs const& renderingArgs)
            {
                if (!HasPendingCallbacks())
                {
                    m_pendingCallbacks.clear();
                    UnsubscribeRendering();
                    return;
                }

                m_isDispatching = true;
                m_dispatchingIndex = 0;
                m_dispatchingCount = m_pendingCallbacks.size();

                auto cleanup = tenkai::cpp_utils::scope_exit([&]() noexcept
                    {
                        auto const processedCount =
                            std::min(m_dispatchingCount, m_dispatchingIndex + (m_dispatchingIndex < m_dispatchingCount ? size_t{ 1 } : size_t{}));
                        if (processedCount > 0)
                        {
                            m_pendingCallbacks.erase(
                                m_pendingCallbacks.begin(),
                                m_pendingCallbacks.begin() + static_cast<std::ptrdiff_t>(processedCount));
                        }

                        m_isDispatching = false;
                        m_dispatchingIndex = 0;
                        m_dispatchingCount = 0;
                        if (!HasPendingCallbacks())
                        {
                            m_pendingCallbacks.clear();
                            UnsubscribeRendering();
                        }
                    });

                auto const timestamp = std::chrono::duration<double, std::milli>(renderingArgs.RenderingTime()).count();

                for (; m_dispatchingIndex < m_dispatchingCount; ++m_dispatchingIndex)
                {
                    auto callback = m_pendingCallbacks[m_dispatchingIndex].Callback;
                    if (!callback)
                    {
                        continue;
                    }

                    m_pendingCallbacks[m_dispatchingIndex].Callback = nullptr;
                    // Unlike HTML rAF, an exception here aborts the rest of this batch
                    // instead of being reported while later callbacks continue running.
                    callback(timestamp);
                }
            }

            void SubscribeRendering()
            {
                if (m_renderingToken)
                {
                    return;
                }

                m_renderingToken = wuxm::CompositionTarget::Rendering(
                    [](wf::IInspectable const&, wf::IInspectable const& args)
                    {
                        GetAnimationFrameSchedulerState().OnRendering(args.as<wuxm::RenderingEventArgs>());
                    });
            }

            void UnsubscribeRendering() noexcept
            {
                if (!m_renderingToken)
                {
                    return;
                }

                auto const renderingToken = std::exchange(m_renderingToken, {});
                try
                {
                    wuxm::CompositionTarget::Rendering(renderingToken);
                }
                catch (...)
                {
                }
            }

            ~AnimationFrameSchedulerState() noexcept
            {
                try
                {
                    Reset();
                }
                catch (...)
                {
                }
            }

        private:
            int64_t m_nextRequestId{ 1 };
            bool m_isDispatching{};
            winrt::event_token m_renderingToken{};
            std::vector<PendingCallback> m_pendingCallbacks;
            size_t m_dispatchingIndex{};
            size_t m_dispatchingCount{};
        };

        AnimationFrameSchedulerState& GetAnimationFrameSchedulerState()
        {
            thread_local AnimationFrameSchedulerState state;
            return state;
        }
    }

    int64_t AnimationFrameScheduler::RequestAnimationFrame(
        WUILiquidGlassDemo::RequestAnimationFrameCallback const& callback)
    {
        return GetAnimationFrameSchedulerState().RequestAnimationFrame(callback);
    }

    void AnimationFrameScheduler::CancelAnimationFrame(int64_t requestId)
    {
        GetAnimationFrameSchedulerState().CancelAnimationFrame(requestId);
    }

    bool AnimationFrameScheduler::HasPendingCallbacks()
    {
        return GetAnimationFrameSchedulerState().HasPendingCallbacks();
    }
}
