#pragma once
// Design tokens for the hand-painted console and its dialogs. Every colour,
// type size and spacing constant used by the UI comes from here.
#include <QtGui/QColor>
#include <QtGui/QFont>

namespace theme {
// Surfaces
inline const QColor bg{0x14,0x1d,0x25};           // window background, insets
inline const QColor panel{0x1c,0x28,0x33};        // grouped panels
inline const QColor panelRaised{0x22,0x31,0x40};  // buttons, caps, chips
inline const QColor border{0x2d,0x3e,0x4b};
inline const QColor borderStrong{0x3d,0x52,0x62};
inline const QColor track{0x3a,0x4c,0x58};        // inactive knob arcs, empty meters
inline const QColor grid{0x1f,0x2c,0x37};         // faint XY pad grid
// Text
inline const QColor ink{0xe8,0xef,0xf3};
inline const QColor inkSecondary{0x93,0xa7,0xb4};
inline const QColor inkMuted{0x6b,0x7f,0x8c};
// Signal
inline const QColor accent{0x6f,0xd3,0xa8};       // enabled / routed / active
inline const QColor accentDim{0x2f,0x5a,0x48};
inline const QColor warn{0xe0,0xa3,0x40};         // recording / attention
inline const QColor danger{0xe0,0x5a,0x4f};       // mute / clipping

// Type: one family, five sizes (logical pixels of the 1480x720 canvas).
namespace type {
constexpr int title=20, section=16, label=13, value=11, micro=10;
}
inline QFont font(int px,bool bold=false,bool caps=false){
    QFont f("DejaVu Sans");f.setPixelSize(px);f.setBold(bold);
    if(caps)f.setLetterSpacing(QFont::AbsoluteSpacing,px*.08);
    return f;
}

// Space
constexpr double unit=8, radius=6, controlRadius=4;
// knobSize is spelled out to avoid colliding with the knob() paint helper in
// console.cpp, which shares the global namespace through the anonymous one.
constexpr double knobSize=44, knobCompact=30;  // diameters
constexpr double routeHeight=22, rowHeight=28;
}
