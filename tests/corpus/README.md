# Compat corpus

Real preset files per product, used by the layer-extraction safety net. Every fixture must load through the real
headless processor, match its frozen parameter dump, re-save to the frozen baseline bytes, and re-save
idempotently (see `mu-core/Tests/FrozenBaselineHarness.h`).

Per product folder:

- the fixtures (`*.muClid`, `*.muRhythm`, `*.muTant`, `*.muPattern`, `*.muToni`, `*.muArp`, `*.muOn`, `*.muPerformance`)
- `<fixture>.params.txt`: frozen load result (layers, load notices, re-save size and SHA-256, parameters that differ from default)
- `CROSSLOAD.txt`: what other products' layer and full presets do when offered to this product
- `MANIFEST.txt`: SHA-256 and size of every file in the folder
- `FACTORY_STATE.<ext>`: a freshly saved factory-state full preset (mu-Toni and mu-On have no shipped presets)

Sources (copied read-only; the content folders were not touched):

| Product | Fixtures |
|---|---|
| mu-clid | 9 full presets and 2 performances from `Documents/TDP/muClid`, 15 of 63 rhythms (even spread), plus `TS1`, `A3_filter`, `A9_base`, `T11`, `A9_hat` from `tests/presets` |
| mu-tant | 15 of 108 full presets and 20 of 100 patterns (even spread) from `Documents/TDP/muTant` |
| mu-toni | `TONI_legacy`, `TONI_silent`, `TONI_mute_layer` from `tests/presets` + factory state |
| mu-on | `ON_legacy`, `ON_silent` from `tests/presets` + factory state |

Sample files are not part of the corpus: a fixture that names a sample reports "missing from content folder" as a
frozen load notice. Anything else reported on load is a test failure.

A deliberate change goes through `tests/scripts/regenerate-frozen-baselines.ps1 -Reason "<why>"`.