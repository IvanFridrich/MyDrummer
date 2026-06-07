#!/usr/bin/env python3
"""
midi_to_cpp.py — convert assets/patterns/*.mid → generated/patterns.hpp + .cpp

Parses SMF format-0 files produced by gen_patterns.py and emits C++ arrays
consumed by the Dummer auto-drummer.

Assumptions: Format 0, channel 9 for drums, PPQN=480, one LOOP_START marker.
"""
from __future__ import annotations

import struct
from pathlib import Path
from typing import List, Tuple

PPQN = 480
BAR  = PPQN * 4   # 1920 ticks per 4/4 bar

Event = Tuple[int, int, int]   # (abs_tick, note, vel)


def _read_varint(data: bytes, pos: int) -> Tuple[int, int]:
    val = 0
    while True:
        b = data[pos]; pos += 1
        val = (val << 7) | (b & 0x7F)
        if not (b & 0x80):
            break
    return val, pos


def parse_mid(path: Path) -> Tuple[int, int, int, List[Event]]:
    """
    Returns: (bpm, loop_start_tick, loop_end_tick, note_on_events)
    note_on_events: sorted by tick, channel-9 note-ons with vel > 0 only.
    loop_end_tick: next bar boundary after the last note event.
    """
    data = path.read_bytes()
    assert data[:4] == b'MThd', f"{path.name}: not an SMF file"
    # PPQN is at offset 12-13
    assert data[14:18] == b'MTrk', f"{path.name}: missing MTrk"

    trk_len = struct.unpack('>I', data[18:22])[0]
    pos = 22
    end = pos + trk_len

    bpm              = 120
    loop_start_tick  = -1
    events: List[Event] = []
    cur_tick         = 0
    running_status   = 0

    while pos < end:
        delta, pos = _read_varint(data, pos)
        cur_tick += delta
        b = data[pos]

        if b == 0xFF:                           # meta event
            pos += 1
            meta_type = data[pos]; pos += 1
            meta_len, pos = _read_varint(data, pos)
            if meta_type == 0x51 and meta_len == 3:
                uspb = struct.unpack('>I', b'\x00' + data[pos:pos+3])[0]
                bpm  = round(60_000_000 / uspb)
            elif meta_type == 0x06:
                if data[pos:pos+meta_len] == b'LOOP_START':
                    loop_start_tick = cur_tick
            pos += meta_len
            running_status = 0

        elif b in (0xF0, 0xF7):                 # sysex
            pos += 1
            slen, pos = _read_varint(data, pos)
            pos += slen
            running_status = 0

        else:
            if b & 0x80:                        # new status byte
                running_status = b; pos += 1
            status = running_status
            cmd = (status >> 4) & 0x0F
            ch  = status & 0x0F

            if cmd in (0x8, 0x9, 0xA, 0xB, 0xE):
                d1 = data[pos]; pos += 1
                d2 = data[pos]; pos += 1
                if cmd == 0x9 and ch == 9 and d2 > 0:
                    events.append((cur_tick, d1, d2))
            elif cmd in (0xC, 0xD):
                pos += 1                        # single data byte

    events.sort(key=lambda e: e[0])

    assert loop_start_tick >= 0, f"{path.name}: LOOP_START marker not found"
    last_tick    = max(e[0] for e in events) if events else loop_start_tick
    loop_end_tick = ((last_tick // BAR) + 1) * BAR

    return bpm, loop_start_tick, loop_end_tick, events


def emit_cpp(patterns: list, out_dir: Path) -> None:
    # ---- generated/patterns.hpp -----------------------------------------
    hpp = out_dir / 'patterns.hpp'
    hpp.write_text('\n'.join([
        '// AUTO-GENERATED — do not edit (see scripts/midi_to_cpp.py)',
        '#pragma once',
        '#include <stdint.h>',
        '',
        'namespace dummer { namespace audio {',
        '',
        'struct DrumEvent {',
        '    uint32_t tick;   // absolute MIDI tick from file start',
        '    uint8_t  note;   // GM percussion note number',
        '    uint8_t  vel;    // velocity 1-127',
        '};',
        '',
        'struct PatternInfo {',
        '    const DrumEvent* events;',
        '    uint16_t         count;',
        '    uint32_t         loop_start_tick;   // tick of LOOP_START marker',
        '    uint32_t         loop_end_tick;     // tick of last bar boundary',
        '    uint16_t         bpm;               // nominal BPM for this file',
        '    const char*      name;',
        '};',
        '',
        'constexpr uint8_t PATTERN_COUNT = 7;',
        'extern const PatternInfo kDrumPatterns[PATTERN_COUNT];',
        '',
        '}} // namespace dummer::audio',
        '',
    ]), encoding='utf-8')

    # ---- generated/patterns.cpp -----------------------------------------
    cpp_lines = [
        '// AUTO-GENERATED — do not edit (see scripts/midi_to_cpp.py)',
        '#include "patterns.hpp"',
        '',
        'namespace dummer { namespace audio {',
        '',
    ]

    meta = []
    for name, bpm, loop_start, loop_end, evs in patterns:
        arr = f'kEvt{name.capitalize()}'
        meta.append((name, arr, bpm, loop_start, loop_end, len(evs)))
        cpp_lines.append(f'static const DrumEvent {arr}[] = {{')
        for tick, note, vel in evs:
            cpp_lines.append(f'    {{{tick}u, {note}u, {vel}u}},')
        cpp_lines.append('};')
        cpp_lines.append('')

    cpp_lines.append('const PatternInfo kDrumPatterns[PATTERN_COUNT] = {')
    for name, arr, bpm, loop_start, loop_end, count in meta:
        cpp_lines.append(
            f'    {{{arr}, {count}u, {loop_start}u, {loop_end}u, '
            f'{bpm}u, "{name}"}},')
    cpp_lines.append('};')
    cpp_lines.append('')
    cpp_lines.append('}} // namespace dummer::audio')
    cpp_lines.append('')

    (out_dir / 'patterns.cpp').write_text('\n'.join(cpp_lines), encoding='utf-8')


def main() -> None:
    repo    = Path(__file__).resolve().parent.parent
    in_dir  = repo / 'assets' / 'patterns'
    out_dir = repo / 'generated'
    out_dir.mkdir(exist_ok=True)

    NAMES = ['blues', 'country', 'jazz', 'funk', 'reggae', 'gospel', 'hardrock']
    patterns = []

    print('midi_to_cpp: parsing patterns')
    for name in NAMES:
        bpm, loop_start, loop_end, evs = parse_mid(in_dir / f'{name}.mid')
        loop_bars = (loop_end - loop_start) // BAR
        print(f'  {name:<12} bpm={bpm:3d}  loop_start={loop_start}'
              f'  loop_end={loop_end}  loop_bars={loop_bars}  events={len(evs)}')
        patterns.append((name, bpm, loop_start, loop_end, evs))

    print(f'midi_to_cpp: writing to {out_dir}')
    emit_cpp(patterns, out_dir)
    print(f'  patterns.hpp')
    print(f'  patterns.cpp')
    print('Done.')


if __name__ == '__main__':
    main()
