#!/usr/bin/env python3
"""
gen_patterns.py — generate drum-pattern MIDI files for the Dummer auto-drummer.

Usage: python scripts/gen_patterns.py

Output: assets/patterns/{blues,jazz,funk,reggae,gospel,hardrock}.mid

Structure (all styles):
  Bar 0:       Count-in — 4 stick clicks (plays once)
  Bars 1-2:    Intro    — building groove (plays once)
  [LOOP_START meta marker at bar 3 = tick 5760]
  Blues/Jazz:  Bars 3-14 — 12-bar form (loops forever)
  Funk/Reggae: Bars 3-6  — 4-bar groove (loops forever)
  Gospel/Rock: Bars 3-10 — 8-bar groove (loops forever)

PPQN = 480, 4/4 time.
"""
from __future__ import annotations

import struct
from pathlib import Path
from typing import List, Tuple

# ---------------------------------------------------------------------------
# MIDI / timing constants
# ---------------------------------------------------------------------------

PPQN    = 480
CHANNEL = 9   # MIDI channel 10 (0-indexed)

Q   = PPQN        # quarter note  = 480 ticks
E   = PPQN // 2   # 8th note      = 240
S   = PPQN // 4   # 16th note     = 120
T   = PPQN // 3   # triplet 8th   = 160
T2  = 2 * T       # swung upbeat (2/3 of beat) = 320
BAR = 4 * Q       # 4/4 bar       = 1920 ticks

LOOP_START_BAR  = 1
LOOP_START_TICK = LOOP_START_BAR * BAR   # 1920

NOTE_DUR = S   # percussive hit duration

# ---------------------------------------------------------------------------
# GM drum note numbers (channel 10)
# ---------------------------------------------------------------------------

STICKS    = 31
KICK      = 36
SNARE     = 38
HH_CLOSED = 42
HH_PEDAL  = 44
HH_OPEN   = 46
CRASH     = 49
TOM_HI    = 50
RIDE      = 51
TAMBOURINE= 54
COWBELL   = 56
TOM_MID   = 47
TOM_LO    = 45

Event = Tuple[int, int, int]   # (abs_tick, gm_note, velocity)


# ---------------------------------------------------------------------------
# SMF encoding
# ---------------------------------------------------------------------------

def _varint(v: int) -> bytes:
    buf = [v & 0x7F]
    v >>= 7
    while v:
        buf.append((v & 0x7F) | 0x80)
        v >>= 7
    return bytes(reversed(buf))


def _build_track_body(note_events: List[Event], bpm: int) -> bytes:
    uspb   = 60_000_000 // bpm
    marker = b'LOOP_START'

    # (tick, sort_priority, raw_bytes): 0=meta, 1=note-off, 2=note-on
    raw: List[Tuple[int, int, bytes]] = []
    raw.append((0, 0, b'\xff\x51\x03' + struct.pack('>I', uspb)[1:]))
    raw.append((0, 0, b'\xff\x58\x04\x04\x02\x18\x08'))
    raw.append((LOOP_START_TICK, 0,
                b'\xff\x06' + _varint(len(marker)) + marker))

    for tick, note, vel in note_events:
        raw.append((tick,            2, bytes([0x90 | CHANNEL, note, vel])))
        raw.append((tick + NOTE_DUR, 1, bytes([0x80 | CHANNEL, note, 0])))

    raw.sort(key=lambda e: (e[0], e[1]))

    out = bytearray()
    cur = 0
    for tick, _, data in raw:
        out += _varint(tick - cur)
        out += data
        cur = tick

    out += b'\x00\xff\x2f\x00'
    return bytes(out)


def _write_mid(path: Path, note_events: List[Event], bpm: int) -> None:
    body   = _build_track_body(note_events, bpm)
    header = b'MThd' + struct.pack('>IHHH', 6, 0, 1, PPQN)
    track  = b'MTrk' + struct.pack('>I', len(body)) + body
    path.write_bytes(header + track)
    print(f"  {path.name:<16}  {bpm} BPM   {len(note_events)} events   "
          f"{len(header) + len(track)} bytes")


# ---------------------------------------------------------------------------
# Shared helpers
# ---------------------------------------------------------------------------

def _b(bar: int) -> int:
    return bar * BAR


def _count_in(bar_idx: int) -> List[Event]:
    base = _b(bar_idx)
    return [(base + beat * Q, STICKS, 100) for beat in range(4)]


