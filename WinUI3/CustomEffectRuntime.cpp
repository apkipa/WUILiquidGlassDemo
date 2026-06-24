#include "pch.h"
#include "CustomEffectRuntime.h"

using namespace winrt;
using namespace Microsoft::UI::Composition;
using namespace Windows::Graphics::Effects;

namespace
{
    constexpr uintptr_t kEffectTypeFromGuidRva = 0x17c48;
    constexpr uintptr_t kEffectTypeTableRva = 0x62150;
    constexpr uintptr_t kEffectTypeGetBoundsRva = 0x1e040;
    constexpr uintptr_t kEffectTypeCalcInputBoundsRva = 0x1d700;
    constexpr size_t kEffectTypeCount = 0x1f;
    // Reverse engineering shows EffectType virtual calls stop at slot 21
    // (+0xa8, GetEffectOpacityRelation) in this WinAppSDK build. Slot 22+
    // is not part of the callable ABI we need to model for private GUIDs.
    constexpr size_t kEffectTypeVtableSlotCount = 22;
    constexpr size_t kFromGuidPatchSize = 15;

    struct RuntimeEffectEntry;

    struct RuntimeEffectType
    {
        void** vtable;
        RuntimeEffectEntry* entry;
    };

    struct RuntimeEffectEntry
    {
        explicit RuntimeEffectEntry(CustomEffectRuntime::CustomEffectDefinition const& value) :
            definition(&value)
        {
            effectType.vtable = effectTypeVtable;
            effectType.entry = this;
        }

        CustomEffectRuntime::CustomEffectDefinition const* definition{};
        winrt::com_ptr<ID3DBlob> shaderBlob;
        std::once_flag shaderOnce;
        void* effectTypeVtable[kEffectTypeVtableSlotCount]{};
        RuntimeEffectType effectType{};
        RuntimeEffectEntry* next{};
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

        RuntimeEffectEntry* entry;
    };

    static_assert(offsetof(CompiledResult, subgraphBegin) == 16);
    static_assert(offsetof(CompiledResult, entry) == 40);

    struct ImportPatch
    {
        char const* name;
        void* original;
        void* replacement;
    };

    RuntimeEffectEntry* g_effects{};
    std::mutex g_registryMutex;
    HMODULE g_wuceffectsiModule{};
    std::once_flag g_hookOnce;

    using CompileEffectDescriptionFn = HRESULT(__fastcall*)(void*, void**);
    CompileEffectDescriptionFn g_originalCompileEffectDescription{};

    bool SameGuid(GUID const& left, GUID const& right)
    {
        return left.Data1 == right.Data1 &&
            left.Data2 == right.Data2 &&
            left.Data3 == right.Data3 &&
            memcmp(left.Data4, right.Data4, sizeof(left.Data4)) == 0;
    }

    RuntimeEffectEntry* FindEntryByGuidLocked(GUID const& id)
    {
        for (auto* entry = g_effects; entry; entry = entry->next)
        {
            if (SameGuid(entry->definition->id, id))
            {
                return entry;
            }
        }

        return nullptr;
    }

    RuntimeEffectEntry* FindEntryByEffectTypeLocked(void* effectType)
    {
        for (auto* entry = g_effects; entry; entry = entry->next)
        {
            if (effectType == &entry->effectType)
            {
                return entry;
            }
        }

        return nullptr;
    }

    void EnsureShader(RuntimeEffectEntry* entry)
    {
        std::call_once(entry->shaderOnce, [entry]
        {
            // wuceffectsi!EffectGenerator::BuildCompiledEffectSubgraph does not compile
            // generated effects as lib_5_0. This WinUI3 build passes
            // "lib_4_0_level_9_3_ps_only" and flags 0x8800
            // (STRICTNESS | OPTIMIZATION_LEVEL3) to D3DCompile, then dwmcorei links the
            // library into ps_4_0/ps_4_0_level_9_x. Matching that profile is intentional:
            // D3DLoadModule/CreateInstance accepts a lib_5_0 blob, but the final DWM
            // ID3D11Linker::Link path rejects the mixed-profile graph with E_FAIL.
            UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;

            auto const& definition = *entry->definition;
            winrt::com_ptr<ID3DBlob> errors;
            check_hresult(D3DCompile(
                definition.shaderSource,
                definition.shaderSourceSize,
                nullptr,
                nullptr,
                nullptr,
                // dwmcorei!LoadShaderBody consumes this blob with D3DLoadModule and then
                // CreateInstance(functionName). A standalone ps_4_0 blob compiles locally
                // but is not a loadable shader-linking module, so this runtime keeps the
                // code-only path aligned with wuceffectsi's generated-effect profile.
                nullptr,
                "lib_4_0_level_9_3_ps_only",
                flags,
                0,
                entry->shaderBlob.put(),
                errors.put()));
        });
    }

