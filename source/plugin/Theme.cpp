#include "Theme.h"

#include "PatternspaceFonts.h"

namespace ps::theme
{
namespace
{
// The embedded faces, made once and released when JUCE shuts down (not at static destruction, after
// the platform's font engine may already be gone).
class Faces : private juce::DeletedAtShutdown
{
public:
    Faces()
        : faces { juce::Typeface::createSystemTypefaceFor (PatternspaceFonts::FiraCodeLight_ttf, PatternspaceFonts::FiraCodeLight_ttfSize),
                  juce::Typeface::createSystemTypefaceFor (PatternspaceFonts::FiraCodeRegular_ttf, PatternspaceFonts::FiraCodeRegular_ttfSize),
                  juce::Typeface::createSystemTypefaceFor (PatternspaceFonts::FiraCodeMedium_ttf, PatternspaceFonts::FiraCodeMedium_ttfSize),
                  juce::Typeface::createSystemTypefaceFor (PatternspaceFonts::FiraCodeSemiBold_ttf, PatternspaceFonts::FiraCodeSemiBold_ttfSize) }
    {
    }

    ~Faces() override { clearSingletonInstance(); }

    juce::Typeface::Ptr faces[4];

    JUCE_DECLARE_SINGLETON_INLINE (Faces, false)
};

juce::Typeface::Ptr typeface (Weight weight) { return Faces::getInstance()->faces[(int) weight]; }

bool isAccent (const juce::Component& c) { return (bool) c.getProperties()["accent"]; }
} // namespace

// Sizes are CSS pixels, as the system gives them, so they are point heights here.
juce::Font font (float size, Weight weight)
{
    return juce::Font (juce::FontOptions (typeface (weight)).withPointHeight (size).withFeatureDisabled ("calt").withFeatureDisabled ("liga"));
}

Look::Look()
{
    setColourScheme ({ sidebar, bg, surface, lineStrong, text, surface, text, cyanSoft, text });
    setColour (juce::ResizableWindow::backgroundColourId, bg);
    setColour (juce::DocumentWindow::textColourId, textSoft);
    setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    setColour (juce::TextButton::textColourOffId, textSoft);
    setColour (juce::TextButton::textColourOnId, cyan);
    setColour (juce::ComboBox::backgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::ComboBox::outlineColourId, lineStrong);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::arrowColourId, textDim);
    setColour (juce::PopupMenu::backgroundColourId, tooltip);
    setColour (juce::PopupMenu::textColourId, textSoft);
    setColour (juce::PopupMenu::headerTextColourId, textDim);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, cyanSoft);
    setColour (juce::PopupMenu::highlightedTextColourId, textBright);
    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, juce::Colour (0x4000f0ff));
    setColour (juce::ToggleButton::textColourId, textSoft);
    setColour (juce::ToggleButton::tickColourId, cyan);
    setColour (juce::ToggleButton::tickDisabledColourId, textDim);
    setColour (juce::Label::textColourId, textSoft);
    setColour (juce::Label::textWhenEditingColourId, text);
    setColour (juce::Label::outlineWhenEditingColourId, juce::Colours::transparentBlack);
    setColour (juce::TextEditor::backgroundColourId, juce::Colour (0x59000000));
    setColour (juce::TextEditor::textColourId, text);
    setColour (juce::TextEditor::highlightColourId, juce::Colour (0x4000f0ff));
    setColour (juce::TextEditor::highlightedTextColourId, textBright);
    setColour (juce::TextEditor::outlineColourId, lineStrong);
    setColour (juce::TextEditor::focusedOutlineColourId, cyan);
    setColour (juce::CaretComponent::caretColourId, cyan);
    setColour (juce::ScrollBar::thumbColourId, line);
    setColour (juce::TooltipWindow::backgroundColourId, tooltip);
    setColour (juce::TooltipWindow::textColourId, text);
    setColour (juce::TooltipWindow::outlineColourId, lineStrong);
    setColour (juce::AlertWindow::backgroundColourId, surface);
    setColour (juce::AlertWindow::textColourId, text);
    setColour (juce::AlertWindow::outlineColourId, line);
}

juce::Typeface::Ptr Look::getTypefaceForFont (const juce::Font& f)
{
    // Anything that asks for the default font (menus, editors, alerts) gets Fira Code too.
    if (f.getTypefaceName() == juce::Font::getDefaultSansSerifFontName() || f.getTypefaceName() == juce::Font::getDefaultMonospacedFontName())
        return typeface (f.isBold() ? Weight::medium : Weight::regular);
    return LookAndFeel_V4::getTypefaceForFont (f);
}

juce::Font Look::getTextButtonFont (juce::TextButton&, int) { return font (12.0f); }

