# mu-Tant: UI layout notes

Family rules are in [../design-ui-family.md](../design-ui-family.md); this file holds only mu-Tant specifics.

## Layer content band fit (PatternContent, 464)

Built 2026-10-12. The band (`kLayerContentH` = 464) is fully tiled:

| Part | Height |
|---|---|
| Root / Scale row: 7 inset + 24 control + 16 plate | 47 |
| Row A | 64 |
| Plate | 16 |
| Row B (`kVoiceSubH`, regrouped from 144) | 116 |
| Inset | 7 |
| Gate editor (flex) = 464 - 250 | 214 |

The gate editor is the flex panel: the grid grows from 134 to 148 px. The Root / Scale label and gap widths are tokens (`kTonalLabelW`, `kTonalRootW`, `kTonalScaleW`, `kTonalGap`) in `MuLookAndFeel`.

## Root / Scale row: shared Key and Scale selector (ruled 2026-11-19)

The row's Root and Scale labels and two dropdowns are replaced by the shared `mu_ui::KeyScaleSelector` ([design-ui-family.md section 6.8](../design-ui-family.md#68-key-and-scale-selector-ruled-2026-11-19)): caption "Key" (`kDropdownCaptionW`) and one LCD of width `kTonalRootW + kTonalScaleW`, left-aligned at `g.pad` where the Root label is today. Height is unchanged (47: inset + `g.ddH` + plate), so the 464 budget is untouched. The Note mode "MIDI" selector stays right-aligned. Still the global `root` / `scale` ids. `kTonalLabelW` and `kTonalGap` retire; `kTonalRootW` and `kTonalScaleW` stay. Status bar: Key / Scale as in 6.8.
