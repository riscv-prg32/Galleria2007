# Multiplayer

Up to four players share one world. There is no mode selector: the same rules
produce solo play, cooperation and rivalry (scarce backpack slots, useful
objects, information, route choice, being first to record a place). There is
no player-versus-player damage.

## What PRG32 provides

`prg32_multiplayer_join("galleria2007-v1", PRG32_MP_FLAG_ENABLE)` opts in.
Multiplayer is an **optional** feature of the cartridge: the game joins only
when the host advertises `PRG32_FEATURE_MULTIPLAYER` (the ESP32-C6 and QEMU
firmware and PRG32-QT do; PRG32-iOS does not and runs the full solo game). On
the ESP32-C6 the firmware relays, about every 50 ms, each player's latest
snapshot through the PRG32 MultiplayerServer:

```c
int16_t x, y; uint16_t sprite; uint16_t flags; uint32_t input /* 7 bits kept */; uint32_t frame;
```

There are no reliable messages and no custom payloads. In QEMU the firmware's
offline stub accepts the join and reports no peers, so the same code runs
solo. The signature is shared by every language edition: Italian and English
players meet in the same world.

## State gossip

Every player keeps re-publishing its view of the shared world, one 29-bit
**record** at a time, and merges what it receives with rules that converge
regardless of loss, duplication or order (`src/net_proto.c`):

| Field | Bits | Meaning |
|---|---|---|
| `x`, `y` | 16 + 16 | player position (cm) |
| `sprite` 0–7 | 8 | view angle (10-bit angle >> 2) |
| `sprite` 8 | 1 | torch on (also makes the player more visible to others) |
| `sprite` 9 | 1 | forcing the 2007 wall |
| `sprite` 10–15, `input` 0–6, `flags` 0–15 | 29 | one record |

Record kinds (bit 28):

- **World record** — progression flags (`WF_*`: clues, wall identified/open,
  gate/grate open, cavity entered, stairs found, …). Merged with **OR**: the
  state is monotonic, so a stale packet can never close the passage.
- **Item record** — `item(4) | version(5) | state(2) | payload(16)`; payload is
  the carrier's 16-bit random tag, or the dropped position on an 8-bit grid
  (40 cm cells). A **last-writer-wins register** per instance:
  - a newer 5-bit version (serial-number arithmetic) wins;
  - equal versions are tie-broken by the larger `(state, payload)` key, so a
    simultaneous pickup resolves identically on every board; the loser's
    backpack drops the instance and shows *UN ALTRO L'HA PRESO PRIMA*;
  - drops are placed on the shared grid by the dropper itself, so all peers
    converge on the same position and sector.

Sending: changed items first (each record is held for two 33 ms ticks so the
50 ms sampler sees it), then a round robin over the world record and every
item that has ever changed. Receiving: snapshots whose `frame` is not newer
than the last one seen from that peer are ignored; malformed records
(reserved bits, out-of-range item, impossible state, off-map drop) are
rejected without side effects.

Local movement never waits for the network. Remote players are drawn at their
latest position with a per-player jacket colour and, when their torch is on,
a lamp glow. Archive discoveries and statistics are per player; world
progression is shared.

## Cooperation

Another player within 3.5 m of the 2007 wall halves the forcing time (the
helper can also watch the corridor). Four players are never required.

## Tests

`tests/test_protocol.c` covers packing round trips, malformed input, stale
frames, duplicate-pickup convergence in either delivery order, late stale
pickups after a drop, and the monotonic wall flag. `tests/test_game.c`
injects a peer through the host PRG32 implementation and checks the shared
wall opening and a lost simultaneous pickup inside the real game loop.
Physical multi-board acceptance is listed in [acceptance.md](acceptance.md).
