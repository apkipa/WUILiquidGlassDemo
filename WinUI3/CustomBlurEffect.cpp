#include "pch.h"
#include "CustomBlurEffect.h"

using namespace winrt;
using namespace Microsoft::UI::Composition;
using namespace Windows::Foundation;
using namespace Windows::Graphics::Effects;

namespace
{
    wchar_t const kBlurEffectName[] = L"BackdropBlurEffect";

    constexpr GUID kCustomBlurEffectId{
        0x7a8f90d4, 0x04ef, 0x4d16, { 0xb0, 0x7d, 0x55, 0x89, 0x50, 0x16, 0x85, 0xf0 } };

    constexpr uintptr_t kEffectTypeFromGuidRva = 0x17c48;
    constexpr uintptr_t kEffectTypeTableRva = 0x62150;
    constexpr uintptr_t kEffectTypeGetBoundsRva = 0x1e040;
    constexpr uintptr_t kEffectTypeCalcInputBoundsRva = 0x1d700;
    constexpr size_t kEffectTypeCount = 0x1f;
    // Reverse engineering shows EffectType virtual calls stop at slot 21
    // (+0xa8, GetEffectOpacityRelation) in this WinAppSDK build. Slot 22+
    // is not part of the callable ABI we need to model for the private GUID.
    constexpr size_t kEffectTypeVtableSlotCount = 22;
    constexpr size_t kFromGuidPatchSize = 15;

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
// before calling AppendNode. Exporting these aliases keeps the code-only shader library
// compatible with that native naming convention instead of forcing a DWM-side patch.
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

    winrt::com_ptr<ID3DBlob> g_shaderBlob;
    std::once_flag g_shaderOnce;

    void EnsureShader()
    {
        std::call_once(g_shaderOnce, []
        {
            // wuceffectsi!EffectGenerator::BuildCompiledEffectSubgraph does not compile
            // generated effects as lib_5_0. This WinUI3 build passes
            // "lib_4_0_level_9_3_ps_only" and flags 0x8800
            // (STRICTNESS | OPTIMIZATION_LEVEL3) to D3DCompile, then dwmcorei links the
            // library into ps_4_0/ps_4_0_level_9_x. Matching that profile is intentional:
            // D3DLoadModule/CreateInstance accepts a lib_5_0 blob, but the final DWM
            // ID3D11Linker::Link path rejects the mixed-profile graph with E_FAIL.
            UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;

            winrt::com_ptr<ID3DBlob> errors;
            check_hresult(D3DCompile(
                kBlurShader,
                sizeof(kBlurShader) - 1,
                nullptr,
                nullptr,
                nullptr,
                // dwmcorei!LoadShaderBody consumes this blob with D3DLoadModule and then
                // CreateInstance("PSBody"). A standalone ps_4_0 blob compiles locally but
                // is not a loadable shader-linking module, so the render path returns
                // E_INVALIDARG from brushrenderingeffect.cpp when GetShaders links it.
                // Runtime library compilation keeps the demo code-only and intentionally
                // diverges from the v3 note's offline fxc /T ps_4_0 artifact path, while
                // still matching wuceffectsi's generated-effect library profile.
                nullptr,
                "lib_4_0_level_9_3_ps_only",
                flags,
                0,
                g_shaderBlob.put(),
                errors.put()));
        });
    }

