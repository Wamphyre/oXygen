#pragma once
#include <JuceHeader.h>

namespace oxygen
{
    namespace Theme
    {
        // Color Palette (Material Design 3 inspired Dark Theme)
        namespace Colors
        {
            const juce::Colour Background    = juce::Colour::fromString("FF121212"); // Surface
            const juce::Colour SurfaceVariant= juce::Colour::fromString("FF1E1E1E"); // Slightly lighter background for modules
            const juce::Colour OnSurface     = juce::Colour::fromString("FFE0E0E0"); // Text
            const juce::Colour OnSurfaceVariant = juce::Colour::fromString("FFAAAAAA"); // Secondary Text
            
            const juce::Colour Primary       = juce::Colour::fromString("FF00FFFF"); // Cyan Neon (Brand)
            const juce::Colour Secondary     = juce::Colour::fromString("FFFF00FF"); // Magenta Neon (Brand)
            
            const juce::Colour Error         = juce::Colour::fromString("FFCF6679");
            
            const juce::Colour ButtonFill    = SurfaceVariant.brighter(0.1f);
            const juce::Colour ButtonHover   = ButtonFill.brighter(0.1f);
            const juce::Colour ButtonPress   = ButtonFill.darker(0.1f);
        }

        // Typography
        namespace Fonts
        {
            static const juce::Font getHeading() { return juce::Font("Outfit", 24.0f, juce::Font::bold); }
            static const juce::Font getSubheading() { return juce::Font("Outfit", 18.0f, juce::Font::plain); }
            static const juce::Font getBody() { return juce::Font("Unbounded", 14.0f, juce::Font::plain); }
        }

        // Layout Constants
        namespace Dimens
        {
            constexpr float CornerRadius = 12.0f;
            constexpr float SmallCornerRadius = 6.0f;
            constexpr float Padding = 10.0f;
            constexpr float Margin = 5.0f;
        }
    }
}
