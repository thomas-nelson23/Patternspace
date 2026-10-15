#include "Theme.h"

#include "PatternspaceFonts.h"

namespace ps::theme
{
namespace
{
enum Face
{
    soraRegular,
    soraSemibold,
    soraBold,
    monoRegular,
    monoMedium,
    michroma,
    faceCount
};

// The embedded faces, made once and released when JUCE shuts down (not at static destruction, after
// the platform's font engine may already be gone).
class Faces : private juce::DeletedAtShutdown
{
public:
    Faces()
    {
        using namespace PatternspaceFonts;
        const std::pair<const char*, int> data[] = {
            { SoraRegular_ttf, SoraRegular_ttfSize },
            { SoraSemiBold_ttf, SoraSemiBold_ttfSize },
            { SoraBold_ttf, SoraBold_ttfSize },
            { JetBrainsMonoRegular_ttf, JetBrainsMonoRegular_ttfSize },
            { JetBrainsMonoMedium_ttf, JetBrainsMonoMedium_ttfSize },
            { MichromaRegular_ttf, MichromaRegular_ttfSize },
        };
        for (int i = 0; i < faceCount; ++i)
            faces[i] = juce::Typeface::createSystemTypefaceFor (data[i].first, (size_t) data[i].second);
    }

    ~Faces() override { clearSingletonInstance(); }

    juce::Typeface::Ptr faces[faceCount];

    JUCE_DECLARE_SINGLETON_INLINE (Faces, false)
};

juce::Typeface::Ptr face (Face f) { return Faces::getInstance()->faces[f]; }

juce::Font make (Face f, float size)
{
    return juce::Font (juce::FontOptions (face (f)).withPointHeight (size).withFeatureDisabled ("calt").withFeatureDisabled ("liga"));
}

bool isAccent (const juce::Component& c) { return (bool) c.getProperties()["accent"]; }

constexpr float pill = 999.0f;

float pillRadius (juce::Rectangle<float> r) { return std::min (pill, r.getHeight() * 0.5f); }
} // namespace

juce::Font sans (float size, Weight weight)
{
    return make (weight == Weight::bold ? soraBold : (weight == Weight::semibold ? soraSemibold : soraRegular), size);
}

juce::Font mono (float size, bool medium) { return make (medium ? monoMedium : monoRegular, size); }

juce::Font display (float size) { return make (michroma, size); }

void drawPanel (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour edge, float cornerSize)
{
    // A soft drop, offset down and kept inside the panel's width, as in the dashboard's glass shadow.
    g.setColour (juce::Colours::black.withAlpha (0.28f));
    g.fillRoundedRectangle (r.reduced (6, 0).translated (0, 10), cornerSize + 4);
    g.setColour (juce::Colours::black.withAlpha (0.18f));
    g.fillRoundedRectangle (r.reduced (2, 0).translated (0, 5), cornerSize + 2);

    juce::ColourGradient fill (panelTop, 0, r.getY(), panelBottom, 0, r.getY() + std::max (40.0f, r.getHeight() * 0.7f), false);
    g.setGradientFill (fill);
    g.fillRoundedRectangle (r, cornerSize);
    juce::ColourGradient sheen (juce::Colours::white.withAlpha (0.035f), 0, r.getY(), juce::Colours::white.withAlpha (0.0f), 0,
                                r.getY() + std::max (16.0f, r.getHeight() * 0.4f), false);
    g.setGradientFill (sheen);
    g.fillRoundedRectangle (r, cornerSize);
    g.setColour (edge);
    g.drawRoundedRectangle (r.reduced (0.5f), cornerSize, 1.0f);
}

void drawValue (juce::Graphics& g, const juce::String& t, juce::Rectangle<float> area, juce::Justification j)
{
    const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), t);
    const float x = j.testFlags (juce::Justification::right) ? area.getRight() - w : area.getX();
    g.setGradientFill (juce::ColourGradient (textBright, x, 0, ice, x + std::max (1.0f, w), 0, false));
    g.drawText (t, area, j, true);
}