    char const* __fastcall EffectType_GetShaderFragmentName(RuntimeEffectType* self)
    {
        return self->entry->definition->fragmentName;
    }

    GUID const* __fastcall EffectType_GetGuid(RuntimeEffectType* self)
    {
        return &self->entry->definition->id;
    }

    bool __fastcall EffectType_IsValidInputCount(RuntimeEffectType* self, uint32_t sourceCount)
    {
        return sourceCount == self->entry->definition->sourceCount;
    }

    bool __fastcall EffectType_IsValidInputType(RuntimeEffectType*, uint32_t inputType)
    {
        return inputType != 0;
    }

    uint32_t __fastcall EffectType_GetPropertiesStructSize(RuntimeEffectType* self)
    {
        return self->entry->definition->propertiesStructSize;
    }

    uint32_t __fastcall EffectType_GetEffectSamplingBehavior(RuntimeEffectType*)
    {
        return 0;
    }

    bool __fastcall EffectType_ReturnFalse(RuntimeEffectType*)
    {
        return false;
    }

    bool __fastcall EffectType_ReturnTrue(RuntimeEffectType*)
    {
        return true;
    }

    bool __fastcall EffectType_IsInputTransform(RuntimeEffectType*, uint32_t* mode)
    {
        if (mode)
        {
            *mode = 0;
        }

        return false;
    }

    bool __fastcall EffectType_IsIntersectionCombinator(RuntimeEffectType*, void const*)
    {
        return false;
    }

    bool __fastcall EffectType_IsNoOp(RuntimeEffectType*, uint32_t, void const*)
    {
        return false;
    }

    uint32_t __fastcall EffectType_GetEffectOpacityRelation(RuntimeEffectType*, void const*)
    {
        return 0;
    }

    void __fastcall EffectType_GetPropertiesMetadata(
        RuntimeEffectType* self,
        uint32_t* count,
        void const** metadata)
    {
        auto const& definition = *self->entry->definition;
        if (count)
        {
            *count = definition.nativePropertyMetadataCount;
        }

        if (metadata)
        {
            *metadata = definition.nativePropertyMetadata;
        }
    }

    void __fastcall EffectType_Validate(RuntimeEffectType*, void const*)
    {
    }

    void __fastcall EffectType_GenerateCode(RuntimeEffectType*, void const*, void*, char const*)
    {
        // CompileEffectDescription detects runtime EffectType objects and returns a
        // CompiledEffect-shaped object directly. This no-op is a defensive slot so an
        // accidental fallback does not run a built-in GenerateCode path and hide whether
        // our private shader path ran.
    }

