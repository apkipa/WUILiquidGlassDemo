#include "pch.h"
#include "CustomBlurEffect.h"
#include "CustomEffectRuntime.h"

using namespace winrt;

namespace
{
    wchar_t const kBlurEffectName[] = L"BackdropBlurEffect";

    constexpr GUID kCustomBlurEffectId{
        0x7a8f90d4, 0x04ef, 0x4d16, { 0xb0, 0x7d, 0x55, 0x89, 0x50, 0x16, 0x85, 0xf0 } };

    char const kBlurShader[] = R"(
Texture2D texture0;
SamplerState sampler0;

float4 BlurCore(float2 uv)
{
    float2 texelStep = float2(0.004f, 0.004f);
    float4 sum = texture0.Sample(sampler0, uv) * 0.20f;
    sum += texture0.Sample(sampler0, uv + float2( texelStep.x, 0.0f)) * 0.12f;
    sum += texture0.Sample(sampler0, uv + float2(-texelStep.x, 0.0f)) * 0.12f;
    sum += texture0.Sample(sampler0, uv + float2(0.0f,  texelStep.y)) * 0.12f;
    sum += texture0.Sample(sampler0, uv + float2(0.0f, -texelStep.y)) * 0.12f;
    sum += texture0.Sample(sampler0, uv + float2( texelStep.x,  texelStep.y)) * 0.08f;
    sum += texture0.Sample(sampler0, uv + float2(-texelStep.x,  texelStep.y)) * 0.08f;
    sum += texture0.Sample(sampler0, uv + float2( texelStep.x, -texelStep.y)) * 0.08f;
    sum += texture0.Sample(sampler0, uv + float2(-texelStep.x, -texelStep.y)) * 0.08f;
    return sum;
}

// dwmcorei!AppendCustomSamplerShaderBody appends edge-mode suffixes such as CC/MM
// before linking. Exporting aliases keeps this code-only shader library compatible
// with that native convention instead of patching the DWM linker path.
export float4 PSBody(float2 uv) { return BlurCore(uv); }
export float4 PSBodyCC(float2 uv) { return BlurCore(uv); }
export float4 PSBodyCW(float2 uv) { return BlurCore(uv); }
export float4 PSBodyCM(float2 uv) { return BlurCore(uv); }
export float4 PSBodyWC(float2 uv) { return BlurCore(uv); }
export float4 PSBodyWW(float2 uv) { return BlurCore(uv); }
export float4 PSBodyWM(float2 uv) { return BlurCore(uv); }
export float4 PSBodyMC(float2 uv) { return BlurCore(uv); }
export float4 PSBodyMW(float2 uv) { return BlurCore(uv); }
export float4 PSBodyMM(float2 uv) { return BlurCore(uv); }
export float4 PSBodyC(float2 uv) { return BlurCore(uv); }
export float4 PSBodyW(float2 uv) { return BlurCore(uv); }
export float4 PSBodyM(float2 uv) { return BlurCore(uv); }
)";

    constexpr uint16_t kBackdropUvArgument = 0x0100;
    constexpr uint16_t kBackdropCustomSamplerResult = 0x0200;

    CustomEffectRuntime::SourceDescriptor const kSources[] = {
        { L"Backdrop", CustomEffectRuntime::SourceKind::Backdrop },
    };

    uint16_t const kShaderArguments[] = {
        kBackdropUvArgument,
    };

    CustomEffectRuntime::CustomEffectDefinition const kDefinition{
        kCustomBlurEffectId,
        kBlurEffectName,
        "CustomBlurEffect",
        kBlurShader,
        sizeof(kBlurShader) - 1,
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
        kBackdropCustomSamplerResult,
        true,
        0,
        nullptr,
    };
}

namespace CustomBlurEffect
{
    winrt::Windows::Graphics::Effects::IGraphicsEffect CreateEffect()
    {
        // This definition deliberately takes the CCustomKernelEffect-style custom
        // sampler route: argument 0x0100 gives PSBody a UV, while linkingArgType
        // 0x0200 asks dwmcorei to bind texture0/sampler0. Returning an
        // IGraphicsEffect here keeps the public shape compatible with
        // Compositor::CreateEffectFactory instead of hiding the graph inside a brush
        // helper.
        return CustomEffectRuntime::CreateEffect(kDefinition);
    }

}
