#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdint>

// Colours, type and metrics from the Home Dashboard design system: black first, cyan second, red
// third. Flat near-black surfaces with hairline cyan borders and Fira Code throughout. The system
// is dark-only. Names follow the system's tokens; the few the plug-in adds are marked.
namespace ps::theme
{
// ---- colour --------------------------------------------------------------------------------

inline const juce::Colour bg { 0xff040506 };         // the ground behind everything
inline const juce::Colour sidebar { 0xff07080a };    // the module library and top bar (the source's 90% fill, flattened)
inline const juce::Colour surface { 0xff0a0c0f };    // modules, menus and popovers, always with a `line` border
inline const juce::Colour surfaceHi { 0x0b00f0ff };  // a raised, faintly cyan wash on top of `surface`
inline const juce::Colour line { 0x1a00f0ff };       // the hairline cyan border
inline const juce::Colour lineStrong { 0x4700f0ff }; // borders of buttons and inputs
inline const juce::Colour lineHover { 0x3300f0ff };  // a module's border under the mouse (tile hover)
inline const juce::Colour hair { 0x0fffffff };       // neutral dividers inside a module
inline const juce::Colour text { 0xffd9eef2 };       // body copy
inline const juce::Colour textBright { 0xffffffff }; // titles, values, hovered and active rows
inline const juce::Colour textSoft { 0xff9cc7cf };   // secondary copy and button labels
inline const juce::Colour textDim { 0xff5f727a };    // labels and captions only (4.05:1 on bg)
inline const juce::Colour cyan { 0xff00f0ff };       // what is live or chosen: active tab, selection, focus, accent
inline const juce::Colour red { 0xffff2a4f };        // icons, and the outputs' modules
inline const juce::Colour cyanSoft { 0x1400f0ff };   // active tab fill, accent button fill, focus ring
inline const juce::Colour redSoft { 0x1aff2a4f };
inline const juce::Colour warn { 0xffffb020 };       // warnings only, always with a word beside it
inline const juce::Colour off { 0xff3a4650 };        // decorative "off" state
inline const juce::Colour onCyan { 0xff021012 };     // a label on a solid colour fill
inline const juce::Colour tooltip { 0xff0b0d10 };
inline const juce::Colour wash { 0x0affffff };       // the 3.5-4% white wash under a hovered row or button

// Plug-in additions: the canvas dot grid and an unlit step cell.
inline const juce::Colour dots { 0x2400f0ff };
inline const juce::Colour cellOff { 0x14ffffff };

// ---- metrics -------------------------------------------------------------------------------

constexpr float radiusSm = 6.0f;  // tabs, rows, tooltips
constexpr float radiusMd = 8.0f;  // buttons, inputs
constexpr float radius = 10.0f;   // modules (the system's tiles and panels)
constexpr int controlH = 30;      // buttons, tabs
constexpr int topbarH = 44;
constexpr int sidebarW = 232;

inline juce::Colour rgb (std::uint32_t rgb) { return juce::Colour (0xff000000u | rgb); }

inline juce::Colour fromHex (const std::string& hex, juce::Colour fallback)
{
    if (hex.size() == 7 && hex[0] == '#')
        return juce::Colour (0xff000000u | (std::uint32_t) juce::String (hex.substr (1)).getHexValue32());
    return fallback;
}

// ---- type ----------------------------------------------------------------------------------

enum class Weight
{
    light,    // 300: large figures
    regular,  // 400: everything by default
    medium,   // 500: titles and values that need weight
    semibold  // 600: the wordmark
};

// Fira Code, embedded so it looks the same on every system. Ligatures are off, as in the system.
juce::Font font (float size, Weight weight = Weight::regular);

inline juce::Font mono (float size, bool bold = false) { return font (size, bold ? Weight::medium : Weight::regular); }

// Tags without a colour of their own get one from their name. The palette keeps to the system's
// neon on black, and leaves out amber, which the system keeps for warnings.
inline juce::Colour hashedTagColour (const std::string& tag)
{
    static const std::uint32_t palette[] = { 0x00f0ff, 0xff2a4f, 0xff6ad5, 0x9d8cff, 0x9cc7cf, 0x4dffb8, 0xff8f6b, 0xd9eef2 };
    std::uint32_t h = 0;
    for (char c : tag)
        h = h * 31 + (std::uint32_t) (unsigned char) c;
    return rgb (palette[h % 8]);
}

// An LED: a small dot with the system's glow, or a hollow ring when off.
inline void drawLed (juce::Graphics& g, juce::Point<float> centre, juce::Colour c, bool on, float size = 6.0f)
{
    const auto r = juce::Rectangle<float> (size, size).withCentre (centre);
    if (! on)
    {
        g.setColour (textDim);
        g.drawEllipse (r.reduced (0.5f), 1.0f);
        return;
    }
    g.setColour (c.withAlpha (0.18f));
    g.fillEllipse (r.expanded (3.0f));
    g.setColour (c.withAlpha (0.3f));
    g.fillEllipse (r.expanded (1.5f));
    g.setColour (c);
    g.fillEllipse (r);
}

// The look and feel for every JUCE control in the window: outlined 30px buttons, the accent
// button, an LED toggle, and menus and inputs on near-black with cyan focus.
class Look : public juce::LookAndFeel_V4
{
public:
    Look();

    // Buttons whose property "accent" is true get the cyan accent style (one per view).
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void changeToggleButtonWidthToFitText (juce::ToggleButton&) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive, bool isHighlighted,
                            bool isTicked, bool hasSubMenu, const juce::String& text, const juce::String& shortcutKeyText,
                            const juce::Drawable* icon, const juce::Colour* textColour) override;
    void drawPopupMenuSectionHeader (juce::Graphics&, const juce::Rectangle<int>& area, const juce::String& sectionName) override;
    juce::Font getPopupMenuFont() override;
    int getPopupMenuBorderSize() override { return 4; }

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getSliderPopupFont (juce::Slider&) override { return font (12.0f); }
    juce::Label* createSliderTextBox (juce::Slider&) override;

    void fillTextEditorBackground (juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int width, int height, juce::TextEditor&) override;

    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea) override;

    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height, bool isScrollbarVertical,
                        int thumbStartPosition, int thumbSize, bool isMouseOver, bool isMouseDown) override;

    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;

    void drawAlertBox (juce::Graphics&, juce::AlertWindow&, const juce::Rectangle<int>& textArea, juce::TextLayout&) override;
};
} // namespace ps::theme
