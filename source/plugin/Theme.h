#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

#include <cstdint>

// Colours, type and metrics from the Home Dashboard's Deep Ocean look: one flat deep-blue field,
// dark glass panels with hairline edges, cyan for what is live or chosen and red for small signals.
// Sora for text, JetBrains Mono for numbers and ids, Michroma for the wordmark. Names follow the
// dashboard's CSS variables; the few the plug-in adds are marked.
namespace ps::theme
{
// ---- colour --------------------------------------------------------------------------------

inline const juce::Colour bg { 0xff021d2e };         // the deep-blue field behind everything
inline const juce::Colour ink { 0xff040c12 };        // the base tint of panels, menus and popovers
inline const juce::Colour panelTop { 0xff03121c };   // a panel's fill at its top (ink at 62% over the field)
inline const juce::Colour panelBottom { 0xff040d14 }; // and lower down (ink at 93%), denser so text stays easy to read
inline const juce::Colour menu { 0xff040c13 };       // popovers: ink at 97%
inline const juce::Colour glassEdge { 0x1ac8f5ff };  // the hairline edge of every panel
inline const juce::Colour edgeHover { 0x29ffffff };  // a panel's edge under the mouse
inline const juce::Colour pillEdge { 0x29ffffff };   // hairline pill buttons
inline const juce::Colour line { 0x1a2ad4ff };       // faint cyan rules
inline const juce::Colour lineStrong { 0x472ad4ff }; // inputs, and cyan edges that must be seen
inline const juce::Colour hair { 0x0effffff };       // dividers inside a panel
inline const juce::Colour text { 0xffd9eef2 };       // body copy
inline const juce::Colour textBright { 0xffffffff }; // titles, values, hovered and active rows
inline const juce::Colour textSoft { 0xff9cc7cf };   // secondary copy and button labels
inline const juce::Colour textDim { 0xff5f727a };    // labels and captions
inline const juce::Colour cyan { 0xff2ad4ff };       // what is live or chosen: selection, focus, links
inline const juce::Colour ice { 0xffc6f9ff };        // the far end of a value's white-to-ice gradient
inline const juce::Colour red { 0xffff2a4f };        // small signals: the current number, icons, the wordmark
inline const juce::Colour cyanSoft { 0x142ad4ff };   // selection and focus fill
inline const juce::Colour warn { 0xffffb020 };       // warnings only, always with a word beside it
inline const juce::Colour off { 0xff3a4650 };        // decorative "off" state
inline const juce::Colour onAccent { 0xff0b0f1c };   // a label on a solid white or coloured fill
inline const juce::Colour wash { 0x0affffff };       // the faint white wash under a hovered row

// Plug-in additions: the canvas dot grid and an unlit step cell.
inline const juce::Colour dots { 0x1f2ad4ff };
inline const juce::Colour cellOff { 0x12ffffff };

// ---- metrics -------------------------------------------------------------------------------

constexpr float radiusSm = 4.0f; // rows, cells
constexpr float radius = 8.0f;   // panels: modules, blocks, menus
constexpr int controlH = 30;     // buttons, fields
constexpr int topbarH = 52;
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
    regular,  // 400: everything by default
    semibold, // 600: titles and uppercase labels
    bold      // 700: large values
};

// The faces are embedded so the plug-in looks the same on every system. Sizes are CSS pixels.
juce::Font sans (float size, Weight weight = Weight::regular);  // Sora
juce::Font mono (float size, bool medium = false);              // JetBrains Mono, ligatures off
juce::Font display (float size);                                // Michroma: the wordmark only

// The dashboard's panel and tile labels: small Sora semibold capitals, widely tracked. Upper-case
// the text where it is drawn.
inline juce::Font caps (float size, float tracking = 0.18f) { return sans (size, Weight::semibold).withExtraKerningFactor (tracking); }

// Tags without a colour of their own get one from their name. The palette keeps to the look's
// neon on deep blue, and leaves out amber, which is kept for warnings.
struct Swatch
{
    const char* name;
    std::uint32_t rgb;
};
inline const std::array<Swatch, 8>& swatches()
{
    static const std::array<Swatch, 8> list { { { "Cyan", 0x2ad4ff }, { "Red", 0xff2a4f }, { "Pink", 0xff6ad5 }, { "Violet", 0x9d8cff }, { "Slate", 0x9cc7cf }, { "Mint", 0x4dffb8 }, { "Coral", 0xff8f6b }, { "White", 0xd9eef2 } } };
    return list;
}
inline juce::Colour hashedTagColour (const std::string& tag)
{
    std::uint32_t h = 0;
    for (char c : tag)
        h = h * 31 + (std::uint32_t) (unsigned char) c;
    return rgb (swatches()[h % 8].rgb);
}

// An LED: a small dot with a soft glow, or a hollow ring when off.
inline void drawLed (juce::Graphics& g, juce::Point<float> centre, juce::Colour c, bool on, float size = 6.0f)
{
    const auto r = juce::Rectangle<float> (size, size).withCentre (centre);
    if (! on)
    {
        g.setColour (textDim);
        g.drawEllipse (r.reduced (0.5f), 1.0f);
        return;
    }
    g.setColour (c.withAlpha (0.16f));
    g.fillEllipse (r.expanded (3.0f));
    g.setColour (c.withAlpha (0.3f));
    g.fillEllipse (r.expanded (1.5f));
    g.setColour (c);
    g.fillEllipse (r);
}

// A panel: dark glass that gets denser toward the bottom, with a faint sheen at the top, a
// hairline edge and a soft drop shadow.
void drawPanel (juce::Graphics&, juce::Rectangle<float>, juce::Colour edge = glassEdge, float cornerSize = radius);

// Draws text with a gradient from white to ice, as the dashboard draws its large values.
void drawValue (juce::Graphics&, const juce::String& text, juce::Rectangle<float> area, juce::Justification);

// The look and feel for every JUCE control in the window: hairline pill buttons, a solid white
// pill for the main action, an LED toggle, and menus and inputs on dark glass with cyan focus.
class Look : public juce::LookAndFeel_V4
{
public:
    Look();

    // Buttons whose property "accent" is true get the solid white pill (one per view).
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
    int getPopupMenuBorderSize() override { return 5; }

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getSliderPopupFont (juce::Slider&) override { return mono (12.0f); }
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
