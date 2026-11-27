#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "juce_theme.h"

namespace voicemixer {
// One look-and-feel for everything the host draws itself: the rack window and
// the two plugin-control editors. Third-party plugin editors are never given
// this, so a vendor GUI keeps its own colours.
//
// A button's job is carried in its component ID, so the row can say "primary",
// "destructive", "toggle" or "quiet" once instead of each button painting
// itself: those four are the only distinctions the rack needs.
namespace rack_role {
inline constexpr const char* primary = "primary";
inline constexpr const char* destructive = "destructive";
inline constexpr const char* secondary = "secondary";  // muted label, still a button
inline constexpr const char* toggle = "toggle";        // shows its state as a dot
inline constexpr const char* quiet = "quiet";          // no chrome until hovered
}

// Buttons are as wide as their own text plus padding, so no row carries a
// guessed width or leaves dead space at the edge.
namespace rack_metrics {
inline int indicatorWidth(const juce::Button& button)
{
    return button.getComponentID() == rack_role::toggle ? 13 : 0;
}
inline int buttonWidth(const juce::Button& button, int pad = 10)
{
    const auto text = rack_theme::font(rack_theme::type::label)
                          .getStringWidthFloat(button.getButtonText());
    return juce::jmax(44, juce::roundToInt(text) + 2 * pad + indicatorWidth(button));
}
} // namespace rack_metrics

class RackLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    RackLookAndFeel()
    {
        using namespace juce;
        const auto c = [](juce::uint32 value) { return rack_theme::colour(value); };

        setColour(ResizableWindow::backgroundColourId, c(rack_theme::bg));
        setColour(DocumentWindow::backgroundColourId, c(rack_theme::bg));
        setColour(TextButton::buttonColourId, c(rack_theme::panel));
        setColour(TextButton::buttonOnColourId, c(rack_theme::panelRaised));
        setColour(TextButton::textColourOffId, c(rack_theme::ink));
        setColour(TextButton::textColourOnId, c(rack_theme::inkSecondary));
        setColour(ComboBox::backgroundColourId, c(rack_theme::panel));
        setColour(ComboBox::textColourId, c(rack_theme::ink));
        setColour(ComboBox::outlineColourId, c(rack_theme::borderStrong));
        setColour(ComboBox::arrowColourId, c(rack_theme::inkSecondary));
        setColour(ComboBox::focusedOutlineColourId, c(rack_theme::accent));
        setColour(Label::textColourId, c(rack_theme::ink));
        setColour(PopupMenu::backgroundColourId, c(rack_theme::panelRaised));
        setColour(PopupMenu::textColourId, c(rack_theme::ink));
        setColour(PopupMenu::headerTextColourId, c(rack_theme::inkMuted));
        setColour(PopupMenu::highlightedBackgroundColourId, c(rack_theme::accentDim));
        setColour(PopupMenu::highlightedTextColourId, c(rack_theme::ink));
        setColour(ToggleButton::textColourId, c(rack_theme::inkSecondary));
        setColour(ToggleButton::tickColourId, c(rack_theme::accent));
        setColour(ToggleButton::tickDisabledColourId, c(rack_theme::inkMuted));
        setColour(Slider::textBoxTextColourId, c(rack_theme::ink));
        setColour(Slider::textBoxBackgroundColourId, c(rack_theme::panel));
        setColour(Slider::textBoxOutlineColourId, Colours::transparentBlack);
        setColour(Slider::thumbColourId, c(rack_theme::ink));
        setColour(Slider::trackColourId, c(rack_theme::accent));
        setColour(Slider::backgroundColourId, c(rack_theme::track));
        setColour(Slider::rotarySliderFillColourId, c(rack_theme::accent));
        setColour(Slider::rotarySliderOutlineColourId, c(rack_theme::track));
    }

    juce::Font getTextButtonFont(juce::TextButton&, int) override
    {
        return rack_theme::font(rack_theme::type::label);
    }
    juce::Font getComboBoxFont(juce::ComboBox&) override
    {
        return rack_theme::font(rack_theme::type::label);
    }
    juce::Font getPopupMenuFont() override
    {
        return rack_theme::font(rack_theme::type::label);
    }