# ---------------------------------------------------------------------------
# Blues — Chicago shuffle, 90 BPM — 12-bar form
#
# Shuffle: closed HH on beat + swung upbeat (T2). Pedal HH on 2 & 4.
# Kick: 1, swung "and of 2", 3.  Snare: 2 & 4.
# Crash on chord changes (bars 3/7/11). Bar 14: tom turnaround.
# ---------------------------------------------------------------------------

def _gen_blues() -> Tuple[List[Event], int]:
    evs: List[Event] = []

    def shuffle_bar(bar_idx: int, crash: bool = False) -> None:
        base = _b(bar_idx)
        if crash:
            evs.append((base, CRASH, 127))
        for beat in range(4):
            evs.append((base + beat * Q,      HH_CLOSED, 80))
            evs.append((base + beat * Q + T2, HH_CLOSED, 60))
        evs.extend([(base + 1*Q, HH_PEDAL, 70), (base + 3*Q, HH_PEDAL, 70)])
        evs.extend([(base,            KICK,  100),
                    (base + 1*Q + T2, KICK,   80),
                    (base + 2*Q,      KICK,   90)])
        evs.extend([(base + 1*Q, SNARE, 100), (base + 3*Q, SNARE, 100)])

    def turnaround(bar_idx: int) -> None:
        base = _b(bar_idx)
        # Shuffle hi-hat + pedal — same feel the bass expects
        for beat in range(4):
            evs.append((base + beat * Q,      HH_CLOSED, 80))
            evs.append((base + beat * Q + T2, HH_CLOSED, 60))
        evs.extend([(base + 1*Q, HH_PEDAL, 70), (base + 3*Q, HH_PEDAL, 70)])
        # Kick pushes hard: 1, swung "and of 2", 3, swung "and of 3"
        evs.extend([(base,            KICK,  110),
                    (base + 1*Q + T2, KICK,   85),
                    (base + 2*Q,      KICK,  105),
                    (base + 2*Q + T2, KICK,   80)])
        # Snare on 2 & 4 — big accent on 4 to land the turnaround
        evs.extend([(base + 1*Q, SNARE, 108), (base + 3*Q, SNARE, 120)])
        # Full descending tom fill in shuffle time across beats 3–4
        evs.append((base + 2*Q,      TOM_HI,  100))
        evs.append((base + 2*Q + T2, TOM_MID,  95))
        evs.append((base + 3*Q + T2, TOM_LO,   90))

    evs += _count_in(0)

    # 12-bar blues (bars 1-12) ← loop start at bar 1
    shuffle_bar(1,  crash=True)   # I
    shuffle_bar(2)
    shuffle_bar(3)
    shuffle_bar(4)
    shuffle_bar(5,  crash=True)   # IV
    shuffle_bar(6)
    shuffle_bar(7)
    shuffle_bar(8)
    shuffle_bar(9,  crash=True)   # V
    shuffle_bar(10)
    shuffle_bar(11)
    turnaround(12)

    return evs, 90


# ---------------------------------------------------------------------------
# Jazz — ride swing, 120 BPM — 12-bar form
#
# Ride: quarter + swung upbeat. Pedal HH foot on 2 & 4.
# Kick light on 1. Snare on 4. Crash on chord changes.
# Bar 14: simple tom fill.
# ---------------------------------------------------------------------------

