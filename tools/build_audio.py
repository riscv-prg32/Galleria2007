#!/usr/bin/env python3
"""Compose the original Galleria 2007 adaptive soundtrack.

Writes ``audio/audio.json`` (input for PRG32's ``tools/prg32audio_pack.py``)
and ``src/gen/audio_ids.h`` (track ids, tempi, instrument ids for C).

All music is original and written here as short note lists; it does not
quote or imitate any film or series theme. Each adaptive state is one
tracker track (see docs/audio.md):

  EXPLORATION  restrained D-dorian theme: curiosity rather than triumph
  DISCOVERY    one-shot rising motif, used sparingly
  MYSTERY      low pulse, tritone pad, sparse glassy notes
  THREAT       sparse rhythmic ostinato
  CHASE        stronger pulse and noise percussion, kept short
  MEMORY       intimate triangle melody for the shelter
  SILENCE      no track: only environmental sounds

The PRG32 tracker plays NOTE_ON on channel N with instrument N, so music
uses channels/instruments 0..3; sound effects use instruments 4..9 on
channels 4..5 through prg32_audio_note(). Only procedural (SID-like)
instruments are used, so the AUDIO block contains no PCM bytes.
"""
from __future__ import annotations

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def synth(wave, pw=8, cutoff=15, res=0):
    waves = {"tri": 0, "saw": 1, "pulse": 2, "noise": 3}
    return 0x8000 | ((res & 3) << 10) | ((cutoff & 15) << 6) | ((pw & 15) << 2) | waves[wave]


INSTRUMENTS = [
    # music
    dict(name="BASS", pan=0, sample_id=synth("pulse", 5, 7, 1), default_volume=150, attack=2, decay=30, sustain=150, release=40),
    dict(name="PAD", pan=-28, sample_id=synth("tri", 8, 9, 0), default_volume=95, attack=40, decay=40, sustain=200, release=80),
    dict(name="LEAD", pan=24, sample_id=synth("saw", 8, 8, 1), default_volume=85, attack=6, decay=40, sustain=120, release=60),
    dict(name="PERC", pan=14, sample_id=synth("noise", 8, 11, 0), default_volume=60, attack=0, decay=6, sustain=0, release=6),
    # effects
    dict(name="CLICK", sample_id=synth("pulse", 3, 15, 0), default_volume=140, attack=0, decay=3, sustain=0, release=3),
    dict(name="STEP", sample_id=synth("noise", 8, 4, 0), default_volume=110, attack=0, decay=8, sustain=0, release=6),
    dict(name="CHIME", sample_id=synth("tri", 8, 15, 0), default_volume=150, attack=1, decay=40, sustain=60, release=60),
    dict(name="RUMBLE", sample_id=synth("noise", 8, 2, 2), default_volume=220, attack=10, decay=90, sustain=120, release=90),
    dict(name="DRIP", sample_id=synth("tri", 8, 15, 0), default_volume=90, attack=0, decay=10, sustain=0, release=10),
    dict(name="ALERT", sample_id=synth("saw", 8, 10, 2), default_volume=160, attack=1, decay=50, sustain=90, release=60),
    dict(name="HEAVY", sample_id=synth("noise", 8, 3, 1), default_volume=150, attack=0, decay=10, sustain=0, release=8),
]

# Note helpers (MIDI). Tracker step = one 16th note.
N = {n: i for i, n in enumerate(["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"])}


def m(name: str) -> int:
    note, octave = name[:-1], int(name[-1])
    return 12 * (octave + 1) + N[note]


def seq(spec: str, start=0):
    """'D4:4 F4:2 -:2 ...' -> [(step, note, length)]; '-' is a rest."""
    out, t = [], start
    for tok in spec.split():
        name, length = tok.split(":")
        length = int(length)
        if name != "-":
            out.append((t, m(name), length))
        t += length
    return out


def bars(pattern: str, roots, steps_per_bar=16):
    """Repeat a bass pattern of scale-degree offsets over a list of roots."""
    out = []
    for b, root in enumerate(roots):
        t = b * steps_per_bar
        for tok in pattern.split():
            off, length = tok.split(":")
            length = int(length)
            if off != "-":
                out.append((t, m(root) + int(off), length))
            t += length
    return out