Look::Look()
{
    setColourScheme ({ menu, bg, panelBottom, pillEdge, text, panelBottom, text, cyanSoft, text });
    setColour (juce::ResizableWindow::backgroundColourId, bg);
    setColour (juce::DocumentWindow::textColourId, textSoft);
    setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    setColour (juce::TextButton::textColourOffId, textSoft);
    setColour (juce::TextButton::textColourOnId, cyan);
    setColour (juce::ComboBox::backgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::ComboBox::outlineColourId, pillEdge);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::arrowColourId, textDim);
    setColour (juce::PopupMenu::backgroundColourId, menu);
    setColour (juce::PopupMenu::textColourId, textSoft);
    setColour (juce::PopupMenu::headerTextColourId, textDim);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, wash);
    setColour (juce::PopupMenu::highlightedTextColourId, textBright);
    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, juce::Colour (0x402ad4ff));
    setColour (juce::ToggleButton::textColourId, textSoft);
    setColour (juce::ToggleButton::tickColourId, cyan);
    setColour (juce::ToggleButton::tickDisabledColourId, textDim);
    setColour (juce::Label::textColourId, textSoft);
    setColour (juce::Label::textWhenEditingColourId, text);
    setColour (juce::Label::outlineWhenEditingColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TextEditor::backgroundColourId, juce::Colour (0x59000000));
    setColour (juce::TextEditor::textColourId, text);
    setColour (juce::TextEditor::highlightColourId, juce::Colour (0x402ad4ff));
    setColour (juce::TextEditor::highlightedTextColourId, textBright);
    setColour (juce::TextEditor::outlineColourId, lineStrong);
    setColour (juce::TextEditor::focusedOutlineColourId, cyan);
    setColour (juce::CaretComponent::caretColourId, cyan);
    setColour (juce::ScrollBar::thumbColourId, hair);
    setColour (juce::TooltipWindow::backgroundColourId, menu);
    setColour (juce::TooltipWindow::textColourId, text);
    setColour (juce::TooltipWindow::outlineColourId, glassEdge);
    setColour (juce::AlertWindow::backgroundColourId, menu);
    setColour (juce::AlertWindow::textColourId, text);
    setColour (juce::AlertWindow::outlineColourId, glassEdge);
}

juce::Typeface::Ptr Look::getTypefaceForFont (const juce::Font& f)
{
    // Anything that asks for a default font (menus, editors, alerts) gets the embedded faces too.
    const auto name = f.getTypefaceName();
    if (name == juce::Font::getDefaultMonospacedFontName())
        return face (monoRegular);
    if (name == juce::Font::getDefaultSansSerifFontName())
        return face (f.isBold() ? soraSemibold : soraRegular);
    return LookAndFeel_V4::getTypefaceForFont (f);
}

juce::Font Look::getTextButtonFont (juce::TextButton& b, int) { return sans (12.0f, isAccent (b) ? Weight::semibold : Weight::regular); }

void Look::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool down)
{
    const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const float corner = pillRadius (r);
    if (! b.isEnabled())
    {
        g.setColour (hair);
        g.drawRoundedRectangle (r, corner, 1.0f);
        return;
    }
    if (isAccent (b))
    {
        // The main action: a solid white pill that turns cyan when hovered. While it is off (the
        // clock stopped) it is a hairline pill with a white label instead.
        const bool solid = b.getToggleState() || highlighted || down;
        if (solid)
        {
            g.setColour ((highlighted || down) ? cyan : textBright);
            g.fillRoundedRectangle (r, corner);
        }
        else
        {
            g.setColour (textBright.withAlpha (0.55f));
            g.drawRoundedRectangle (r, corner, 1.0f);
        }
        return;
    }
    if (highlighted || down)
    {
        g.setColour (wash);
        g.fillRoundedRectangle (r, corner);
    }
    g.setColour (b.getToggleState() ? cyan.withAlpha (0.45f) : pillEdge);
    g.drawRoundedRectangle (r, corner, 1.0f);
}

void Look::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool highlighted, bool down)
{
    juce::Colour c = textSoft;
    if (! b.isEnabled())
        c = textDim;
    else if (isAccent (b))
        c = b.getToggleState() || highlighted || down ? onAccent : textBright;
    else if (highlighted || down)
        c = textBright;
    else if (b.getToggleState())
        c = cyan;
    g.setColour (c);
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.drawText (b.getButtonText(), b.getLocalBounds().reduced (10, 0), juce::Justification::centred, true);
}

// A toggle is an LED and its words, like a status row: a lit cyan dot when on, a hollow ring when off.
void Look::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    const auto r = b.getLocalBounds().toFloat();
    drawLed (g, { r.getX() + 8.0f, r.getCentreY() }, cyan, b.getToggleState());
    g.setColour (highlighted ? textBright : (b.getToggleState() ? text : textSoft));
    g.setFont (sans (12.0f));
    g.drawText (b.getButtonText(), r.withTrimmedLeft (20), juce::Justification::centredLeft, true);
}

void Look::changeToggleButtonWidthToFitText (juce::ToggleButton& b)
{
    b.setSize (juce::roundToInt (juce::GlyphArrangement::getStringWidth (sans (12.0f), b.getButtonText())) + 28, b.getHeight());
}

