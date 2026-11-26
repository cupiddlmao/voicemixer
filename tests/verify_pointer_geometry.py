#!/usr/bin/env python3
"""Recompute every coordinate native/smoke.cpp presses from the console's own
constants, and assert each one lands on the control its comment names.

smoke.cpp drives the real window with absolute logical pixels, so a control that
moves silently turns a test into a click on nothing. This reads the geometry
constants out of native/console.cpp and native/ui_theme.h, rebuilds the hit
rectangles the painter registers in Console::add(), and checks the numbers
written in smoke.cpp against them. No Qt, PipeWire or build needed.

Run: python3 tests/verify_pointer_geometry.py [--audit]
--audit reports where each pin lands without requiring it to be centred.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CONSOLE = ROOT / 'native' / 'console.cpp'
THEME = ROOT / 'native' / 'ui_theme.h'
SMOKE = ROOT / 'native' / 'smoke.cpp'

# Where each pin pointed at f08c539, before the EQ gutter and the master
# regroup, so the report can show what moved. Not used for checking.
OLD = {
    'gate': (99, 286), 'pad': (84, 175), 'denoise': (32, 323),
    'route-b1': (99, 584), 'bus-gain-b1': (1343, 550),
    'system-gain': (711, 550), 'b2-gain': (1395, 550), 'music-mute': (873, 662),
    'route-b2': (99, 606), 'route-b3': (753, 628), 'eq-l-treble': (681, 155),
    'send-reverb': (33, 415), 'balance-b2': (1386, 409), 'mono-b2': (1386, 450),
    'mic-mute': (99, 662), 'balance-loop': (1074, 409),
}


def parse_constants():
    """Evaluate the geometry constants in dependency order from the two sources."""
    text = THEME.read_text() + '\n' + CONSOLE.read_text()
    values = {}
    order = ['unit', 'radius', 'controlRadius', 'knobSize', 'knobCompact',
             'routeHeight', 'rowHeight', 'W', 'H', 'page', 'pitch', 'stripW',
             'top', 'stripH', 'padTop', 'padW', 'padH', 'padInX', 'padInY',
             'gainTop', 'gainH', 'capH', 'masterX', 'masterW', 'mfTop', 'mfH',
             'seekX', 'seekW', 'tapeY', 'tapeW', 'tapeH', 'routeCellW',
             'routeCellH', 'routePitch', 'routeGap', 'fxX', 'fxW', 'busPitch',
             'busCol', 'busTop', 'triplePitch', 'eqGutter', 'eqPitch']
    for name in order:
        match = re.search(r'\b' + name + r'\s*=\s*([^,;]+)[,;]', text)
        if not match:
            sys.exit('cannot find constant %r' % name)
        expression = match.group(1).strip()
        try:
            values[name] = eval(expression, {'__builtins__': {}}, dict(values))  # noqa: S307
        except Exception:
            sys.exit('cannot evaluate %s = %s' % (name, expression))
    return values


C = parse_constants()


class Rect:
    def __init__(self, x, y, w, h):
        self.x, self.y, self.w, self.h = x, y, w, h

    def contains(self, px, py):
        # QRectF::contains is half-open: the right and bottom edges are outside.
        return self.x <= px < self.x + self.w and self.y <= py < self.y + self.h

    @property
    def centre(self):
        return (self.x + self.w / 2, self.y + self.h / 2)

    def adjusted(self, dx1, dy1, dx2, dy2):
        return Rect(self.x + dx1, self.y + dy1, self.w - dx1 + dx2, self.h - dy1 + dy2)

    def __str__(self):
        return '({:g},{:g},{:g},{:g})'.format(self.x, self.y, self.w, self.h)


def strip_x(i):
    return C['page'] + i * C['pitch']


def pad_rect(i):
    return Rect(strip_x(i) + 8, C['padTop'], C['padW'], C['padH'])


def pad_inner(i):
    return pad_rect(i).adjusted(C['padInX'], C['padInY'], -C['padInX'], -C['padInY'])


def eq_x(i, k):
    return strip_x(i) + 8 + C['eqGutter'] + C['knobCompact'] / 2 + k * C['eqPitch']


def triple_x(i, k):
    return strip_x(i) + 8 + C['knobCompact'] / 2 + k * C['triplePitch']


def bus_xx(i):
    return C['masterX'] + 10 + i * C['busPitch']


TOP, KNOB = C['top'], C['knobSize']


def controls():
    """Hit rectangles exactly as Console::add() registers them, keyed by name."""
    c = {}
    # --- hardware strip 0 (Input 1) -----------------------------------------
    x = strip_x(0)
    c['comp'] = Rect(x + 33 - KNOB / 2, TOP + 232 - KNOB / 2, KNOB, KNOB)
    c['gate'] = Rect(x + 84 - KNOB / 2, TOP + 232 - KNOB / 2, KNOB, KNOB)
    c['pad'] = pad_rect(0)
    c['pad-inner'] = pad_inner(0)
    c['denoise'] = Rect(x + 8, TOP + 260, 49, 22)
    c['route-b1'] = Rect(x + 74, C['gainTop'] + 5 * (C['routeHeight'] + 2), 35, C['routeHeight'])
    c['route-b2'] = Rect(x + 74, C['gainTop'] + 6 * (C['routeHeight'] + 2), 35, C['routeHeight'])
    c['send-reverb'] = Rect(triple_x(0, 0) - 16, TOP + 350 - 16, 32, 32)
    c['mic-mute'] = Rect(x + 60, TOP + 588, 49, C['rowHeight'])
    # --- virtual strip 5 (System) -------------------------------------------
    vx = strip_x(5)
    c['system-gain'] = Rect(vx + 54 - 20, C['gainTop'] - 8, 40, C['gainH'] + 16)
    c['route-b3'] = Rect(vx + 74, C['gainTop'] + 7 * (C['routeHeight'] + 2), 35, C['routeHeight'])
    c['eq-l-treble'] = Rect(eq_x(5, 0) - 16, TOP + 114 - 16, 32, 32)
    c['eq-r-bass'] = Rect(eq_x(5, 2) - 16, TOP + 154 - 16, 32, 32)
    # --- virtual strip 6 (Music) --------------------------------------------
    c['music-mute'] = Rect(strip_x(6) + 60, TOP + 588, 49, C['rowHeight'])
    # --- tape route chips: one column per group ------------------------------
    for bus in range(8):
        blue = bus >= 5
        row = bus - 5 if blue else bus
        c['chip-b%d' % (bus - 4) if blue else 'chip-a%d' % (bus + 1)] = Rect(
            C['masterX'] + 208 + (C['routeCellW'] + C['routeGap'] if blue else 0),
            C['tapeY'] + 44 + row * C['routePitch'], C['routeCellW'], C['routeCellH'])
    # --- one master column per bus ------------------------------------------
    for i in range(8):
        xx = bus_xx(i)
        prefix = 'bus%d' % i
        c[prefix + '-mode'] = Rect(xx, C['busTop'] + 32, C['busCol'], C['routeHeight'])
        c[prefix + '-mono'] = Rect(xx, C['busTop'] + 62, C['busCol'], C['routeHeight'])
        c[prefix + '-mute'] = Rect(xx, C['busTop'] + 92, C['busCol'], C['routeHeight'])
        c[prefix + '-balance'] = Rect(xx + C['busCol'] / 2 - KNOB / 2,
                                      C['busTop'] + 154 - KNOB / 2, KNOB, KNOB)
        c[prefix + '-gain'] = Rect(xx + 14 + (C['busCol'] - 16) / 2 - 16,
                                   C['mfTop'] - 6, 32, C['mfH'] + 12)
    return c


HITS = controls()

# (id, locator, control, expected, note)
#   locator: ('click'|'dblclick'|'wheel'|'context'|'loop'|'drag', name)
#   expected: 'centre' or an explicit (x, y)
PINS = [
    ('gate', ('wheel', 'wheel'), 'gate', 'centre', 'Input 1 gate knob'),
    ('pad', ('click', 'pad'), 'pad', 'centre', 'Input 1 colour pad'),
    ('denoise', ('click', 'denoise'), 'denoise', 'centre', 'Input 1 Denoise chip'),
    ('route-b1', ('click', 'route-b1'), 'route-b1', 'centre', 'Input 1 route B1'),
    ('bus-gain-b1', ('wheel', 'busWheel'), 'bus5-gain', 'centre', 'master fader B1'),
    ('system-gain', ('wheel', 'systemWheel'), 'system-gain', 'centre', 'System fader grab'),
    ('b2-gain', ('wheel', 'b2Wheel'), 'bus6-gain', 'centre', 'master fader B2'),
    ('music-mute', ('click', 'music-mute'), 'music-mute', 'centre', 'Music Mute'),
    ('route-b2', ('click', 'route-b2'), 'route-b2', 'centre', 'Input 1 route B2'),
    ('route-b3', ('click', 'route-b3'), 'route-b3', 'centre', 'System route B3'),
    ('eq-l-treble', ('wheel', 'eqWheel'), 'eq-l-treble', 'centre', 'System EQ L Treble'),
    ('send-reverb', ('wheel', 'sendWheel'), 'send-reverb', 'centre', 'Input 1 reverb send'),
    ('balance-b2', ('wheel', 'balanceWheel'), 'bus6-balance', 'centre', 'B2 balance knob'),
    ('mono-b2', ('click', 'mono-b2'), 'bus6-mono', 'centre', 'B2 downmix'),
    ('mic-mute', ('click', 'mic-mute'), 'mic-mute', 'centre', 'Input 1 Mute'),
    ('pan-left', ('click', 'pan-left'), 'pad', (24, 178), 'Input 1 pad, pan clamps to -1'),
    ('pan-right', ('click', 'pan-right'), 'pad', (116, 178), 'Input 1 pad, pan clamps to +1'),
    ('pan-reset', ('dblclick', 'reset'), 'pad', 'centre', 'Input 1 pad, centre reset'),
    ('balance-loop', ('loop', 'balance'), 'bus%d-balance', 'centre', 'bus i balance knob'),
    ('ctx-loop', ('loop', 'context'), 'pad', 'centre', 'Input i colour pad'),
    ('pan-drag', ('drag', 'target'), 'pad-inner', (93, 178), 'pad drag to pan +0.5'),
]


def read_pin(text, locator):
    """Return the coordinate literal written in smoke.cpp for one pinned input."""
    kind, name = locator
    if kind == 'click':
        pattern = r'click\(c,\{([-\d.]+),([-\d.]+)\}\);\s*//\s*' + re.escape(name) + r':'
        match = re.search(pattern, text)
        return (float(match.group(1)), float(match.group(2))) if match else None
    if kind == 'wheel':
        pattern = (r'QWheelEvent ' + re.escape(name) +
                   r'\(\{([-\d.]+),([-\d.]+)\},\{([-\d.]+),([-\d.]+)\}')
        match = re.search(pattern, text)
        if not match:
            return None
        press = (float(match.group(1)), float(match.group(2)))
        move = (float(match.group(3)), float(match.group(4)))
        return press if press == move else ('split', press, move)
    if kind == 'dblclick':
        pattern = (r'QMouseEvent ' + re.escape(name) +
                   r'\(QEvent::MouseButtonDblClick,\{([-\d.]+),([-\d.]+)\}')
        match = re.search(pattern, text)
        return (float(match.group(1)), float(match.group(2))) if match else None
    if kind == 'loop':
        if name == 'balance':
            pattern = r'QPointF p\(([\d.]+)\+i\*([\d.]+),([\d.]+)\)'
        else:
            pattern = r'const QPoint pos\(([\d.]+)\+i\*([\d.]+),([\d.]+)\)'
        match = re.search(pattern, text)
        if not match:
            return None
        return ('loop', float(match.group(1)), float(match.group(2)), float(match.group(3)))
    if kind == 'drag':
        pattern = r'QPointF target=\*phase==0\?p\+QPointF\(([\d.]+),([\d.]+)\):QPointF\(([-\d.]+),([-\d.]+)\)'
        match = re.search(pattern, text)
        if not match:
            return None
        return ('drag', float(match.group(1)), float(match.group(2)),
                float(match.group(3)), float(match.group(4)))
    raise AssertionError(kind)


def main():
    audit = '--audit' in sys.argv
    text = SMOKE.read_text()
    failures = []
    inner0 = pad_inner(0)
    print('geometry: page=%g pitch=%g top=%g padInner0=(%g,%g,%g,%g)'
          % (C['page'], C['pitch'], C['top'], inner0.x, inner0.y, inner0.w, inner0.h))
    print('          EQ x = %s at pitch %g; master column x0=%g col=%g pitch=%g'
          % (', '.join('%g' % eq_x(5, k) for k in range(3)), C['eqPitch'],
             bus_xx(0), C['busCol'], C['busPitch']))
    print()
    header = '%-14s %-13s %-13s %-30s %-5s' % ('pin', 'old', 'new', 'control', 'land')
    print(header)
    print('-' * (len(header) + 4))
    for pin_id, locator, control, expected, note in PINS:
        found = read_pin(text, locator)
        if found is None:
            failures.append('%s: no coordinate found in smoke.cpp' % pin_id)
            print('%-14s %-13s %-13s %-30s %-5s' % (pin_id, '-', 'MISSING', note, 'FAIL'))
            continue
        old = OLD.get(pin_id)
        old_text = '%g,%g' % old if old else '-'
        if found[0] == 'split':
            failures.append('%s: press %s and move %s differ' % (pin_id, found[1], found[2]))
            print('%-14s %-13s %-13s %-30s %-5s' % (pin_id, old_text, 'SPLIT', note, 'FAIL'))
            continue
        if found[0] == 'loop':
            base, step, y = found[1], found[2], found[3]
            shown = '%g+%gi,%g' % (base, step, y)
            bad = False
            for i in range(8):
                point = (base + i * step, y)
                if locator[1] == 'balance':
                    hit = HITS['bus%d-balance' % i]
                    want = hit.centre
                    if not hit.contains(*point) or abs(point[0] - want[0]) >= 0.51:
                        failures.append('%s i=%d (%g,%g) misses %s' % (pin_id, i, point[0], point[1], hit))
                        bad = True
                else:
                    hit = pad_rect(i)
                    if not hit.contains(*point):
                        failures.append('%s i=%d (%g,%g) misses the pad %s' % (pin_id, i, point[0], point[1], hit))
                        bad = True
            print('%-14s %-13s %-13s %-30s %-5s' % (pin_id, old_text, shown, note, 'ok' if not bad else 'FAIL'))
            continue
        if found[0] == 'drag':
            dx, _dy, px, py = found[1], found[2], found[3], found[4]
            # phase 0 drags the knob `dx` right: balance = dx*0.015, gainL = 1-max(0,b).
            balance = round(max(-1., min(1., dx * 0.015)), 3)
            gain_l = 1 - max(0., balance)
            if abs(balance - 0.45) > 1e-9 or abs(gain_l - 0.55) > 1e-9:
                failures.append('pan-drag: %gpx right gives balance %g, gainL %g (test wants .55)'
                                % (dx, balance, gain_l))
            point = (px, py)
            hit = HITS['pad-inner']
            fx = (point[0] - hit.x) / hit.w
            # phase 1 drags the pad to x=93: 75% across padInner(0) is pan +0.5.
            if abs(fx - 0.75) > 1e-9:
                failures.append('pan-drag: x=%g is %g across padInner, not 0.75' % (point[0], fx))
            print('%-14s %-13s %-13s %-30s %-5s' % (pin_id, old_text,
                  'dx=%g, x=%g' % (dx, point[0]), note + ' (+%g -> %.2f)' % (dx, fx * 2 - 1),
                  'ok' if abs(fx - 0.75) < 1e-9 and abs(balance - 0.45) < 1e-9 else 'FAIL'))
            continue
        point = found
        hit = HITS[control]
        want = hit.centre if expected == 'centre' else expected
        inside = hit.contains(*point)
        centred = abs(point[0] - want[0]) < 0.51 and abs(point[1] - want[1]) < 0.51
        if not inside or (not centred and not audit):
            reasons = []
            if not inside:
                reasons.append('outside %s' % hit)
            if not centred:
                reasons.append('off centre by (%+.1f,%+.1f), centre=(%g,%g)'
                               % (point[0] - want[0], point[1] - want[1], want[0], want[1]))
            failures.append('%s at (%g,%g): %s' % (pin_id, point[0], point[1], '; '.join(reasons)))
        print('%-14s %-13s %-13s %-30s %-5s' % (
            pin_id, old_text, '%g,%g' % point, note, 'ok' if inside else 'MISS'))
    # The clamp probes must stay outside the pad's inner box.
    for pin_id, x in (('pan-left', 24), ('pan-right', 116)):
        if inner0.contains(x, 178):
            failures.append('%s x=%g is inside padInner; it no longer clamps' % (pin_id, x))
    # Tape chips: A1-A5 and B1-B3 must not share a column, and must stay in the tape rect.
    for a in range(1, 6):
        for b in range(1, 4):
            if HITS['chip-a%d' % a].x == HITS['chip-b%d' % b].x:
                failures.append('tape chips A%d and B%d share a column' % (a, b))
    for name, chip in sorted(HITS.items()):
        if not name.startswith('chip-'):
            continue
        if chip.x + chip.w > C['masterX'] + C['tapeW']:
            failures.append('%s leaves the tape rect (%g > %g)'
                            % (name, chip.x + chip.w, C['masterX'] + C['tapeW']))
        if chip.y + chip.h > C['tapeY'] + C['tapeH']:
            failures.append('%s drops below the tape rect' % name)
    # Master columns: controls in a column may not overlap, and the bank must fit.
    for i in range(8):
        keys = ['bus%d-%s' % (i, s) for s in ('mode', 'mono', 'mute', 'balance', 'gain')]
        for a in range(len(keys)):
            for b in range(a + 1, len(keys)):
                ra, rb = HITS[keys[a]], HITS[keys[b]]
                if (ra.x < rb.x + rb.w and rb.x < ra.x + ra.w
                        and ra.y < rb.y + rb.h and rb.y < ra.y + ra.h):
                    failures.append('column %d: %s overlaps %s' % (i, keys[a], keys[b]))
    right = HITS['bus7-mute'].x + HITS['bus7-mute'].w
    if right > C['masterX'] + C['masterW']:
        failures.append('master column 7 ends at %g, past the panel' % right)
    if HITS['bus0-mode'].x < C['masterX']:
        failures.append('master column 0 starts left of the panel')
    # EQ rows must stay inside the strip and clear of the Applications caption.
    for k in range(3):
        left = eq_x(5, k) - C['knobCompact'] / 2
        right_edge = eq_x(5, k) + C['knobCompact'] / 2
        if left < strip_x(5) + 8 or right_edge > strip_x(5) + 8 + C['padW']:
            failures.append('EQ slot %d leaves the strip content column' % k)
    print()
    print('EQ L/R rows (x, y): ' + ', '.join(
        '%s:(%g,%g)' % (('L' if k < 3 else 'R') + str(k % 3 + 1), eq_x(5, k % 3),
                        TOP + (114 if k < 3 else 154)) for k in range(6)))
    if failures:
        for failure in failures:
            print('FAIL: ' + failure)
        print('\n%d check(s) failed' % len(failures))
        return 1
    print('PASS: %d pinned inputs land on their controls; chips and master columns do not overlap.'
          % len(PINS))
    return 0


if __name__ == '__main__':
    sys.exit(main())