def track(name, bpm, length, channels, loop=True, volumes=None, pans=()):
    """Merge channel note lists (and stereo pan moves) into tracker events.

    ``pans`` holds (step, channel, pan) moves, pan in -64..63. In mono the
    firmware accepts and ignores them, so the music stays mono-safe."""
    events = []   # (time, order, command, arg0, arg1)
    for (t, ch, pan) in pans:
        events.append((t, -1, "SET_PAN", ch, pan & 0xFF))
    for ch, notes in channels.items():
        for (t, note, ln) in notes:
            events.append((t, 1, "NOTE_ON", ch, note))
            events.append((min(t + ln, length), 0, "NOTE_OFF", ch, 0))
    events.sort(key=lambda e: (e[0], e[1], e[3]))
    out = [dict(delta=0, command="SET_TEMPO", arg0=bpm)]
    for ch, vol in (volumes or {}).items():
        out.append(dict(delta=0, command="SET_VOLUME", arg0=ch, arg1=vol))
    def wait(ticks):
        # The delta of an event is the wait *after* it; long waits are split
        # with harmless SET_TEMPO events because deltas are 8-bit.
        while ticks > 0:
            room = 255 - out[-1]["delta"]
            take = min(room, ticks)
            out[-1]["delta"] += take
            ticks -= take
            if ticks:
                out.append(dict(delta=0, command="SET_TEMPO", arg0=bpm))
    now = 0
    for (t, _, cmd, a0, a1) in events:
        wait(t - now)
        now = t
        out.append(dict(delta=0, command=cmd, arg0=a0, arg1=a1))
    wait(length - now)
    for ch in channels:
        out.append(dict(delta=0, command="NOTE_OFF", arg0=ch, arg1=0))
    out.append(dict(delta=0, command="JUMP" if loop else "END", arg0=0, arg1=0))
    return dict(name=name, bpm=bpm, loop=loop, steps=length, events=out)


