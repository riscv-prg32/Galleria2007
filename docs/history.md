# History, provenance and guardrails

**The real Galleria determines the identity of the game; PRG32 determines its
technical language.** The game must never imply that invented plot events are
historical facts.

## Historical anchor (verified 2026-10-01)

From the official history page (`https://www.galleriaborbonica.com/it/storia/`,
recorded in [SOURCES.md](../assets/source/SOURCES.md)), paraphrased:

- 1853: Ferdinand II of Bourbon decrees an underground viaduct beneath Monte
  Echia, designed by architect Errico Alvino, as a military passage and escape
  route towards the Royal Palace; works 1853–1855; 431 m.
- 1939–1945: between 5,000 and 10,000 Neapolitans shelter in the tunnel and
  adjacent spaces; lighting, sanitation and whitewashed walls are added.
- Post-war to 1970: Municipal Judicial Deposit for objects recovered from the
  rubble of the bombing.
- **2007:** geologists find a walled passage leading to another large cavity
  that had been adapted as an air-raid shelter, and identify an older access
  used in the seventeenth century by the *pozzari*: a narrow staircase of
  **75 steps** towards Vico del Grottone.

These facts are the spine of the vertical slice and the content of the six
historical Archive entries (`lang/*.json`, keys `A0_*`–`A5_*`). The seventh
entry, *Finzione del gioco*, states explicitly that LampMan, the footprints,
the reworked plaster and the map are inventions.

## Provenance labels

Every sector and entity in `assets/map/galleria2007.json` carries one label;
`assets/map/map_report.md` lists them all.

| Label | Meaning | Examples |
|---|---|---|
| `HIST` | documented fact or object | the 2007 cavity, the 75-step access |
| `PUBLIC` | appearance/placement inferred from public official material | the Bourbon tunnel, vehicles in the deposit, digging marks |
| `RECON` | plausible reconstruction awaiting validation | cistern overlook, shelter rooms, the cavity's shape |
| `GAME` | gameplay alteration | access stairs, gate A, the grate, the 12-step staircase, items |
| `FICTION` | narrative invention | LampMan, footprints, fresh plaster, the air draught clue, the walled-passage interior |

The labels are development metadata: the Archive shows only *documented
history* vs *game fiction* to players.

## Guardrails applied

- The map is described everywhere as **provisional topology, not a survey**
  (map JSON notice, map report, Archive, metadata, colophon).
- The 2007 puzzle is a dramatisation; the game does not claim the geologists
  opened the wall this way.
- No generic supernatural content: LampMan is human; no zombies or monsters.
- No invented casualties or named victims; wartime writings are generic.
- No collectible human remains or war memorabilia; historical objects are
  *examined and recorded*, never taken; the player never damages inscriptions.
  The wall the player forces is the walled-up passage itself — the documented
  discovery.
- No modern tourist signage or lighting in the 2007 scenes.
- No unlicensed web photography is shipped; official photos are reference
  only until permission is recorded.
- No institutional partnership is stated; the outreach screen says official
  information is awaiting approval.