void Look::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool down)
{
    const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    if (! b.isEnabled())
    {
        g.setColour (hair);
        g.drawRoundedRectangle (r, radiusMd, 1.0f);
        return;
    }
    if (isAccent (b))
    {
        // Cyan on cyan-soft; it fills solid cyan when hovered, and stays filled while on.
        const bool solid = on || highlighted || down;
        g.setColour (solid ? cyan : cyanSoft);
        g.fillRoundedRectangle (r, radiusMd);
        g.setColour (solid ? cyan : cyan.withAlpha (0.45f));
        g.drawRoundedRectangle (r, radiusMd, 1.0f);
        return;
    }
    if (highlighted || down)
    {
        g.setColour (wash);
        g.fillRoundedRectangle (r, radiusMd);
    }
    g.setColour (lineStrong);
    g.drawRoundedRectangle (r, radiusMd, 1.0f);
}

void Look::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool highlighted, bool down)
{
    juce::Colour c = textSoft;
    if (! b.isEnabled())
        c = textDim;
    else if (isAccent (b))
        c = b.getToggleState() || highlighted || down ? onCyan : cyan;
    else if (highlighted || down)
        c = textBright;
    else if (b.getToggleState())
        c = cyan;
    g.setColour (c);
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.drawText (b.getButtonText(), b.getLocalBounds().reduced (8, 0), juce::Justification::centred, true);
}

// A toggle is an LED and its words, like a status row: a lit cyan dot when on, a hollow ring when off.
void Look::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool down)
{
    const auto r = b.getLocalBounds().toFloat();
    if (highlighted || down)
    {
        g.setColour (wash);
        g.fillRoundedRectangle (r.reduced (0.5f), radiusMd);
    }
    drawLed (g, { r.getX() + 14.0f, r.getCentreY() }, cyan, b.getToggleState());
    g.setColour (highlighted ? textBright : (b.getToggleState() ? text : textSoft));
    g.setFont (font (12.0f));
    g.drawText (b.getButtonText(), r.withTrimmedLeft (26), juce::Justification::centredLeft, true);
}

void Look::changeToggleButtonWidthToFitText (juce::ToggleButton& b)
{
    b.setSize (juce::roundToInt (juce::GlyphArrangement::getStringWidth (font (12.0f), b.getButtonText())) + 38, b.getHeight());
}

void Look::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto r = juce::Rectangle<float> (0, 0, (float) width, (float) height).reduced (0.5f);
    if (box.isMouseOver (true))
    {
        g.setColour (wash);
        g.fillRoundedRectangle (r, radiusMd);
    }
    g.setColour (box.hasKeyboardFocus (true) ? cyan : lineStrong);
    g.drawRoundedRectangle (r, radiusMd, 1.0f);
    juce::Path chevron;
    const float cx = (float) width - 14.0f, cy = (float) height * 0.5f;
    chevron.startNewSubPath (cx - 3.5f, cy - 1.5f);
    chevron.lineTo (cx, cy + 2.0f);
    chevron.lineTo (cx + 3.5f, cy - 1.5f);
    g.setColour (textDim);
    g.strokePath (chevron, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

juce::Font Look::getComboBoxFont (juce::ComboBox&) { return font (12.0f); }

void Look::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (4, 1, box.getWidth() - 26, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

void Look::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    g.fillAll (tooltip);
    g.setColour (lineStrong);
    g.drawRect (0, 0, width, height, 1);
}

void Look::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive, bool isHighlighted,
                              bool isTicked, bool hasSubMenu, const juce::String& itemText, const juce::String& shortcutKeyText,
                              const juce::Drawable*, const juce::Colour* textColour)
{
    if (isSeparator)
    {
        g.setColour (hair);
        g.fillRect (area.reduced (10, 0).withHeight (1).withY (area.getCentreY()));
        return;
    }
    auto r = area.reduced (4, 1).toFloat();
    if (isHighlighted && isActive)
    {
        g.setColour (cyanSoft);
        g.fillRoundedRectangle (r, radiusSm);
        g.setColour (cyan);
        g.fillRoundedRectangle (r.getX(), r.getY() + 6, 2.0f, r.getHeight() - 12, 1.0f);
    }
    if (isTicked)
        drawLed (g, { r.getX() + 13.0f, r.getCentreY() }, cyan, true);
    juce::Colour c = textColour != nullptr ? *textColour : (isHighlighted ? textBright : textSoft);
    if (! isActive)
        c = textDim;
    g.setColour (c);
    g.setFont (getPopupMenuFont());
    auto textArea = r.withTrimmedLeft (26).withTrimmedRight (10);
    if (hasSubMenu)
    {
        g.drawText (juce::String::fromUTF8 ("\xE2\x80\xBA"), textArea.removeFromRight (12), juce::Justification::centred);
    }
    if (shortcutKeyText.isNotEmpty())
    {
        g.setColour (textDim);
        g.drawText (shortcutKeyText, textArea, juce::Justification::centredRight, true);
        g.setColour (c);
    }
    g.drawText (itemText, textArea, juce::Justification::centredLeft, true);
}

