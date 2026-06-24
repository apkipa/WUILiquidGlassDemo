#pragma once

namespace CustomEffectRuntime
{
    enum class SourceKind
    {
        Backdrop,
    };

    struct SourceDescriptor
    {
        wchar_t const* name;
        SourceKind kind;
    };

    struct PropertyDescriptor
    {
        wchar_t const* publicName;
        uint32_t index;
        ABI::Windows::Graphics::Effects::GRAPHICS_EFFECT_PROPERTY_MAPPING mapping;
        HRESULT (*getDefaultValue)(ABI::Windows::Foundation::IPropertyValue** value);
    };

    struct CustomEffectDefinition
    {
        GUID id;
        wchar_t const* effectName;
        char const* fragmentName;

        char const* shaderSource;
        size_t shaderSourceSize;
        char const* shaderFunctionName;

        SourceDescriptor const* sources;
        uint32_t sourceCount;

        PropertyDescriptor const* properties;
        uint32_t propertyCount;
        void const* nativePropertyMetadata;
        uint32_t nativePropertyMetadataCount;
        uint32_t propertiesStructSize;

        uint16_t const* shaderArguments;
        uint64_t shaderArgumentCount;
        uint16_t linkingArgType;
        bool hasCustomSamplers;

        uint32_t constantBufferSize;
        void const* constantBufferInitialValue;
    };

    void RegisterEffect(CustomEffectDefinition const& definition);

    winrt::Microsoft::UI::Composition::CompositionEffectBrush CreateBackdropBrush(
        winrt::Microsoft::UI::Composition::Compositor const& compositor,
        CustomEffectDefinition const& definition);
}