def compose():
    tracks = []
    # EXPLORATION: 8 bars, Dm - C - Gm - A (dorian colour), restrained.
    roots = ["D2", "C2", "G1", "A1", "D2", "C2", "A#1", "A1"]
    bass = bars("0:6 -:2 7:4 12:2 -:2", roots)
    pad = seq("F3:16 E3:16 D3:16 C#3:16 F3:16 G3:16 F3:16 E3:16")
    lead = seq("-:4 D4:2 F4:2 G4:4 A4:4 -:4 C5:2 A4:2 G4:8 "
               "-:4 F4:2 G4:2 A4:6 G4:2 E4:8 -:8 "
               "-:4 D4:2 F4:2 G4:4 A4:4 -:4 C5:2 D5:2 A4:8 "
               "-:4 G4:2 F4:2 E4:4 C#4:4 D4:12 -:4")
    perc = [(b * 16 + o, 70, 1) for b in range(8) for o in (4, 12)]
    # The lead answers itself across the stereo field, phrase by phrase.
    tracks.append(track("EXPLORATION", 84, 128, {0: bass, 1: pad, 2: lead, 3: perc},
                        volumes={2: 70, 3: 40},
                        pans=[(0, 2, 22), (32, 2, -18), (64, 2, 26), (96, 2, -12),
                              (0, 1, -30), (64, 1, -20)]))
    # DISCOVERY: one-shot rising motif (2 bars).
    # The rising motif opens the stereo image: from centre to wide.
    tracks.append(track("DISCOVERY", 96, 40, {
        0: seq("D2:16 F2:8 A2:16"),
        1: seq("A3:8 C4:8 D4:24"),
        2: seq("D4:3 F4:3 A4:3 D5:7 E5:4 F5:4 A5:16"),
    }, loop=False, pans=[(0, 1, -8), (0, 2, 8), (9, 1, -24), (9, 2, 24), (16, 1, -44), (16, 2, 44)]))
    # MYSTERY: low pulse on D1, tritone pad, sparse high notes.
    bass = [(t, m("D2"), 2) for t in range(0, 128, 4)]
    pad = seq("G#3:32 A3:32 G#3:32 G3:32")
    lead = seq("-:20 E5:3 -:29 C#5:2 -:10 D#5:3 -:30 E5:2 -:20 G#4:4 -:8")
    # Glassy notes drift from side to side: you cannot tell where they come from.
    tracks.append(track("MYSTERY", 70, 128, {0: bass, 1: pad, 2: lead}, volumes={0: 110, 2: 55},
                        pans=[(18, 2, -50), (50, 2, 48), (62, 2, -30), (98, 2, 54), (118, 2, -54),
                              (0, 1, -36), (64, 1, 36)]))
    # THREAT: sparse ostinato.
    bass = bars("0:2 -:2 0:2 1:2 -:4 0:2 -:2", ["D2"] * 4)
    perc = [(b * 16 + o, 60, 1) for b in range(4) for o in (0, 10)]
    pad = seq("C#3:32 C3:32")
    tracks.append(track("THREAT", 100, 64, {0: bass, 1: pad, 3: perc}, volumes={3: 70},
                        pans=[(0, 3, -40), (10, 3, 40), (16, 3, -40), (26, 3, 40),
                              (32, 3, -40), (42, 3, 40), (48, 3, -40), (58, 3, 40)]))
    # CHASE: driving 8ths, rising stabs, kept to 4 bars.
    bass = bars("0:2 0:2 7:2 0:2 0:2 7:2 8:2 7:2", ["D2", "D2", "D#2", "C2"])
    perc = [(t, 64 if t % 4 else 76, 1) for t in range(0, 64, 2)]
    lead = seq("D4:2 -:6 F4:2 -:6 G#4:2 -:6 A4:2 -:6 D#4:2 -:6 F#4:2 -:6 C5:2 -:6 B4:4 A4:4")
    # Percussion ping-pongs every 8th note; the stabs alternate sides.
    tracks.append(track("CHASE", 144, 64, {0: bass, 2: lead, 3: perc}, volumes={2: 90, 3: 75},
                        pans=[(t, 3, -46 if (t // 2) % 2 == 0 else 46) for t in range(0, 64, 2)]
                        + [(t, 2, -30 if (t // 8) % 2 == 0 else 30) for t in range(0, 64, 8)]))
    # MEMORY: intimate melody (triangle) over soft roots, A minor / F / C / E.
    melody = seq("E4:6 D4:2 C4:4 A3:4 F4:6 E4:2 D4:8 E4:4 G4:4 C4:8 B3:6 C4:2 D4:4 E4:4 "
                 "A4:6 G4:2 E4:4 C4:4 F4:4 E4:4 D4:8 C4:6 B3:2 C4:4 D4:4 A3:16")
    bass = seq("A2:16 F2:16 C2:16 E2:16 A2:16 D2:16 G2:16 A2:16")
    # Intimate and close: melody slightly left, bass right, gently.
    tracks.append(track("MEMORY", 66, 128, {0: bass, 1: melody}, volumes={0: 90, 1: 120},
                        pans=[(0, 1, -12), (0, 0, 10)]))
    return tracks


def main():
    tracks = compose()
    cfg = {
        "_comment": "Generated by tools/build_audio.py. Original music; procedural instruments only.",
        "instruments": [{k: v for k, v in i.items() if k not in ("name", "pan")} | {"default_pan": i.get("pan", 0)} for i in INSTRUMENTS],
        "tracks": [{"events": t["events"]} for t in tracks],
    }
    (ROOT / "audio").mkdir(exist_ok=True)
    (ROOT / "audio/audio.json").write_text(json.dumps(cfg, indent=1) + "\n", encoding="utf-8")
    h = ["/* Generated by tools/build_audio.py. Do not edit. */",
         "#ifndef G2007_GEN_AUDIO_IDS_H", "#define G2007_GEN_AUDIO_IDS_H", ""]
    for i, t in enumerate(tracks):
        h.append(f"#define TRACK_{t['name']} {i}")
        h.append(f"#define TRACK_{t['name']}_BPM {t['bpm']}")
        h.append(f"#define TRACK_{t['name']}_STEPS {t['steps']}")
    for i, ins in enumerate(INSTRUMENTS):
        h.append(f"#define INSTR_{ins['name']} {i}")
    h += ["", "#endif", ""]
    (ROOT / "src/gen/audio_ids.h").write_text("\n".join(h), encoding="utf-8")
    n = sum(len(t["events"]) for t in tracks)
    print(f"audio: {len(tracks)} tracks, {len(INSTRUMENTS)} instruments, {n} events "
          f"(~{n * 4 + len(tracks) * 8 + len(INSTRUMENTS) * 8 + 40} bytes)")


if __name__ == "__main__":
    main()
