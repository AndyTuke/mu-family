# Archived design docs

Stage-plan or audit docs that described work now complete. Kept here as historical record — not deleted, in case future work needs to revisit the reasoning.

| File | What it describes | Status |
|---|---|---|
| `design-seamless-hotswap.md` | Stage 34 plan for polyphonic voice-tail hot-swap. | Closed (Stage 34 completed; closure note in [DevelopmentHistory.md](../../DevelopmentHistory.md)). T11 listening regression covers it. |
| `design-stage35-preset-format.md` | Stage 35 plan for v2 preset format (rename `drv*`→`ins*`, de-normalise, string algorithm names). | Closed at v1.0.520. Schema reference now lives in [preset-format.md](../preset-format.md). |
| `knob-size-audit.md` | One-shot audit log of voice / mixer / FX knob sizes against the design spec. | Findings actioned; doc is a snapshot of pre-fix state. |

If you find yourself reading one of these, double-check the implementation against current code — the doc captures intent at the time, not necessarily what shipped.
