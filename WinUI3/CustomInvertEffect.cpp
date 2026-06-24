#include "pch.h"
#include "CustomInvertEffect.h"
#include "CustomEffectRuntime.h"

using namespace winrt;
using namespace Microsoft::UI::Composition;

namespace
{
    wchar_t const kInvertEffectName[] = L"BackdropInvertEffect";

    constexpr GUID kCustomInvertEffectId{
        0x2fd153f9, 0x9c8f, 0x4b68, { 0x95, 0x4d, 0x0f, 0x8b, 0x0c, 0x94, 0xf2, 0x62 } };

    char const kInvertShader[] = R"(
export float4 PSBody(float4 sample0)
{
    return float4(1.0f - sample0.rgb, sample0.a);
}
)";

    constexpr uint16_t kBackdropSampleArgument = 0x0200;

    CustomEffectRuntime::SourceDescriptor const kSources[] = {
        { L"Backdrop", CustomEffectRuntime::SourceKind::Backdrop },
    };

    uint16_t const kShaderArguments[] = {
        kBackdropSampleArgument,
    };

    CustomEffectRuntime::CustomEffectDefinition const kDefinition{
        kCustomInvertEffectId,
        kInvertEffectName,
        "CustomInvertEffect",
        kInvertShader,
        sizeof(kInvertShader) - 1,
        "PSBody",
        kSources,
        ARRAYSIZE(kSources),
        nullptr,
        0,
        nullptr,
        0,
        0,
        kShaderArguments,
        ARRAYSIZE(kShaderArguments),
        0,
        true,
        0,
        nullptr,
    };
}

namespace CustomInvertEffect
{
    CompositionEffectBrush CreateBackdropBrush(Compositor const& compositor)
    {
        // This remains a private EffectType even though it uses the generated-effect
        // color argument ABI (0x0200). The divergence from a Texture2D shader is
        // intentional: this path proves the runtime can host a color-input effect
        // without masquerading as a built-in ColorMatrix/Invert effect.
        return CustomEffectRuntime::CreateBackdropBrush(compositor, kDefinition);
    }
}
