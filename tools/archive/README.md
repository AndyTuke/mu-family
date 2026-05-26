# Archived dev tools

One-shot scripts kept for historical reference but no longer needed.

| File | Purpose | Retired at |
|---|---|---|
| `upgrade_presets_v0_to_v2.py` | Convert v0/v1 normalised preset values to v2 actual-value format. Pre-distribution era; library already migrated. | v1.0.605 (#643 removed version gating) |
| `migrate_presets_to_v3.py` | Collapse 9 named insert fields → 4 generic `insP*` slots in shipped preset files. Same logic as `PresetIO::migrateInsertSlotsV3` runtime path. | v1.0.588 (Stage 36) |

These are kept here, not deleted, in case the migration logic ever needs auditing or a one-off re-run on an old preset that didn't get touched at the time.
