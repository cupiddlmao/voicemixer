#pragma once
// Colours and type for the plugin-rack executable, mirroring native/ui_theme.h.
//
// This file exists because the rack is a separate JUCE binary: it cannot
// include ui_theme.h, which pulls in QtGui, and adding Qt to this target would
// mean linking a second GUI toolkit into the plugin host. The two files must
// therefore be kept in step by hand -- tests/verify_juce_theme.cpp parses the
// hex out of both and fails the build if a shared name drifts.
#include <juce_graphics/juce_graphics.h>

namespace rack_theme {
// Surfaces
constexpr juce::uint32 bg = 0xff141d25;           // window background, insets
constexpr juce::uint32 panel = 0xff1c2833;        // grouped panels
constexpr juce::uint32 panelRaised = 0xff223140;  // buttons, caps, chips
constexpr juce::uint32 border = 0xff2d3e4b;
constexpr juce::uint32 borderStrong = 0xff3d5262;
constexpr juce::uint32 track = 0xff3a4c58;        // inactive knob arcs, resting meter
constexpr juce::uint32 meterWell = 0xff2b3a47;    // empty meter slot, one step above panel
constexpr juce::uint32 grid = 0xff1f2c37;         // faint XY pad grid
// Text
constexpr juce::uint32 ink = 0xffe8eff3;
constexpr juce::uint32 inkSecondary = 0xff93a7b4;
constexpr juce::uint32 inkMuted = 0xff6b7f8c;
// Signal
constexpr juce::uint32 accent = 0xff6fd3a8;       // enabled / routed / active
constexpr juce::uint32 accentDim = 0xff2f5a48;
constexpr juce::uint32 warn = 0xffe0a340;         // recording / attention
constexpr juce::uint32 danger = 0xffe05a4f;       // mute / clipping

// Type: the console's five steps, in the Qt pixel sizes ui_theme.h names.
namespace type {
constexpr int title=20, section=16, label=13, value=11, micro=10;
}

inline juce::Colour colour(juce::uint32 value) { return juce::Colour(value); }

// JUCE measures a font by ascent + descent where Qt measures it by pixel size;
// 1.22 on DejaVu Sans puts rack text at the same size as the console's.
inline juce::Font font(int px, bool bold = false, float kerning = 0.0f)
{
    juce::Font f("DejaVu Sans", float(px) * 1.22f, bold ? juce::Font::bold : juce::Font::plain);
    if (kerning != 0.0f)
        f.setExtraKerningFactor(kerning);
    return f;
}

// Space
constexpr float radius = 4.0f;
constexpr int gutter = 12;  // window edge inset
} // namespace rack_theme
