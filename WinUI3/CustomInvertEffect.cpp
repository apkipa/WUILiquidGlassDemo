#include "pch.h"
#include "CustomInvertEffect.h"

using namespace winrt;
using namespace Microsoft::UI::Composition;
using namespace Windows::Foundation;
using namespace Windows::Graphics::Effects;

namespace
{
    wchar_t const kInvertEffectName[] = L"BackdropInvertEffect";

    constexpr GUID kD2DColorMatrixEffectId{
        0x921f03d6, 0x641c, 0x47df, { 0x85, 0x2d, 0xb4, 0xbb, 0x61, 0x53, 0xae, 0x11 } };

    constexpr float kIdentityColorMatrix[20] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
    };

    char const kInvertShader[] = R"(
export float4 PSBody(float4 sample0)
{
    return float4(1.0f - sample0.rgb, sample0.a);
}
)";

    constexpr uint16_t kBackdropSampleArgument = 0x0200;

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
                kInvertShader,
                sizeof(kInvertShader) - 1,
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

    struct InvertEffect :
        winrt::implements<
            InvertEffect,
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

            // The v3 write-up describes publishing a private effect id, but WinUI3 rejects
            // unknown ids while wuceffectsi!CreateEffectDescription is still traversing the
            // public graph, before CompileEffectDescription can be detoured. We report a
            // built-in single-input Direct2D effect id here so graph creation can finish.
            // We intentionally do not use D2D1Invert even though wuceffectsi supports it,
            // because that would make it impossible to tell whether the visible inversion
            // came from our custom shader or from the placeholder itself. ColorMatrix is a
            // neutral, supported placeholder whose payload is replaced by the compile detour.
            // This is why this code intentionally diverges from the scheme's private-GUID path.
            *id = kD2DColorMatrixEffectId;
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

            *count = 3;
            return S_OK;
        }

        HRESULT __stdcall GetProperty(UINT index, ABI::Windows::Foundation::IPropertyValue** value) noexcept final
        {
            if (!value)
            {
                return E_POINTER;
            }

            *value = nullptr;
            try
            {
                Windows::Foundation::IInspectable property{ nullptr };
                switch (index)
                {
                case 0:
                    property = PropertyValue::CreateSingleArray(kIdentityColorMatrix);
                    break;
                case 1:
                    // wuceffectsi's ColorMatrix metadata expects UInt32 here even though
                    // the SDK enum is normally written as a signed C++ enum. Returning Int32
                    // reaches VisitEffectProperty but fails its exact WinRT PropertyType check.
                    property = PropertyValue::CreateUInt32(1);
                    break;
                case 2:
                    property = PropertyValue::CreateBoolean(false);
                    break;
                default:
                    return E_INVALIDARG;
                }

                *value = reinterpret_cast<ABI::Windows::Foundation::IPropertyValue*>(
                    detach_abi(property.as<Windows::Foundation::IPropertyValue>()));
                return S_OK;
            }
            catch (...)
            {
                return to_hresult();
            }
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
        hstring m_name{ kInvertEffectName };
    };

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

        // Generated wuceffectsi shaders do not declare Texture2D/SamplerState globals for
        // color transforms. They receive sampled pixels through linking arguments instead:
        // 0x0200 means sampler input 0 after dwmcorei has appended the real texture sample
        // node. This deliberately diverges from the v3 note's simple Texture2D HLSL because
        // using globals with linkingArgType 0x100 made DWM append a second sample stage.
        // The generated shaders spell this as minfloat4 via Common.hlsl; this code-only
        // runtime compiler has no wuceffectsi include handler, so float4 keeps the same
        // four-component ABI while remaining directly compilable by D3DCompile.
        body->argCount = argCount;
        body->argData = subgraph && subgraph->shaderArgumentBegin
            ? subgraph->shaderArgumentBegin
            : &kBackdropSampleArgument;
        body->bytecodeSize = g_shaderBlob->GetBufferSize();
        body->bytecodeData = g_shaderBlob->GetBufferPointer();
        body->functionName = "PSBody";
        body->constantBufferSize = 0;
        body->linkingArgType = 0;
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

        *shaderArgument = kBackdropSampleArgument;
        inputBinding->inputIndex = 0;
        inputBinding->isSubgraphOutput = false;

        subgraph->flags = 0;
        subgraph->linkingArgType = 0;
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
    using CreateEffectDescriptionFn = HRESULT(__fastcall*)(void*, void*, void**);

    CreateEffectDescriptionFn g_originalCreateEffectDescription{};
    CompileEffectDescriptionFn g_originalCompileEffectDescription{};
    std::once_flag g_hookOnce;
    std::mutex g_descriptionMutex;
    std::unordered_set<void*> g_invertDescriptions;
    std::atomic_uint g_pendingInvertCompiles{};

    struct ImportPatch
    {
        char const* name;
        void* original;
        void* replacement;
    };

    bool ShouldCompileAsCustomInvert(void* description)
    {
        if (!description)
        {
            return false;
        }

        {
            std::lock_guard guard{ g_descriptionMutex };
            if (g_invertDescriptions.find(description) != g_invertDescriptions.end())
            {
                return true;
            }
        }

        // CreateEffectDescription and CompileEffectDescription do not receive the same
        // object identity in WinUI3: Create returns one interface pointer, then wuceffectsi
        // flattens/repackages that graph before compile. In this code-only demo the custom
        // effect factory is created immediately after our InvertEffect is recognized, so the
        // narrow association is to consume exactly the next compile request instead of
        // pretending the two internal pointers are comparable.
        auto pending = g_pendingInvertCompiles.load(std::memory_order_acquire);
        while (pending != 0)
        {
            if (g_pendingInvertCompiles.compare_exchange_weak(
                pending,
                pending - 1,
                std::memory_order_acq_rel,
                std::memory_order_acquire))
            {
                return true;
            }
        }

        return false;
    }

    bool IsOurEffect(void* effect)
    {
        if (!effect)
        {
            return false;
        }

        HSTRING name{};
        bool matches = false;
        auto const vtable = *reinterpret_cast<void***>(effect);
        auto const getName = reinterpret_cast<HRESULT(__fastcall*)(void*, HSTRING*)>(vtable[6]);
        if (SUCCEEDED(getName(effect, &name)) && name)
        {
            uint32_t length{};
            auto const value = WindowsGetStringRawBuffer(name, &length);
            matches = length == ARRAYSIZE(kInvertEffectName) - 1 &&
                wmemcmp(value, kInvertEffectName, length) == 0;
        }

        if (name)
        {
            WindowsDeleteString(name);
        }
        return matches;
    }

    HRESULT __fastcall DetourCreateEffectDescription(void* effect, void* animatableProperties, void** result)
    {
        if (!result)
        {
            return E_POINTER;
        }

        // This extra detour is deliberately earlier than the v3 document's compile hook.
        // WinUI3 validates the effect GUID while building the description, so an unknown
        // custom id fails with "Unsupported effect type" before CompileEffectDescription is
        // reached. Recording the description here gives the later compile detour a stable
        // way to recognize only this placeholder-backed custom effect.
        auto const isOurs = IsOurEffect(effect);
        auto const hr = g_originalCreateEffectDescription(effect, animatableProperties, result);
        if (SUCCEEDED(hr) && isOurs && *result)
        {
            std::lock_guard guard{ g_descriptionMutex };
            g_invertDescriptions.insert(*result);
            g_pendingInvertCompiles.fetch_add(1, std::memory_order_release);
        }

        return hr;
    }

    HRESULT __fastcall DetourCompileEffectDescription(void* description, void** result)
    {
        if (!result)
        {
            return E_POINTER;
        }

        if (ShouldCompileAsCustomInvert(description))
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
            auto originalCreate = reinterpret_cast<CreateEffectDescriptionFn>(
                GetProcAddress(module, "CreateEffectDescription"));
            check_pointer(originalCreate);

            g_originalCompileEffectDescription = original;
            g_originalCreateEffectDescription = originalCreate;

            ImportPatch const patches[] = {
                {
                    "CompileEffectDescription",
                    reinterpret_cast<void*>(original),
                    reinterpret_cast<void*>(DetourCompileEffectDescription),
                },
                {
                    "CreateEffectDescription",
                    reinterpret_cast<void*>(originalCreate),
                    reinterpret_cast<void*>(DetourCreateEffectDescription),
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

namespace CustomInvertEffect
{
    CompositionEffectBrush CreateBackdropBrush(Compositor const& compositor)
    {
        // Keep this code-only inside the WinUI3 project instead of adding a custom WinMain:
        // Windows App SDK initialization and the generated XAML entry point stay untouched,
        // while this still runs before CreateEffectFactory enters dwmcorei/wuceffectsi.
        InstallHook();

        auto effect = make<InvertEffect>();
        auto factory = compositor.CreateEffectFactory(effect);
        auto brush = factory.CreateBrush();
        brush.SetSourceParameter(L"Backdrop", compositor.CreateBackdropBrush());
        return brush;
    }
}