def _gen_jazz() -> Tuple[List[Event], int]:
    evs: List[Event] = []

    def jazz_bar(bar_idx: int, crash: bool = False) -> None:
        base = _b(bar_idx)
        if crash:
            evs.append((base, CRASH, 127))
        for beat in range(4):
            evs.append((base + beat * Q,      RIDE, 127))
            evs.append((base + beat * Q + T2, RIDE, 127))
        evs.extend([(base + 1*Q, HH_PEDAL, 65), (base + 3*Q, HH_PEDAL, 65)])
        evs.append((base, KICK, 60))
        evs.append((base + 3*Q, SNARE, 75))

    def jazz_build(bar_idx: int) -> None:
        # Bar 11: thicken groove to signal incoming turnaround.
        # Kick on 1 and 3, snare on 2 and 4, pedal drops on 4,
        # single TOM_HI on swung "and of 4" anticipates the fill.
        base = _b(bar_idx)
        for beat in range(4):
            evs.append((base + beat * Q,      RIDE, 127))
            evs.append((base + beat * Q + T2, RIDE, 127))
        evs.append((base + 1*Q, HH_PEDAL, 65))   # pedal on 2 only
        evs.extend([(base, KICK, 65), (base + 2*Q, KICK, 60)])
        evs.extend([(base + 1*Q, SNARE, 78), (base + 3*Q, SNARE, 90)])
        evs.append((base + 3*Q + T2, TOM_HI, 80))

    def jazz_fill(bar_idx: int) -> None:
        # Bar 12: full 2/3-bar fill — ride drops after beat 2, kick pushes,
        # snare escalates 2→3→4, 3-tom descent in triplet time drives back to bar 1.
        base = _b(bar_idx)
        for beat in range(2):
            evs.append((base + beat * Q,      RIDE, 127))
            evs.append((base + beat * Q + T2, RIDE, 127))
        evs.extend([(base,            KICK, 65),
                    (base + 1*Q + T2, KICK, 52),
                    (base + 2*Q,      KICK, 62),
                    (base + 2*Q + T2, KICK, 48)])
        evs.extend([(base + 1*Q, SNARE, 80),
                    (base + 2*Q, SNARE, 92),
                    (base + 3*Q, SNARE, 115)])
        evs.append((base + 2*Q + T2, TOM_HI,  88))
        evs.append((base + 3*Q,      TOM_MID, 82))   # lands with snare
        evs.append((base + 3*Q + T2, TOM_LO,  85))

    evs += _count_in(0)

    # 12-bar jazz blues (bars 1-12) ← loop start at bar 1
    jazz_bar(1,  crash=True)
    jazz_bar(2)
    jazz_bar(3)
    jazz_bar(4)
    jazz_bar(5,  crash=True)
    jazz_bar(6)
    jazz_bar(7)
    jazz_bar(8)
    jazz_bar(9,  crash=True)
    jazz_bar(10)
    jazz_build(11)
    jazz_fill(12)

    return evs, 120


# ---------------------------------------------------------------------------
# Funk — 16th-note pocket, 100 BPM — 4-bar loop
#
# Kick 1 & 3, snare 2 & 4, 8th closed HH, open HH on "and of 4".
# Tambourine on all 4 eighth-note offbeats (1+, 2+, 3+, 4+).
# ---------------------------------------------------------------------------

def _gen_funk() -> Tuple[List[Event], int]:
    evs: List[Event] = []

    def funk_bar(bar_idx: int, crash: bool = False) -> None:
        base = _b(bar_idx)
        if crash:
            evs.append((base, CRASH, 127))
        evs.extend([(base,       KICK,  110), (base + 2*Q, KICK,  100)])
        evs.extend([(base + 1*Q, SNARE, 110), (base + 3*Q, SNARE, 110)])
        for i in range(8):
            note = HH_OPEN if i == 7 else HH_CLOSED
            evs.append((base + i * E, note, 80 if i % 2 == 0 else 60))
        for beat in range(4):
            evs.append((base + beat * Q + E, TAMBOURINE, 80))

    evs += _count_in(0)

    # 4-bar loop (bars 1-4) ← loop start at bar 1
    for bar_idx in range(1, 5):
        funk_bar(bar_idx, crash=(bar_idx == 1))

    return evs, 100


# ---------------------------------------------------------------------------
# Reggae — one-drop, 80 BPM — 4-bar loop
#
# Kick on beat 3 only. Snare 2 & 4. Pedal HH on offbeats.
# Tambourine on 8th offbeats. Open HH on "and of 3". Crash on bar 3.
# ---------------------------------------------------------------------------

def _gen_reggae() -> Tuple[List[Event], int]:
    evs: List[Event] = []

    def reggae_bar(bar_idx: int, crash: bool = False) -> None:
        base = _b(bar_idx)
        if crash:
            evs.append((base, CRASH, 127))
        evs.append((base + 2*Q, KICK, 105))
        evs.extend([(base + 1*Q, SNARE, 100), (base + 3*Q, SNARE, 100)])
        # Pedal HH on offbeats (and of 1, 2, 3, 4)
        for beat in range(4):
            evs.append((base + beat * Q + E, HH_PEDAL, 65))
        # Open HH on and-of-3 for reggae colour
        evs.append((base + 2*Q + E, HH_OPEN, 75))
        # Tambourine on 8th offbeats
        for beat in range(4):
            evs.append((base + beat * Q + E, TAMBOURINE, 70))

    evs += _count_in(0)

    # 4-bar loop (bars 1-4) ← loop start at bar 1
    for bar_idx in range(1, 5):
        reggae_bar(bar_idx, crash=(bar_idx == 1))

    return evs, 80


# ---------------------------------------------------------------------------
# Gospel — driving soul groove, 85 BPM — 8-bar loop
#
# Kick 1 & 3, snare 2 & 4, 8th closed HH.
# Tambourine on 8th offbeats. Crash on bars 3 & 7.
# Bar 10: triplet tom fill.
# ---------------------------------------------------------------------------