void Look::drawPopupMenuSectionHeader (juce::Graphics& g, const juce::Rectangle<int>& area, const juce::String& sectionName)
{
    g.setColour (textDim);
    g.setFont (font (11.0f));
    g.drawText (sectionName, area.reduced (14, 0).withTrimmedTop (4), juce::Justification::centredLeft, true);
}

juce::Font Look::getPopupMenuFont() { return font (12.0f); }

// Only the tempo uses a slider: a field-like box whose fill shows where the value sits.
void Look::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float pos, float, float,
                             juce::Slider::SliderStyle style, juce::Slider& s)
{
    if (style != juce::Slider::LinearBar)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, pos, 0, 0, style, s);
        return;
    }
    const auto r = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (0.5f);
    g.setColour (juce::Colour (0x59000000));
    g.fillRoundedRectangle (r, radiusMd);
    {
        juce::Graphics::ScopedSaveState save (g);
        juce::Path clip;
        clip.addRoundedRectangle (r, radiusMd);
        g.reduceClipRegion (clip);
        g.setColour (cyanSoft);
        g.fillRect (r.withRight (pos));
    }
    g.setColour (s.isMouseOverOrDragging() ? cyan.withAlpha (0.45f) : lineStrong);
    g.drawRoundedRectangle (r, radiusMd, 1.0f);
}

juce::Font Look::getLabelFont (juce::Label& l)
{
    if (dynamic_cast<juce::Slider*> (l.getParentComponent()) != nullptr)
        return font (12.0f);
    return LookAndFeel_V4::getLabelFont (l);
}

juce::Label* Look::createSliderTextBox (juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (font (12.0f));
    l->setColour (juce::Label::textColourId, text);
    return l;
}

void Look::fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor& ed)
{
    g.setColour (surface);
    g.fillRoundedRectangle (juce::Rectangle<float> (0, 0, (float) width, (float) height), radiusSm);
    g.setColour (ed.findColour (juce::TextEditor::backgroundColourId));
    g.fillRoundedRectangle (juce::Rectangle<float> (0, 0, (float) width, (float) height), radiusSm);
}

void Look::drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& ed)
{
    const auto r = juce::Rectangle<float> (0, 0, (float) width, (float) height).reduced (0.5f);
    g.setColour (ed.hasKeyboardFocus (true) ? cyan : lineStrong);
    g.drawRoundedRectangle (r, radiusSm, 1.0f);
}

void Look::drawTooltip (juce::Graphics& g, const juce::String& tipText, int width, int height)
{
    const auto r = juce::Rectangle<float> (0, 0, (float) width, (float) height);
    g.setColour (tooltip);
    g.fillRoundedRectangle (r, radiusSm);
    g.setColour (lineStrong);
    g.drawRoundedRectangle (r.reduced (0.5f), radiusSm, 1.0f);
    g.setColour (text);
    g.setFont (font (11.0f));
    g.drawFittedText (tipText, r.reduced (8, 3).toNearestInt(), juce::Justification::centredLeft, 3);
}

juce::Rectangle<int> Look::getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea)
{
    const int w = juce::jmin (360, juce::roundToInt (juce::GlyphArrangement::getStringWidth (font (11.0f), tipText)) + 18);
    const int lines = juce::roundToInt (juce::GlyphArrangement::getStringWidth (font (11.0f), tipText) + 18) / 360 + 1;
    const int h = 8 + 16 * lines;
    return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
                                 screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 6, w, h)
        .constrainedWithin (parentArea);
}

void Look::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height, bool vertical,
                          int thumbStart, int thumbSize, bool isMouseOver, bool isMouseDown)
{
    const auto thumb = vertical ? juce::Rectangle<int> (x, thumbStart, width, thumbSize) : juce::Rectangle<int> (thumbStart, y, thumbSize, height);
    g.setColour (isMouseOver || isMouseDown ? lineStrong : line);
    g.fillRoundedRectangle (thumb.toFloat().reduced (1.5f), 2.0f);
}

void Look::drawAlertBox (juce::Graphics& g, juce::AlertWindow& w, const juce::Rectangle<int>& textArea, juce::TextLayout& layout)
{
    g.fillAll (surface);
    g.setColour (line);
    g.drawRect (w.getLocalBounds(), 1);
    layout.draw (g, textArea.toFloat());
}
} // namespace ps::theme