    struct BlurEffect :
        winrt::implements<
            BlurEffect,
            IGraphicsEffect,
            IGraphicsEffectSource,
            ABI::Windows::Graphics::Effects::IGraphicsEffectD2D1Interop>
    {
        hstring Name() const
        {
            return m_name;
        }

        void Name(hstring const& value)
        {
            m_name = value;
        }

        HRESULT __stdcall GetEffectId(GUID* id) noexcept final
        {
            if (!id)
            {
                return E_POINTER;
            }

            // Unknown GUIDs are rejected by wuceffectsi!EffectType::FromGuid before the
            // compile hook runs. We still publish a private id here because the new
            // FromGuid detour registers a real EffectType for this GUID; keeping the
            // identity private is what separates this path from the old ColorMatrix
            // placeholder workaround.
            *id = kCustomBlurEffectId;
            return S_OK;
        }

        HRESULT __stdcall GetNamedPropertyMapping(
            LPCWSTR,
            UINT*,
            ABI::Windows::Graphics::Effects::GRAPHICS_EFFECT_PROPERTY_MAPPING*) noexcept final
        {
            return E_INVALIDARG;
        }

        HRESULT __stdcall GetPropertyCount(UINT* count) noexcept final
        {
            if (!count)
            {
                return E_POINTER;
            }

            *count = 0;
            return S_OK;
        }

        HRESULT __stdcall GetProperty(UINT index, ABI::Windows::Foundation::IPropertyValue** value) noexcept final
        {
            if (!value)
            {
                return E_POINTER;
            }

            *value = nullptr;
            UNREFERENCED_PARAMETER(index);
            return E_INVALIDARG;
        }

        HRESULT __stdcall GetSource(
            UINT index,
            ABI::Windows::Graphics::Effects::IGraphicsEffectSource** source) noexcept final
        {
            if (!source)
            {
                return E_POINTER;
            }

            *source = nullptr;
            if (index != 0)
            {
                return E_INVALIDARG;
            }

            try
            {
                auto parameter = CompositionEffectSourceParameter(L"Backdrop").as<IGraphicsEffectSource>();
                *source = reinterpret_cast<ABI::Windows::Graphics::Effects::IGraphicsEffectSource*>(
                    detach_abi(parameter));
                return S_OK;
            }
            catch (...)
            {
                return to_hresult();
            }
        }

        HRESULT __stdcall GetSourceCount(UINT* count) noexcept final
        {
            if (!count)
            {
                return E_POINTER;
            }

            *count = 1;
            return S_OK;
        }

    private:
        hstring m_name{ kBlurEffectName };
    };

    struct CustomEffectType
    {
        void** vtable;
    };

    void* g_customEffectTypeVtable[kEffectTypeVtableSlotCount]{};
    CustomEffectType g_customEffectType{ g_customEffectTypeVtable };

    bool SameGuid(GUID const& left, GUID const& right)
    {
        return left.Data1 == right.Data1 &&
            left.Data2 == right.Data2 &&
            left.Data3 == right.Data3 &&
            memcmp(left.Data4, right.Data4, sizeof(left.Data4)) == 0;
    }

    char const* __fastcall CustomEffectType_GetShaderFragmentName(CustomEffectType*)
    {
        return "CustomBlurEffect";
    }

    GUID const* __fastcall CustomEffectType_GetGuid(CustomEffectType*)
    {
        return &kCustomBlurEffectId;
    }

    bool __fastcall CustomEffectType_IsValidInputCount(CustomEffectType*, uint32_t sourceCount)
    {
        return sourceCount == 1;
    }

    bool __fastcall CustomEffectType_IsValidInputType(CustomEffectType*, uint32_t inputType)
    {
        return inputType != 0;
    }

    uint32_t __fastcall CustomEffectType_GetPropertiesStructSize(CustomEffectType*)
    {
        return 0;
    }

    uint32_t __fastcall CustomEffectType_GetEffectSamplingBehavior(CustomEffectType*)
    {
        return 0;
    }

    bool __fastcall CustomEffectType_ReturnFalse(CustomEffectType*)
    {
        return false;
    }

    bool __fastcall CustomEffectType_ReturnTrue(CustomEffectType*)
    {
        return true;
    }

    bool __fastcall CustomEffectType_IsInputTransform(CustomEffectType*, uint32_t* mode)
    {
        if (mode)
        {
            *mode = 0;
        }

        return false;
    }

    bool __fastcall CustomEffectType_IsIntersectionCombinator(CustomEffectType*, void const*)
    {
        return false;
    }

    bool __fastcall CustomEffectType_IsNoOp(CustomEffectType*, uint32_t, void const*)
    {
        return false;
    }

    uint32_t __fastcall CustomEffectType_GetEffectOpacityRelation(CustomEffectType*, void const*)
    {
        return 0;
    }

