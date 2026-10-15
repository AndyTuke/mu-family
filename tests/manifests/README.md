# Frozen parameter manifests

One file per product (`mu-clid.txt`, `mu-tant.txt`, `mu-toni.txt`, `mu-on.txt`): every APVTS parameter of a
freshly constructed real processor, sorted by id, with type, min, max, step, skew, plain default and normalised
default, plus a per-prefix count table (ids per layer). The id set is also proven independent of the layer count
(the test grows the processor to the layer capacity and compares).

Checked by `FrozenBaselineTests.cpp` in each product's test target (`mu-<product>-frozen` category). A failure
prints the lines that changed. These files are never rewritten by a normal run.

**Deliberate change:** run `tests/scripts/regenerate-frozen-baselines.ps1 -Reason "<why>"` (needs the Debug test
executables built), read `git diff tests/manifests tests/corpus`, and commit the result with the change.