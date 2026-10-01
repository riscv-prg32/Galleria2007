# Provisional content awaiting Galleria Borbonica validation

Everything below is a placeholder designed to be replaced locally, without
engine changes (brief §35).

| Item | Provisional state | Where | Replacement |
|---|---|---|---|
| Overall map geometry | compressed topological layout, invented sizes | `assets/map/galleria2007.json` | convert the official plan/2007 survey to the same JSON; keep sector ids |
| Access stairs and gate A | gameplay devices (`GAME`) | map | adapt to the documented 2007 access |
| Cistern overlook | reconstruction (`RECON`) | map sectors `cistern_*` | validated position/shape |
| Shelter rooms | reconstruction | `shelter*` | validated plan; documented inscriptions |
| Walled passage interior | fiction | `walled_passage` | 2007 documentation |
| Large cavity shape | reconstruction | `large_cavity` | survey |
| Grate connector | gameplay/fiction | `grate_connector` | remove or justify |
| 75-step staircase | 12 steps of 25 cm (`GAME`) | `pozzari_step_*` | 75 steps if performance allows (portal depth) |
| Vico del Grottone room | reconstruction | `vico_del_grottone` | validated |
| Vehicles | generic period silhouettes | `tools/build_assets.py` | sprites traced from licensed photos |
| Wartime writings | generic clue text | `lang/*.json` `N_WRITINGS` / `A2_*` | approved inscriptions |
| All textures | procedural | `tools/build_assets.py` | licensed photo-derived 32×32 patterns |
| Outreach screen | "awaiting approval" text | `lang/*.json` `INFO_BODY` | partner-approved wording, URL/QR |
| Store homepage | project repository | `metadata/metadata.*.json` | approved official page |
| Store ids | `org.riscv-prg32.galleria2007[.en]` | metadata | owner-approved reverse-DNS ids |
| License field | MIT (code/original assets) | metadata, colophon | final decision once rights for branding/photos/texts are settled |
