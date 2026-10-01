# Gameplay

## Controls

| Input | Play | Backpack / Archive |
|---|---|---|
| UP / DOWN | walk forward / back | select |
| LEFT / RIGHT | turn | switch Backpack ⇄ Archive page |
| A | examine, take, use, open, record; **hold** to force the 2007 wall | use item / drop (press twice) / read entry |
| B | torch on/off | close |
| A+B | open the backpack | close |
| START | open the backpack (shortcut) | close |

There is no fire button. A and B are resolved by a chord state machine
(`src/input.c`): when one goes down the game waits up to 110 ms for the
other. Both inside the window fire **only** A+B; a single press fires when
released or when the window expires. The chord needs a full release before it
can fire again, so opening the backpack never toggles the torch.

## Interaction

The target is the best candidate within reach (170 cm, 230 cm with the
torch), inside a cone around the view centre, and not hidden behind a wall
(checked against the last frame's z-buffer). Priority: doors → collectible
items → historical features → puzzle features. Prompts: `A ESAMINA`,
`A PRENDI`, `A USA`, `A APRI`, `A REGISTRA`, `A MURATURA DIFFERENTE`,
`TIENI A: FORZA`, `ZAINO PIENO`.

Historical objects are **examined and recorded**, never taken: the game does
not reward removing heritage objects.

## Backpack

Five slots (`BACKPACK_SLOTS`) hold item **instances**. Items: notebook,
camera, rope, battery, key A, key B, crowbar, provisional map — eight
instances for five slots, so choices matter.

- Taking an item with a full backpack opens *ZAINO PIENO*: the new object is
  shown, the player picks one carried object to leave, it is dropped at the
  player's position and the new one takes its slot.
- Dropped items persist in the shared world and anyone can pick them up.
- Every item is an entry of a fixed instance pool (`MAX_WORLD_DROPS = 16`):
  items move, they are never created or destroyed, so the pool cannot run
  out. A drop is refused (`NON PUOI LASCIARLO QUI`) when no valid position
  exists; the item stays in the backpack.
- Puzzle-critical items (rope, keys, crowbar) are never lost. Keys are
  consumed only by opening their lock.
- The battery boosts the torch for 90 s; the map shows a provisional
  topological sketch.

## The vertical slice

| Beat | Place (zone) | What happens |
|---|---|---|
| 1 | title card | *NAPOLI – 2007*; enter underground |
| 2 | access, Bourbon tunnel | learn movement and A; take the notebook/camera; record digging marks (Archive 1); learn B |
| — | cistern overlook | height difference down to the water; with the rope, recover key A (Archive 2) |
| 3 | vehicle deposit (past gate A) | period vehicles (Archive 4: judicial deposit); a distant light crosses the hall and vanishes behind a grate |
| 4 | shelter | whitewashed walls, wartime writings (Archive 3); register three clues |
| 5 | shelter | fresh footprints: someone else is here (fiction, Archive 7) |
| 6 | shelter, west wall | the prompt changes to *MURATURA DIFFERENTE*; the crowbar is needed |
| 7 | the 2007 wall | hold A ~4 s (≈2 s with another player nearby); dust, rumble, **music stops** |
| 8 | large cavity | Archive 5 (the real 2007 discovery); discovery motif |
| 9 | cavity | LampMan's light returns, close enough to matter |
| 10 | pozzari staircase | Archive 6 (75 steps); climb to Vico del Grottone; end card |

The end card (*GALLERIA 2007 — LA SCOPERTA È SOLO L'INIZIO*) shows
exploration statistics (places found, finds recorded, history entries, game
secrets, items shared, exploration time) — no body count, no single winner.
Then: **ARCHIVIO**, **RIGIOCA**, **GALLERIA BORBONICA** (a replaceable,
partner-approved information screen; no web access is required).

Gate A, the grate, key B and the shortened 12-step staircase are gameplay
devices, labelled `GAME` in the map provenance.

## L'Uomo della Lampada (LampMan)

A fictional, human antagonist — no monsters, no weapons, no combat.

```text
PATROL --noise--> HEAR --0.6 s--> INVESTIGATE --arrive/11 s--> SEARCH --5 s--> RETURN --arrive--> PATROL
   \--sees--> SEE --0.5 s & still sees--> CHASE --2.5 s without sight--> SEARCH
```

- **Sight:** line of sight through open portals; range 18 m when the
  player's torch is on, 4.2 m when off, 1.4 m always.
- **Noise:** opening the wall (loud), gates, forcing masonry, picking items.
- **Movement:** shortest path over the sector graph (breadth-first search),
  aiming at portal midpoints. He knows the route behind the grate (fiction)
  and never climbs beyond the third stair step: the staircase is the escape.
- **Capture:** fade, *L'UOMO DELLA LAMPADA TI HA TROVATO*, back to the last
  checkpoint with all world progress kept; he returns to his patrol and
  ignores the player for 6 s. Nothing is lost.

Player responses: switch the torch off, break line of sight, take another
route (the grate, once key B opens it, links the cavity back to the vehicle
deposit), cooperate, or simply run for the stairs.

Each board simulates LampMan locally (v0.1): his state is not networked, so
two players may see him in different places. The scripted sighting and his
activation are triggered by shared world flags, so every player experiences
the same beats.