def _gen_gospel() -> Tuple[List[Event], int]:
    evs: List[Event] = []

    def gospel_bar(bar_idx: int, crash: bool = False) -> None:
        base = _b(bar_idx)
        if crash:
            evs.append((base, CRASH, 127))
        evs.extend([(base,       KICK,  110), (base + 2*Q, KICK,  100)])
        evs.extend([(base + 1*Q, SNARE, 115), (base + 3*Q, SNARE, 115)])
        for i in range(8):
            evs.append((base + i * E, HH_CLOSED, 85 if i % 2 == 0 else 65))
        # Tambourine on offbeats
        for i in range(4):
            evs.append((base + i * Q + E, TAMBOURINE, 75))

    def gospel_fill(bar_idx: int) -> None:
        base = _b(bar_idx)
        evs.extend([(base, KICK, 110), (base + 2*Q, KICK, 100)])
        evs.append((base + 1*Q, SNARE, 115))
        for i in range(4):
            evs.append((base + i * E, HH_CLOSED, 85 if i % 2 == 0 else 65))
        fill  = [TOM_HI, TOM_HI, TOM_MID, TOM_MID, TOM_LO, SNARE]
        vels  = [90,     85,     95,      90,       100,    120]
        for i, (note, vel) in enumerate(zip(fill, vels)):
            evs.append((base + 2*Q + i * T, note, vel))

    evs += _count_in(0)

    # 8-bar loop (bars 1-8) ← loop start at bar 1
    gospel_bar(1,  crash=True)
    gospel_bar(2)
    gospel_bar(3)
    gospel_bar(4)
    gospel_bar(5,  crash=True)
    gospel_bar(6)
    gospel_bar(7)
    gospel_fill(8)

    return evs, 85


# ---------------------------------------------------------------------------
# Hard Rock — power groove, 120 BPM — 8-bar loop
#
# Kick: 1, "and of 1", 3.  Snare 2 & 4 full power.  8th closed HH.
# Crash on bars 3 & 7. Bar 10: 8th tom fill.
# ---------------------------------------------------------------------------

def _gen_hardrock() -> Tuple[List[Event], int]:
    evs: List[Event] = []

    def rock_bar(bar_idx: int, crash: bool = False) -> None:
        base = _b(bar_idx)
        if crash:
            evs.append((base, CRASH, 127))
        evs.extend([(base,       KICK,  115),
                    (base + E,   KICK,   95),
                    (base + 2*Q, KICK,  110)])
        evs.extend([(base + 1*Q, SNARE, 115), (base + 3*Q, SNARE, 115)])
        for i in range(8):
            evs.append((base + i * E, HH_CLOSED, 80))

    def rock_fill(bar_idx: int) -> None:
        base = _b(bar_idx)
        evs.extend([(base, KICK, 115), (base + E, KICK, 95), (base + 2*Q, KICK, 110)])
        evs.append((base + 1*Q, SNARE, 115))
        for i in range(4):
            evs.append((base + i * E, HH_CLOSED, 80))
        fill_notes = [TOM_HI, TOM_MID, TOM_LO, SNARE]
        fill_vels  = [100,    95,      100,     120]
        for i, (note, vel) in enumerate(zip(fill_notes, fill_vels)):
            evs.append((base + 2*Q + i * E, note, vel))

    evs += _count_in(0)

    # 8-bar loop (bars 1-8) ← loop start at bar 1
    rock_bar(1,  crash=True)
    rock_bar(2)
    rock_bar(3)
    rock_bar(4)
    rock_bar(5,  crash=True)
    rock_bar(6)
    rock_bar(7)
    rock_fill(8)

    return evs, 120


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

PATTERNS = {
    "blues":    _gen_blues,
    "jazz":     _gen_jazz,
    "funk":     _gen_funk,
    "reggae":   _gen_reggae,
    "gospel":   _gen_gospel,
    "hardrock": _gen_hardrock,
}


def main() -> None:
    repo_root = Path(__file__).resolve().parent.parent
    out_dir   = repo_root / "assets" / "patterns"
    out_dir.mkdir(parents=True, exist_ok=True)

    print(f"gen_patterns: writing to {out_dir}")
    for name, fn in PATTERNS.items():
        events, bpm = fn()
        _write_mid(out_dir / f"{name}.mid", events, bpm)
    print("Done.")


if __name__ == "__main__":
    main()