    void __fastcall CustomEffectType_GetPropertiesMetadata(
        CustomEffectType*,
        uint32_t* count,
        void const** metadata)
    {
        if (count)
        {
            *count = 0;
        }

        if (metadata)
        {
            *metadata = nullptr;
        }
    }

    void __fastcall CustomEffectType_Validate(CustomEffectType*, void const*)
    {
    }

    void __fastcall CustomEffectType_GenerateCode(
        CustomEffectType*,
        void const*,
        void*,
        char const*)
    {
        // This should not be reached for the custom GUID because CompileEffectDescription
        // detects our EffectType and returns a CompiledEffect-shaped object directly.
        // The no-op is only a defensive stub so accidental fallback does not call the
        // built-in EffectType::GenerateCode and hide whether our shader path ran.
    }

    void InitializeCustomEffectType(HMODULE wuceffectsi)
    {
        auto const base = reinterpret_cast<uint8_t*>(wuceffectsi);

        std::fill(
            g_customEffectTypeVtable,
            g_customEffectTypeVtable + kEffectTypeVtableSlotCount,
            nullptr);

        // This vtable is spelled out instead of cloning a built-in EffectType. EffectType
        // slots are not COM methods, and several folded tiny functions have different
        // meanings depending on their slot. The 22 entries cover every observed
        // EffectType virtual call from traversal, flattening, hashing, opacity
        // propagation, and generator code in this wuceffectsi build. The only native
        // functions reused here are the neutral EffectType base bounds helpers for slot
        // 15/16, whose ABI includes struct-return/output-parameter details that this
        // compile-detour path does not otherwise need to reimplement.
        g_customEffectTypeVtable[0] = reinterpret_cast<void*>(CustomEffectType_GetShaderFragmentName);
        g_customEffectTypeVtable[1] = reinterpret_cast<void*>(CustomEffectType_GetGuid);
        g_customEffectTypeVtable[2] = reinterpret_cast<void*>(CustomEffectType_GetEffectSamplingBehavior);
        g_customEffectTypeVtable[3] = reinterpret_cast<void*>(CustomEffectType_IsValidInputCount);
        g_customEffectTypeVtable[4] = reinterpret_cast<void*>(CustomEffectType_IsValidInputType);
        g_customEffectTypeVtable[5] = reinterpret_cast<void*>(CustomEffectType_ReturnFalse);
        g_customEffectTypeVtable[6] = reinterpret_cast<void*>(CustomEffectType_IsInputTransform);
        g_customEffectTypeVtable[7] = reinterpret_cast<void*>(CustomEffectType_ReturnFalse);
        g_customEffectTypeVtable[8] = reinterpret_cast<void*>(CustomEffectType_ReturnFalse);
        g_customEffectTypeVtable[9] = reinterpret_cast<void*>(CustomEffectType_ReturnFalse);
        g_customEffectTypeVtable[10] = reinterpret_cast<void*>(CustomEffectType_ReturnFalse);
        g_customEffectTypeVtable[11] = reinterpret_cast<void*>(CustomEffectType_ReturnFalse);
        g_customEffectTypeVtable[12] = reinterpret_cast<void*>(CustomEffectType_ReturnTrue);
        g_customEffectTypeVtable[13] = reinterpret_cast<void*>(CustomEffectType_IsIntersectionCombinator);
        g_customEffectTypeVtable[14] = reinterpret_cast<void*>(CustomEffectType_IsNoOp);
        g_customEffectTypeVtable[15] = base + kEffectTypeGetBoundsRva;
        g_customEffectTypeVtable[16] = base + kEffectTypeCalcInputBoundsRva;
        g_customEffectTypeVtable[17] = reinterpret_cast<void*>(CustomEffectType_GetPropertiesStructSize);
        g_customEffectTypeVtable[18] = reinterpret_cast<void*>(CustomEffectType_GetPropertiesMetadata);
        g_customEffectTypeVtable[19] = reinterpret_cast<void*>(CustomEffectType_Validate);
        g_customEffectTypeVtable[20] = reinterpret_cast<void*>(CustomEffectType_GenerateCode);
        g_customEffectTypeVtable[21] = reinterpret_cast<void*>(CustomEffectType_GetEffectOpacityRelation);
    }

