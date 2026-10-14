#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdint>

// Colours and fonts from the prototype's Patchbay look.
namespace ps::theme
{
inline const juce::Colour canvas { 0xff15181d };
inline const juce::Colour dots { 0xff262b33 };
inline const juce::Colour bar { 0xff1b1f26 };
inline const juce::Colour sidebar { 0xff191c22 };
inline const juce::Colour line { 0xff272c35 };
inline const juce::Colour lineStrong { 0xff2f3540 };
inline const juce::Colour node { 0xff20252d };
inline const juce::Colour nodeBorder { 0xff2f3540 };
inline const juce::Colour selected { 0xff8fa3ff };
inline const juce::Colour text { 0xffe9edf3 };
inline const juce::Colour textSoft { 0xffc5ccd7 };
inline const juce::Colour label { 0xff9aa4b2 };
inline const juce::Colour muted { 0xff7d8796 };
inline const juce::Colour cellOff { 0xff2b313b };
inline const juce::Colour noteText { 0xffaeb7c4 };
inline const juce::Colour panel { 0xff1f232b };

inline juce::Colour rgb (std::uint32_t rgb) { return juce::Colour (0xff000000u | rgb); }

inline juce::Colour fromHex (const std::string& hex, juce::Colour fallback)
{
    if (hex.size() == 7 && hex[0] == '#')
        return juce::Colour (0xff000000u | (std::uint32_t) juce::String (hex.substr (1)).getHexValue32());
    return fallback;
}

inline juce::Font sans (float size, bool bold = false)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
}

inline juce::Font mono (float size, bool bold = false)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), size, bold ? juce::Font::bold : juce::Font::plain));
}

// Tags without a colour of their own get one from their name, as in the prototype.
inline juce::Colour hashedTagColour (const std::string& tag)
{
    static const std::uint32_t palette[] = { 0xf2a541, 0x5cc8d6, 0xe57a9a, 0x9fb6ff, 0x7bd88f, 0xffb08a, 0xb394ff, 0xf5d77a };
    std::uint32_t h = 0;
    for (char c : tag)
        h = h * 31 + (std::uint32_t) (unsigned char) c;
    return rgb (palette[h % 8]);
}
} // namespace ps::theme