    // Inset the text so the state pip drawn in drawComboBox has its own column.
    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds(14, 1, box.getWidth() - 36, box.getHeight() - 2);
        label.setFont(getComboBoxFont(box));
    }

    void drawComboBox(juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box) override
    {
        using namespace juce;
        const auto area = juce::Rectangle<float>(0.0f, 0.0f, (float) width, (float) height).reduced(0.5f);
        g.setColour(box.findColour(ComboBox::backgroundColourId));
        g.fillRoundedRectangle(area, rack_theme::radius);
        g.setColour(box.hasKeyboardFocus(false) ? box.findColour(ComboBox::focusedOutlineColourId)
                                               : box.findColour(ComboBox::outlineColourId));
        g.drawRoundedRectangle(area, rack_theme::radius, 1.0f);

        // The selected slot's state, as a pip in the gutter. A ComboBox has no
        // per-instance state colour of its own, so the rack passes this one
        // through buttonColourId; the arrow zone it normally paints is drawn
        // below instead.
        const auto pip = box.findColour(ComboBox::buttonColourId);
        if (! pip.isTransparent())
        {
            g.setColour(pip);
            g.fillRoundedRectangle(6.0f, (float) height * 0.5f - 8.0f, 3.0f, 16.0f, 1.5f);
        }

        const auto x = (float) width - 18.0f, y = (float) height * 0.5f;
        juce::Path chevron;
        chevron.startNewSubPath(x - 4.0f, y - 2.5f);
        chevron.lineTo(x, y + 1.5f);
        chevron.lineTo(x + 4.0f, y - 2.5f);
        g.setColour(box.findColour(ComboBox::arrowColourId).withAlpha(box.isEnabled() ? 0.9f : 0.3f));
        g.strokePath(chevron, juce::PathStrokeType(1.4f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
    }

    void drawComboBoxTextWhenNothingSelected(juce::Graphics& g, juce::ComboBox& box, juce::Label& label) override
    {
        // A hint, not a selection: dim and the same size as the caption.
        g.setColour(rack_theme::colour(rack_theme::inkMuted));
        g.setFont(getComboBoxFont(box));
        g.drawText(box.getTextWhenNothingSelected(), label.getBounds().reduced(1),
                   label.getJustificationType(), false);
    }

    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour&,
                              bool highlighted, bool down) override
    {
        using namespace juce;
        const auto id = button.getComponentID();
        const bool engaged = id == rack_role::toggle && button.getToggleState();
        const auto area = button.getLocalBounds().toFloat().reduced(0.5f);

        juce::Colour fill = rack_theme::colour(rack_theme::panel);
        juce::Colour edge = rack_theme::colour(rack_theme::border);
        if (id == rack_role::primary)
            edge = rack_theme::colour(rack_theme::accent).withAlpha(0.55f);
        else if (id == rack_role::destructive)
            edge = rack_theme::colour(rack_theme::danger).withAlpha(0.5f);
        else if (id == rack_role::quiet)
            fill = edge = Colours::transparentBlack;
        else if (engaged)
        {
            fill = rack_theme::colour(rack_theme::panel).overlaidWith(
                rack_theme::colour(rack_theme::warn).withAlpha(0.16f));
            edge = rack_theme::colour(rack_theme::warn).withAlpha(0.55f);
        }

        if (highlighted || down)
        {
            if (id == rack_role::quiet)
            {
                fill = rack_theme::colour(down ? rack_theme::panelRaised : rack_theme::panel);
                edge = rack_theme::colour(rack_theme::border);
            }
            else
            {
                fill = down ? rack_theme::colour(rack_theme::panelRaised).brighter(0.06f) : fill.brighter(0.06f);
                if (id == rack_role::primary) edge = rack_theme::colour(rack_theme::accent);
                else if (id == rack_role::destructive) edge = rack_theme::colour(rack_theme::danger).withAlpha(0.8f);
                else if (engaged) edge = rack_theme::colour(rack_theme::warn).withAlpha(0.8f);
                else edge = rack_theme::colour(rack_theme::borderStrong);
            }
        }

        if (! button.isEnabled())
        {
            fill = fill.withMultipliedAlpha(0.5f);
            edge = edge.withMultipliedAlpha(0.5f);
        }

        if (! fill.isTransparent())
        {
            g.setColour(fill);
            g.fillRoundedRectangle(area, rack_theme::radius);
        }
        if (! edge.isTransparent())
        {
            g.setColour(edge);
            g.drawRoundedRectangle(area, rack_theme::radius, 1.0f);
        }
    }

    void drawButtonText(juce::Graphics& g, juce::TextButton& button, bool, bool) override
    {
        using namespace juce;
        const auto id = button.getComponentID();
        const auto label = rack_theme::font(rack_theme::type::label);
        juce::Colour text = rack_theme::colour(rack_theme::ink);
        if (id == rack_role::primary)
            text = rack_theme::colour(rack_theme::accent);
        else if (id == rack_role::destructive)
            text = rack_theme::colour(rack_theme::danger);
        else if (id == rack_role::secondary || id == rack_role::quiet)
            text = rack_theme::colour(rack_theme::inkSecondary);
        else if (id == rack_role::toggle)
            text = button.getToggleState() ? rack_theme::colour(rack_theme::warn)
                                          : button.findColour(TextButton::textColourOffId);

        if (! button.isEnabled())
            text = text.withMultipliedAlpha(0.45f);

        g.setFont(label);

        if (id == rack_role::toggle)
        {
            // A toggle, not a momentary button: the dot is the plugin's signal
            // state and the label is what a click will do.
            constexpr int dot = 6, gap = 7;
            const int textWidth = juce::roundToInt(label.getStringWidthFloat(button.getButtonText()));
            const int left = (button.getWidth() - (textWidth + dot + gap)) / 2;
            g.setColour(text);
            g.fillEllipse((float) left, (float) (button.getHeight() - dot) * 0.5f, (float) dot, (float) dot);
            g.drawText(button.getButtonText(), left + dot + gap, 0, textWidth + 1, button.getHeight(),
                       Justification::centredLeft, false);
            return;
        }

        g.setColour(text);
        g.drawText(button.getButtonText(), button.getLocalBounds(), Justification::centred, false);
    }
};
} // namespace voicemixer