    void* __fastcall DetourEffectTypeFromGuid(GUID const* guid)
    {
        if (guid && SameGuid(*guid, kCustomBlurEffectId))
        {
            return &g_customEffectType;
        }

        auto const module = GetModuleHandleW(L"wuceffectsi.dll");
        if (!module || !guid)
        {
            return nullptr;
        }

        auto const base = reinterpret_cast<uint8_t*>(module);
        auto* table = reinterpret_cast<void**>(base + kEffectTypeTableRva);
        for (size_t index = 0; index < kEffectTypeCount; ++index)
        {
            auto* effectType = table[index];
            if (!effectType)
            {
                continue;
            }

            auto* vtable = *reinterpret_cast<void***>(effectType);
            auto const getGuid = reinterpret_cast<GUID const*(__fastcall*)(void*)>(vtable[1]);
            auto const knownGuid = getGuid(effectType);
            if (knownGuid && SameGuid(*knownGuid, *guid))
            {
                return effectType;
            }
        }

        return nullptr;
    }

    void PatchEffectTypeFromGuid(HMODULE wuceffectsi)
    {
        auto* target = reinterpret_cast<uint8_t*>(wuceffectsi) + kEffectTypeFromGuidRva;
        uint8_t const expected[kFromGuidPatchSize] = {
            0x48, 0x89, 0x5c, 0x24, 0x08,
            0x48, 0x89, 0x74, 0x24, 0x10,
            0x48, 0x89, 0x7c, 0x24, 0x18,
        };

        if (memcmp(target, expected, sizeof(expected)) != 0)
        {
            check_hresult(HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH));
        }

        uint8_t patch[kFromGuidPatchSize] = {
            0x48, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0,
            0xff, 0xe0,
            0x90, 0x90, 0x90,
        };
        *reinterpret_cast<void**>(patch + 2) = reinterpret_cast<void*>(DetourEffectTypeFromGuid);