    void InitializeEffectType(RuntimeEffectEntry* entry, HMODULE wuceffectsi)
    {
        auto const base = reinterpret_cast<uint8_t*>(wuceffectsi);
        auto* vtable = entry->effectTypeVtable;

        std::fill(vtable, vtable + kEffectTypeVtableSlotCount, nullptr);

        // EffectType slots are not COM methods, and several folded tiny functions have
        // different meanings depending on their slot. These 22 entries cover every
        // observed EffectType virtual call from traversal, flattening, hashing, opacity
        // propagation, and generator code in this wuceffectsi build. The neutral native
        // bounds helpers are reused because their ABI includes struct-return details.
        vtable[0] = reinterpret_cast<void*>(EffectType_GetShaderFragmentName);
        vtable[1] = reinterpret_cast<void*>(EffectType_GetGuid);
        vtable[2] = reinterpret_cast<void*>(EffectType_GetEffectSamplingBehavior);
        vtable[3] = reinterpret_cast<void*>(EffectType_IsValidInputCount);
        vtable[4] = reinterpret_cast<void*>(EffectType_IsValidInputType);
        vtable[5] = reinterpret_cast<void*>(EffectType_ReturnFalse);
        vtable[6] = reinterpret_cast<void*>(EffectType_IsInputTransform);
        vtable[7] = reinterpret_cast<void*>(EffectType_ReturnFalse);
        vtable[8] = reinterpret_cast<void*>(EffectType_ReturnFalse);
        vtable[9] = reinterpret_cast<void*>(EffectType_ReturnFalse);
        vtable[10] = reinterpret_cast<void*>(EffectType_ReturnFalse);
        vtable[11] = reinterpret_cast<void*>(EffectType_ReturnFalse);
        vtable[12] = reinterpret_cast<void*>(EffectType_ReturnTrue);
        vtable[13] = reinterpret_cast<void*>(EffectType_IsIntersectionCombinator);
        vtable[14] = reinterpret_cast<void*>(EffectType_IsNoOp);
        vtable[15] = base + kEffectTypeGetBoundsRva;
        vtable[16] = base + kEffectTypeCalcInputBoundsRva;
        vtable[17] = reinterpret_cast<void*>(EffectType_GetPropertiesStructSize);
        vtable[18] = reinterpret_cast<void*>(EffectType_GetPropertiesMetadata);
        vtable[19] = reinterpret_cast<void*>(EffectType_Validate);
        vtable[20] = reinterpret_cast<void*>(EffectType_GenerateCode);
        vtable[21] = reinterpret_cast<void*>(EffectType_GetEffectOpacityRelation);
    }

    void InitializeAllEffectTypes(HMODULE wuceffectsi)
    {
        std::lock_guard<std::mutex> guard(g_registryMutex);
        for (auto* entry = g_effects; entry; entry = entry->next)
        {
            InitializeEffectType(entry, wuceffectsi);
        }
    }