void Look::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto r = juce::Rectangle<float> (0, 0, (float) width, (float) height).reduced (0.5f);
    if (box.isMouseOver (true))
    {
        g.setColour (wash);
        g.fillRoundedRectangle (r, pillRadius (r));
    }
    g.setColour (box.hasKeyboardFocus (true) ? cyan : pillEdge);
    g.drawRoundedRectangle (r, pillRadius (r), 1.0f);
    juce::Path chevron;
    const float cx = (float) width - 16.0f, cy = (float) height * 0.5f;
    chevron.startNewSubPath (cx - 3.5f, cy - 1.5f);
    chevron.lineTo (cx, cy + 2.0f);
    chevron.lineTo (cx + 3.5f, cy - 1.5f);
    g.setColour (textDim);
    g.strokePath (chevron, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

juce::Font Look::getComboBoxFont (juce::ComboBox&) { return sans (12.5f); }

void Look::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (10, 1, box.getWidth() - 34, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

void Look::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    g.fillAll (menu);
    g.setColour (glassEdge);
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
    auto r = area.reduced (5, 1).toFloat();
    if (isHighlighted && isActive)
    {
        g.setColour (wash);
        g.fillRoundedRectangle (r, radiusSm);
    }
    if (isTicked)
        drawLed (g, { r.getX() + 12.0f, r.getCentreY() }, red, true, 5.0f);
    juce::Colour c = textColour != nullptr ? *textColour : (isHighlighted || isTicked ? textBright : textSoft);
    if (! isActive)
        c = textDim;
    g.setColour (c);
    g.setFont (getPopupMenuFont());
    auto textArea = r.withTrimmedLeft (24).withTrimmedRight (10);
    if (hasSubMenu)
        g.drawText (juce::String::fromUTF8 ("\xE2\x80\xBA"), textArea.removeFromRight (12), juce::Justification::centred);
    if (shortcutKeyText.isNotEmpty())
    {
        g.setColour (textDim);
        g.setFont (mono (11.0f));
        g.drawText (shortcutKeyText, textArea, juce::Justification::centredRight, true);
        g.setColour (c);
        g.setFont (getPopupMenuFont());
    }
    g.drawText (itemText, textArea, juce::Justification::centredLeft, true);
}

void Look::drawPopupMenuSectionHeader (juce::Graphics& g, const juce::Rectangle<int>& area, const juce::String& sectionName)
{
    g.setColour (textDim);
    g.setFont (caps (10.0f));
    g.drawText (sectionName.toUpperCase(), area.reduced (14, 0).withTrimmedTop (4), juce::Justification::centredLeft, true);
}

juce::Font Look::getPopupMenuFont() { return sans (12.5f); }

// Only the tempo uses a slider: a pill field whose faint fill shows where the value sits.
void Look::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float pos, float, float,
                             juce::Slider::SliderStyle style, juce::Slider& s)
{
    if (style != juce::Slider::LinearBar)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, pos, 0, 0, style, s);
        return;
    }
    const auto r = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (0.5f);
    const float corner = pillRadius (r);
    {
        juce::Graphics::ScopedSaveState save (g);
        juce::Path clip;
        clip.addRoundedRectangle (r, corner);
        g.reduceClipRegion (clip);
        g.setColour (cyanSoft);
        g.fillRect (r.withRight (pos));
    }
    g.setColour (s.isMouseOverOrDragging() ? cyan.withAlpha (0.45f) : pillEdge);
    g.drawRoundedRectangle (r, corner, 1.0f);
}

juce::Font Look::getLabelFont (juce::Label& l)
{
    if (dynamic_cast<juce::Slider*> (l.getParentComponent()) != nullptr)
        return mono (12.0f);
    return LookAndFeel_V4::getLabelFont (l);
}

juce::Label* Look::createSliderTextBox (juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (mono (12.0f));
    l->setColour (juce::Label::textColourId, text);
    l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    return l;
}

void Look::fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor& ed)
{
    const auto r = juce::Rectangle<float> (0, 0, (float) width, (float) height);
    g.setColour (ink);
    g.fillRoundedRectangle (r, radiusSm);
    g.setColour (ed.findColour (juce::TextEditor::backgroundColourId));
    g.fillRoundedRectangle (r, radiusSm);
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
    g.setColour (menu);
    g.fillRoundedRectangle (r, radiusSm);
    g.setColour (glassEdge);
    g.drawRoundedRectangle (r.reduced (0.5f), radiusSm, 1.0f);
    g.setColour (text);
    g.setFont (sans (11.5f));
    g.drawFittedText (tipText, r.reduced (9, 3).toNearestInt(), juce::Justification::centredLeft, 3);
}

juce::Rectangle<int> Look::getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea)
{
    const float textW = juce::GlyphArrangement::getStringWidth (sans (11.5f), tipText) + 20;
    const int w = juce::jmin (360, juce::roundToInt (textW));
    const int lines = juce::roundToInt (textW) / 360 + 1;
    const int h = 10 + 17 * lines;
    return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
                                 screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 6, w, h)
        .constrainedWithin (parentArea);
}

void Look::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height, bool vertical,
                          int thumbStart, int thumbSize, bool isMouseOver, bool isMouseDown)
{
    const auto thumb = vertical ? juce::Rectangle<int> (x, thumbStart, width, thumbSize) : juce::Rectangle<int> (thumbStart, y, thumbSize, height);
    g.setColour (isMouseOver || isMouseDown ? pillEdge : hair);
    g.fillRoundedRectangle (thumb.toFloat().reduced (1.5f), 2.0f);
}

void Look::drawAlertBox (juce::Graphics& g, juce::AlertWindow& w, const juce::Rectangle<int>& textArea, juce::TextLayout& layout)
{
    g.fillAll (menu);
    g.setColour (glassEdge);
    g.drawRect (w.getLocalBounds(), 1);
    layout.draw (g, textArea.toFloat());
}
} // namespace ps::theme
