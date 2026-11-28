#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ioline::branding
{
// Approved ioline mark, matching Assets/icon.svg. Parsed once for all paint calls.
inline void drawLogoMark (juce::Graphics& g, juce::Rectangle<float> bounds)
{
    static constexpr char svg[] = R"ioline(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 30 30" color="#a99bff" role="img" aria-label="sightline"><rect x=".6" y=".6" width="28.8" height="28.8" rx="8" fill="#26262a" stroke="#a99bff" stroke-opacity=".85" stroke-width="1.2"/><path d="M5.5 15C9 4 21 4 24.5 15C21 26 9 26 5.5 15Z" fill="none" stroke="#e8e8ec" stroke-width="1.6" stroke-linejoin="round"/><path d="M4 15h22" stroke="#a99bff" stroke-width="1.6" stroke-linecap="round"/><circle cx="15" cy="15" r="3" fill="#a99bff"/></svg>)ioline";
    static const auto drawable = juce::Drawable::createFromImageData (svg, sizeof (svg) - 1);
    if (drawable != nullptr)
        drawable->drawWithin (g, bounds, juce::RectanglePlacement::stretchToFit, 1.0f);
}
} // namespace ioline::branding