    void* __fastcall DetourEffectTypeFromGuid(GUID const* guid)
    {
        if (guid)
        {
            std::lock_guard<std::mutex> guard(g_registryMutex);
            if (auto* entry = FindEntryByGuidLocked(*guid))
            {
                return &entry->effectType;
            }
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

                if (subgraph->constantBufferInitialBegin)
                {
                    HeapFree(GetProcessHeap(), 0, subgraph->constantBufferInitialBegin);
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
        EnsureShader(self->entry);

        auto const& definition = *self->entry->definition;
        auto* subgraph = self->subgraphBegin;
        auto const argCount = subgraph && subgraph->shaderArgumentBegin && subgraph->shaderArgumentEnd
            ? static_cast<uint64_t>(
                (static_cast<uint8_t*>(subgraph->shaderArgumentEnd) -
                    static_cast<uint8_t*>(subgraph->shaderArgumentBegin)) /
                sizeof(uint16_t))
            : definition.shaderArgumentCount;

        body->argCount = argCount;
        body->argData = subgraph && subgraph->shaderArgumentBegin
            ? subgraph->shaderArgumentBegin
            : definition.shaderArguments;
        body->bytecodeSize = self->entry->shaderBlob->GetBufferSize();
        body->bytecodeData = self->entry->shaderBlob->GetBufferPointer();
        body->functionName = definition.shaderFunctionName;
        body->constantBufferSize = definition.constantBufferSize;
        body->linkingArgType = definition.linkingArgType;
        body->hasCustomSamplers = definition.hasCustomSamplers ? 1 : 0;
        body->padding = 0;
        return body;
    }

    uint32_t __fastcall Wrapper_GetSubgraphInputCount(CompiledResult* self, uint32_t)
    {
        return self->entry->definition->sourceCount;
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

    uint32_t __fastcall Wrapper_GetConstantBufferSize(CompiledResult* self, uint32_t)
    {
        return self->entry->definition->constantBufferSize;
    }

    void const* __fastcall Wrapper_GetConstantBufferInitialValue(CompiledResult* self, uint32_t)
    {
        auto* subgraph = self->subgraphBegin;
        if (subgraph && subgraph->constantBufferInitialBegin)
        {
            return subgraph->constantBufferInitialBegin;
        }

        return self->entry->definition->constantBufferInitialValue;
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

    void* AllocateBytes(size_t size)
    {
        if (!size)
        {
            return nullptr;
        }

        auto* memory = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
        check_pointer(memory);
        return memory;
    }

    void* CreateCompiledResult(RuntimeEffectEntry* entry)
    {
        EnsureShader(entry);

        CompiledResult* result{};
        try
        {
            auto const& definition = *entry->definition;
            result = static_cast<CompiledResult*>(
                HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(CompiledResult)));
            check_pointer(result);

            result->vtable = g_wrapperVtable;
            result->refCount = 1;
            result->entry = entry;

            auto* subgraph = static_cast<CompiledSubgraph*>(
                HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(CompiledSubgraph)));
            check_pointer(subgraph);

            result->subgraphBegin = subgraph;
            result->subgraphEnd = subgraph + 1;
            result->subgraphCapacity = subgraph + 1;

            if (definition.sourceCount)
            {
                auto* inputBindings = static_cast<InputBinding*>(
                    AllocateBytes(sizeof(InputBinding) * definition.sourceCount));
                for (uint32_t index = 0; index < definition.sourceCount; ++index)
                {
                    inputBindings[index].inputIndex = index;
                    inputBindings[index].isSubgraphOutput = false;
                }

                subgraph->inputBindingBegin = inputBindings;
                subgraph->inputBindingEnd = inputBindings + definition.sourceCount;
                subgraph->inputBindingCapacity = inputBindings + definition.sourceCount;
            }

            if (definition.shaderArgumentCount)
            {
                auto* shaderArguments = static_cast<uint16_t*>(
                    AllocateBytes(sizeof(uint16_t) * static_cast<size_t>(definition.shaderArgumentCount)));
                memcpy(
                    shaderArguments,
                    definition.shaderArguments,
                    sizeof(uint16_t) * static_cast<size_t>(definition.shaderArgumentCount));

                subgraph->shaderArgumentBegin = shaderArguments;
                subgraph->shaderArgumentEnd = shaderArguments + definition.shaderArgumentCount;
                subgraph->shaderArgumentCapacity = shaderArguments + definition.shaderArgumentCount;
            }

            if (definition.constantBufferSize)
            {
                auto* constantBuffer = static_cast<uint8_t*>(
                    AllocateBytes(definition.constantBufferSize));
                if (definition.constantBufferInitialValue)
                {
                    memcpy(
                        constantBuffer,
                        definition.constantBufferInitialValue,
                        definition.constantBufferSize);
                }

                subgraph->constantBufferInitialBegin = constantBuffer;
                subgraph->constantBufferInitialEnd = constantBuffer + definition.constantBufferSize;
                subgraph->constantBufferInitialCapacity = constantBuffer + definition.constantBufferSize;
            }

            subgraph->flags = 0;
            subgraph->linkingArgType = definition.linkingArgType;
            return result;
        }
        catch (...)
        {
            if (result)
            {
                DestroyCompiledResult(result);
            }

            throw;
        }
    }

    RuntimeEffectEntry* FindEntryInGraph(void* description)
    {
        if (!description)
        {
            return nullptr;
        }

        // CompileEffectDescription receives the IEffectDescriptionWithNames interface
        // pointer at FlattenedEffectGraph + 0x10, not the object base. Reversing the
        // export showed it subtracts 0x10 before invoking EffectGenerator::Compile, so
        // the detour must do the same when it inspects the node vector.
        auto* graph = static_cast<uint8_t*>(description) - 0x10;
        auto* nodeBegin = *reinterpret_cast<void***>(graph + 0x30);
        auto* nodeEnd = *reinterpret_cast<void***>(graph + 0x38);
        auto const beginAddress = reinterpret_cast<uintptr_t>(nodeBegin);
        auto const endAddress = reinterpret_cast<uintptr_t>(nodeEnd);
        if (!nodeBegin || !nodeEnd || endAddress < beginAddress)
        {
            return nullptr;
        }

        auto const nodeBytes = endAddress - beginAddress;
        if ((nodeBytes % sizeof(void*)) != 0)
        {
            return nullptr;
        }

        auto const nodeCount = nodeBytes / sizeof(void*);
        if (nodeCount > 0x19)
        {
            return nullptr;
        }

        std::lock_guard<std::mutex> guard(g_registryMutex);
        for (auto** current = nodeBegin; current != nodeEnd; ++current)
        {
            auto* node = *current;
            if (!node)
            {
                continue;
            }

            if (auto* entry = FindEntryByEffectTypeLocked(*reinterpret_cast<void**>(node)))
            {
                return entry;
            }
        }

        return nullptr;
    }

    HRESULT __fastcall DetourCompileEffectDescription(void* description, void** result)
    {
        if (!result)
        {
            return E_POINTER;
        }

        if (auto* entry = FindEntryInGraph(description))
        {
            try
            {
                *result = CreateCompiledResult(entry);
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
            // not GetProcAddress(wuceffectsi, name), so address comparison never reaches
            // VirtualProtect.
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
            LoadLibraryW(L"dwmcorei.dll");
            auto module = LoadLibraryW(L"wuceffectsi.dll");
            check_pointer(module);

            auto original = reinterpret_cast<CompileEffectDescriptionFn>(
                GetProcAddress(module, "CompileEffectDescription"));
            check_pointer(original);

            g_originalCompileEffectDescription = original;
            g_wuceffectsiModule = module;

            InitializeAllEffectTypes(module);
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

    struct RuntimeGraphicsEffect :
        winrt::implements<
            RuntimeGraphicsEffect,
            IGraphicsEffect,
            IGraphicsEffectSource,
            ABI::Windows::Graphics::Effects::IGraphicsEffectD2D1Interop>
    {
        explicit RuntimeGraphicsEffect(CustomEffectRuntime::CustomEffectDefinition const* definition) :
            m_definition(definition),
            m_name(definition->effectName)
        {
        }

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
            // compile hook runs. The runtime detour registers real EffectType objects for
            // every private GUID so custom effects do not masquerade as built-in D2D IDs.
            *id = m_definition->id;
            return S_OK;
        }

        HRESULT __stdcall GetNamedPropertyMapping(
            LPCWSTR name,
            UINT* index,
            ABI::Windows::Graphics::Effects::GRAPHICS_EFFECT_PROPERTY_MAPPING* mapping) noexcept final
        {
            if (!name || !index || !mapping)
            {
                return E_POINTER;
            }

            for (uint32_t propertyIndex = 0; propertyIndex < m_definition->propertyCount; ++propertyIndex)
            {
                auto const& property = m_definition->properties[propertyIndex];
                if (property.publicName && wcscmp(name, property.publicName) == 0)
                {
                    *index = property.index;
                    *mapping = property.mapping;
                    return S_OK;
                }
            }

            return E_INVALIDARG;
        }

        HRESULT __stdcall GetPropertyCount(UINT* count) noexcept final
        {
            if (!count)
            {
                return E_POINTER;
            }

            *count = m_definition->propertyCount;
            return S_OK;
        }

        HRESULT __stdcall GetProperty(UINT index, ABI::Windows::Foundation::IPropertyValue** value) noexcept final
        {
            if (!value)
            {
                return E_POINTER;
            }

            *value = nullptr;
            if (index >= m_definition->propertyCount)
            {
                return E_INVALIDARG;
            }

            auto const& property = m_definition->properties[index];
            if (!property.getDefaultValue)
            {
                return E_NOTIMPL;
            }

            return property.getDefaultValue(value);
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
            if (index >= m_definition->sourceCount)
            {
                return E_INVALIDARG;
            }

            try
            {
                auto const& sourceDefinition = m_definition->sources[index];
                auto parameter = CompositionEffectSourceParameter(sourceDefinition.name).as<IGraphicsEffectSource>();
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

            *count = m_definition->sourceCount;
            return S_OK;
        }

    private:
        CustomEffectRuntime::CustomEffectDefinition const* m_definition{};
        hstring m_name;
    };
}

namespace CustomEffectRuntime
{
    void RegisterEffect(CustomEffectDefinition const& definition)
    {
        std::lock_guard<std::mutex> guard(g_registryMutex);
        if (FindEntryByGuidLocked(definition.id))
        {
            return;
        }

        auto* entry = new RuntimeEffectEntry(definition);
        if (g_wuceffectsiModule)
        {
            InitializeEffectType(entry, g_wuceffectsiModule);
        }

        entry->next = g_effects;
        g_effects = entry;
    }

    IGraphicsEffect CreateEffect(CustomEffectDefinition const& definition)
    {
        // The public shape must be the same shape WinUI expects from built-in effects:
        // an IGraphicsEffect that can be passed directly to Compositor::CreateEffectFactory.
        // Registration and hook installation live here only to make that object usable
        // before wuceffectsi resolves its private EffectType GUID.
        RegisterEffect(definition);
        InstallHook();
        return make<RuntimeGraphicsEffect>(&definition);
    }

}