        DWORD oldProtect{};
        check_bool(VirtualProtect(target, sizeof(patch), PAGE_EXECUTE_READWRITE, &oldProtect));
        memcpy(target, patch, sizeof(patch));
        FlushInstructionCache(GetCurrentProcess(), target, sizeof(patch));
        DWORD unused{};
        VirtualProtect(target, sizeof(patch), oldProtect, &unused);
    }

    struct ShaderLinkingBody
    {
        uint64_t argCount;
        void const* argData;
        uint64_t bytecodeSize;
        void const* bytecodeData;
        char const* functionName;
        uint32_t constantBufferSize;
        uint16_t linkingArgType;
        uint8_t hasCustomSamplers;
        uint8_t padding;
    };

    static_assert(sizeof(ShaderLinkingBody) == 48);

    struct CompiledResult;

    struct InputBinding
    {
        uint32_t inputIndex;
        bool isSubgraphOutput;
        uint8_t padding[3];
    };

    static_assert(sizeof(InputBinding) == 8);

    struct CompiledSubgraph
    {
        uint32_t flags;
        uint16_t linkingArgType;
        uint16_t padding0;
        void* shaderArgumentBegin;
        void* shaderArgumentEnd;
        void* shaderArgumentCapacity;
        void* shaderSource;
        void* constantBufferUpdaterBegin;
        void* constantBufferUpdaterEnd;
        void* constantBufferUpdaterCapacity;
        void* constantBufferInitialBegin;
        void* constantBufferInitialEnd;
        void* constantBufferInitialCapacity;
        void* surfaceDataBegin;
        void* surfaceDataEnd;
        void* surfaceDataCapacity;
        void* inputBindingBegin;
        void* inputBindingEnd;
        void* inputBindingCapacity;
    };

    static_assert(sizeof(CompiledSubgraph) == 136);
    static_assert(offsetof(CompiledSubgraph, constantBufferUpdaterBegin) == 40);
    static_assert(offsetof(CompiledSubgraph, constantBufferInitialBegin) == 64);
    static_assert(offsetof(CompiledSubgraph, inputBindingBegin) == 112);

    struct CompiledResult
    {
        void** vtable;
        volatile long refCount;
        uint32_t padding;
        CompiledSubgraph* subgraphBegin;
        CompiledSubgraph* subgraphEnd;
        CompiledSubgraph* subgraphCapacity;
    };

    static_assert(sizeof(CompiledResult) == 40);
    static_assert(offsetof(CompiledResult, subgraphBegin) == 16);

    ULONG AddRef(volatile long* refCount)
    {
        return static_cast<ULONG>(InterlockedIncrement(refCount));
    }

    ULONG ReleaseRef(volatile long* refCount)
    {
        return static_cast<ULONG>(InterlockedDecrement(refCount));
    }

    ULONG __fastcall Wrapper_AddRef(CompiledResult* self)
    {
        return AddRef(&self->refCount);
    }

    void DestroyCompiledResult(CompiledResult* self)
    {
        if (self->subgraphBegin)
        {
            for (auto* subgraph = self->subgraphBegin; subgraph != self->subgraphEnd; ++subgraph)
            {
                if (subgraph->inputBindingBegin)
                {
                    HeapFree(GetProcessHeap(), 0, subgraph->inputBindingBegin);
                }

                if (subgraph->shaderArgumentBegin)
                {
                    HeapFree(GetProcessHeap(), 0, subgraph->shaderArgumentBegin);
                }
            }

            HeapFree(GetProcessHeap(), 0, self->subgraphBegin);
        }

        HeapFree(GetProcessHeap(), 0, self);
    }

    ULONG __fastcall Wrapper_Release(CompiledResult* self)
    {
        auto const ref = ReleaseRef(&self->refCount);
        if (ref == 0)
        {
            DestroyCompiledResult(self);
        }

        return ref;
    }

    uint32_t __fastcall Wrapper_GetSubgraphCount(CompiledResult*)
    {
        return 1;
    }

    ShaderLinkingBody* __fastcall Wrapper_GetSubgraphShaderLinkingBody(
        CompiledResult* self,
        ShaderLinkingBody* body,
        uint32_t)
    {
        EnsureShader();

        auto* subgraph = self->subgraphBegin;
        auto const argCount = subgraph && subgraph->shaderArgumentBegin && subgraph->shaderArgumentEnd
            ? static_cast<uint64_t>(
                (static_cast<uint8_t*>(subgraph->shaderArgumentEnd) -
                    static_cast<uint8_t*>(subgraph->shaderArgumentBegin)) /
                sizeof(uint16_t))
            : 1;

        // This blur intentionally diverges from the earlier color-transform PoC:
        // 0x0100 is the input UV passed to PSBody, while linkingArgType 0x0200 marks the
        // body as a DWM custom sampler output. That mirrors CCustomKernelEffect's status
        // in the linker and lets dwmcorei bind texture0/sampler0 for our shader instead
        // of pre-sampling the backdrop into a single float4 color argument.
        body->argCount = argCount;
        body->argData = subgraph && subgraph->shaderArgumentBegin
            ? subgraph->shaderArgumentBegin
            : &kBackdropUvArgument;
        body->bytecodeSize = g_shaderBlob->GetBufferSize();
        body->bytecodeData = g_shaderBlob->GetBufferPointer();
        body->functionName = "PSBody";
        body->constantBufferSize = 0;
        body->linkingArgType = kBackdropCustomSamplerResult;
        body->hasCustomSamplers = 1;
        body->padding = 0;
        return body;
    }

    uint32_t __fastcall Wrapper_GetSubgraphInputCount(CompiledResult*, uint32_t)
    {
        return 1;
    }

    uint32_t __fastcall Wrapper_GetSubgraphFlags(CompiledResult*, uint32_t)
    {
        return 0;
    }

    uint32_t __fastcall Wrapper_GetInputMapping(
        CompiledResult*,
        uint32_t,
        uint32_t inputIndex,
        bool* isSubgraphOutput)
    {
        if (isSubgraphOutput)
        {
            *isSubgraphOutput = false;
        }

        return inputIndex;
    }

    bool __fastcall Wrapper_IsUVClampingRequired(
        CompiledResult*,
        uint32_t,
        uint32_t,
        uint32_t* horizontalMode,
        uint32_t* verticalMode)
    {
        if (horizontalMode)
        {
            *horizontalMode = 0;
        }

        if (verticalMode)
        {
            *verticalMode = 0;
        }

        return false;
    }

    bool __fastcall Wrapper_IsSamplerDataExtRequired(CompiledResult*, uint32_t, uint32_t)
    {
        return false;
    }

    uint32_t __fastcall Wrapper_GetConstantBufferSize(CompiledResult*, uint32_t)
    {
        return 0;
    }

    void const* __fastcall Wrapper_GetConstantBufferInitialValue(CompiledResult*, uint32_t)
    {
        return nullptr;
    }

    void* __fastcall Wrapper_ScalarDeletingDestructor(CompiledResult* self, uint32_t flags)
    {
        if ((flags & 1) != 0)
        {
            DestroyCompiledResult(self);
        }

        return self;
    }

    void __fastcall Wrapper_FinalRelease(CompiledResult*)
    {
    }

    // CompileEffectDescription must return the CompiledEffect-shaped object directly.
    // dwmcorei!Compile_WorkerThread already wraps that returned pointer in its own task
    // result object, and GetCompiledEffectNoRef returns the pointer stored in that DWM
    // wrapper at +0x20. Returning another app-defined outer wrapper here makes
    // wuceffectsi!EffectInstance read that wrapper's +0x10/+0x18 as an empty subgraph
    // vector, so this intentionally diverges from the earlier v3 note's extra-wrapper
    // wording for this WinUI3 build.
    void* g_wrapperVtable[] = {
        reinterpret_cast<void*>(Wrapper_AddRef),
        reinterpret_cast<void*>(Wrapper_Release),
        reinterpret_cast<void*>(Wrapper_GetSubgraphCount),
        reinterpret_cast<void*>(Wrapper_GetSubgraphShaderLinkingBody),
        reinterpret_cast<void*>(Wrapper_GetSubgraphInputCount),
        reinterpret_cast<void*>(Wrapper_GetSubgraphFlags),
        reinterpret_cast<void*>(Wrapper_GetInputMapping),
        reinterpret_cast<void*>(Wrapper_IsUVClampingRequired),
        reinterpret_cast<void*>(Wrapper_IsSamplerDataExtRequired),
        reinterpret_cast<void*>(Wrapper_GetConstantBufferSize),
        reinterpret_cast<void*>(Wrapper_GetConstantBufferInitialValue),
        reinterpret_cast<void*>(Wrapper_ScalarDeletingDestructor),
        reinterpret_cast<void*>(Wrapper_FinalRelease),
    };

    void* CreateCompiledResult()
    {
        EnsureShader();

        auto* result = static_cast<CompiledResult*>(
            HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(CompiledResult)));
        check_pointer(result);

        result->vtable = g_wrapperVtable;
        result->refCount = 1;

        auto* subgraph = static_cast<CompiledSubgraph*>(
            HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(CompiledSubgraph)));
        check_pointer(subgraph);

        auto* inputBinding = static_cast<InputBinding*>(
            HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(InputBinding)));
        check_pointer(inputBinding);

        auto* shaderArgument = static_cast<uint16_t*>(
            HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(uint16_t)));
        check_pointer(shaderArgument);

        *shaderArgument = kBackdropUvArgument;
        inputBinding->inputIndex = 0;
        inputBinding->isSubgraphOutput = false;

        subgraph->flags = 0;
        subgraph->linkingArgType = kBackdropCustomSamplerResult;
        subgraph->shaderArgumentBegin = shaderArgument;
        subgraph->shaderArgumentEnd = shaderArgument + 1;
        subgraph->shaderArgumentCapacity = shaderArgument + 1;
        subgraph->inputBindingBegin = inputBinding;
        subgraph->inputBindingEnd = inputBinding + 1;
        subgraph->inputBindingCapacity = inputBinding + 1;

        result->subgraphBegin = subgraph;
        result->subgraphEnd = subgraph + 1;
        result->subgraphCapacity = subgraph + 1;

        return result;
    }

    using CompileEffectDescriptionFn = HRESULT(__fastcall*)(void*, void**);

    CompileEffectDescriptionFn g_originalCompileEffectDescription{};
    std::once_flag g_hookOnce;

    struct ImportPatch
    {
        char const* name;
        void* original;
        void* replacement;
    };

    bool GraphContainsCustomEffectType(void* description)
    {
        if (!description)
        {
            return false;
        }

        // CompileEffectDescription receives the IEffectDescriptionWithNames interface
        // pointer at FlattenedEffectGraph + 0x10, not the object base. Reversing the
        // export showed it subtracts 0x10 before invoking EffectGenerator::Compile, so
        // the detour must do the same when it inspects the node vector. This replaces the
        // earlier pending-counter workaround and keeps recognition tied to our private
        // EffectType identity.
        auto* graph = static_cast<uint8_t*>(description) - 0x10;
        auto* nodeBegin = *reinterpret_cast<void***>(graph + 0x30);
        auto* nodeEnd = *reinterpret_cast<void***>(graph + 0x38);
        auto const beginAddress = reinterpret_cast<uintptr_t>(nodeBegin);
        auto const endAddress = reinterpret_cast<uintptr_t>(nodeEnd);
        if (!nodeBegin || !nodeEnd || endAddress < beginAddress)
        {
            return false;
        }

        auto const nodeBytes = endAddress - beginAddress;
        if ((nodeBytes % sizeof(void*)) != 0)
        {
            return false;
        }

        auto const nodeCount = nodeBytes / sizeof(void*);
        if (nodeCount > 0x19)
        {
            return false;
        }

        for (auto** current = nodeBegin; current != nodeEnd; ++current)
        {
            auto* node = *current;
            if (node && *reinterpret_cast<void**>(node) == &g_customEffectType)
            {
                return true;
            }
        }

        return false;
    }

    HRESULT __fastcall DetourCompileEffectDescription(void* description, void** result)
    {
        if (!result)
        {
            return E_POINTER;
        }

        if (GraphContainsCustomEffectType(description))
        {
            try
            {
                *result = CreateCompiledResult();
                return S_OK;
            }
            catch (...)
            {
                *result = nullptr;
                return to_hresult();
            }
        }

        return g_originalCompileEffectDescription(description, result);
    }

    bool IsTargetImport(char const* dllName)
    {
        return dllName && _stricmp(dllName, "wuceffectsi.dll") == 0;
    }

    void PatchSlot(void** slot, void* replacement)
    {
        DWORD oldProtect{};
        if (VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &oldProtect))
        {
            *slot = replacement;
            FlushInstructionCache(GetCurrentProcess(), slot, sizeof(void*));
            DWORD unused{};
            VirtualProtect(slot, sizeof(void*), oldProtect, &unused);
        }
    }

    void PatchImport(HMODULE module, ImportPatch const* patches, size_t patchCount)
    {
        auto* base = reinterpret_cast<uint8_t*>(module);
        auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        {
            return;
        }

        auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE)
        {
            return;
        }

        auto const& imports = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (!imports.VirtualAddress || !imports.Size)
        {
            return;
        }

        auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + imports.VirtualAddress);
        for (; descriptor->Name; ++descriptor)
        {
            if (!IsTargetImport(reinterpret_cast<char const*>(base + descriptor->Name)))
            {
                continue;
            }

            auto* thunk = reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->FirstThunk);
            for (; thunk->u1.Function; ++thunk)
            {
                auto** slot = reinterpret_cast<void**>(&thunk->u1.Function);
                for (size_t index = 0; index < patchCount; ++index)
                {
                    if (*slot == patches[index].original)
                    {
                        PatchSlot(slot, patches[index].replacement);
                        break;
                    }
                }
            }
        }
    }

    bool IsImportByOrdinal(IMAGE_THUNK_DATA const& thunk)
    {
#ifdef _WIN64
        return IMAGE_SNAP_BY_ORDINAL64(thunk.u1.Ordinal);
#else
        return IMAGE_SNAP_BY_ORDINAL32(thunk.u1.Ordinal);
#endif
    }

    void PatchDelayImport(HMODULE module, ImportPatch const* patches, size_t patchCount)
    {
        auto* base = reinterpret_cast<uint8_t*>(module);
        auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        {
            return;
        }

        auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE)
        {
            return;
        }

        auto const& delayImports = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT];
        if (!delayImports.VirtualAddress || !delayImports.Size)
        {
            return;
        }

        auto* descriptor = reinterpret_cast<IMAGE_DELAYLOAD_DESCRIPTOR*>(base + delayImports.VirtualAddress);
        for (; descriptor->DllNameRVA; ++descriptor)
        {
            if (!descriptor->Attributes.RvaBased)
            {
                continue;
            }

            if (!IsTargetImport(reinterpret_cast<char const*>(base + descriptor->DllNameRVA)))
            {
                continue;
            }

            // dcompi.dll delay-loads wuceffectsi.dll, so its calls are routed through the
            // .didat delay IAT instead of the normal import directory. Matching by import
            // name here is intentional: before delay resolution the slot is a helper thunk,
            // not GetProcAddress(wuceffectsi, name), so the old address comparison never
            // reached VirtualProtect.
            auto* nameThunk = reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->ImportNameTableRVA);
            auto* addressThunk = reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->ImportAddressTableRVA);
            for (; nameThunk->u1.AddressOfData && addressThunk->u1.Function; ++nameThunk, ++addressThunk)
            {
                if (IsImportByOrdinal(*nameThunk))
                {
                    continue;
                }

                auto const* importByName = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + nameThunk->u1.AddressOfData);
                auto const* importName = reinterpret_cast<char const*>(importByName->Name);
                for (size_t index = 0; index < patchCount; ++index)
                {
                    if (strcmp(importName, patches[index].name) == 0)
                    {
                        auto** slot = reinterpret_cast<void**>(&addressThunk->u1.Function);
                        PatchSlot(slot, patches[index].replacement);
                        break;
                    }
                }
            }
        }
    }

    void InstallHook()
    {
        std::call_once(g_hookOnce, []
        {
            EnsureShader();

            LoadLibraryW(L"dwmcorei.dll");
            auto module = LoadLibraryW(L"wuceffectsi.dll");
            check_pointer(module);

            auto original = reinterpret_cast<CompileEffectDescriptionFn>(
                GetProcAddress(module, "CompileEffectDescription"));
            check_pointer(original);

            g_originalCompileEffectDescription = original;

            InitializeCustomEffectType(module);
            PatchEffectTypeFromGuid(module);

            ImportPatch const patches[] = {
                {
                    "CompileEffectDescription",
                    reinterpret_cast<void*>(original),
                    reinterpret_cast<void*>(DetourCompileEffectDescription),
                },
            };

            auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
            if (snapshot != INVALID_HANDLE_VALUE)
            {
                MODULEENTRY32W entry{};
                entry.dwSize = sizeof(entry);
                if (Module32FirstW(snapshot, &entry))
                {
                    do
                    {
                        PatchImport(entry.hModule, patches, ARRAYSIZE(patches));
                        PatchDelayImport(entry.hModule, patches, ARRAYSIZE(patches));
                    } while (Module32NextW(snapshot, &entry));
                }

                CloseHandle(snapshot);
            }

            PatchImport(GetModuleHandleW(nullptr), patches, ARRAYSIZE(patches));
            PatchDelayImport(GetModuleHandleW(nullptr), patches, ARRAYSIZE(patches));
        });
    }
}

namespace CustomBlurEffect
{
    CompositionEffectBrush CreateBackdropBrush(Compositor const& compositor)
    {
        // Keep this code-only inside the WinUI3 project instead of adding a custom WinMain:
        // Windows App SDK initialization and the generated XAML entry point stay untouched,
        // while this still runs before CreateEffectFactory enters dwmcorei/wuceffectsi.
        InstallHook();

        auto effect = make<BlurEffect>();
        auto factory = compositor.CreateEffectFactory(effect);
        auto brush = factory.CreateBrush();
        brush.SetSourceParameter(L"Backdrop", compositor.CreateBackdropBrush());
        return brush;
    }
}
