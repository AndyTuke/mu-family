# Signalsmith Stretch prototype

Standalone console program (no JUCE, no mu-core) that measures how Signalsmith Stretch behaves when driven the way mu-Tilate would drive it. Not part of the product build and never built by `/build`. Results and recommendation: [docs/mu-tilate/stretch-prototype-results.md](../../../docs/mu-tilate/stretch-prototype-results.md).

```
cmake -S tests/prototypes/stretch -B build/stretch-proto
cmake --build build/stretch-proto --config Release
build/stretch-proto/Release/stretch_prototype.exe [outputBlock=256] [splitComputation=0] [windowPreset=0]
```

The last section prints the worst per-block cost with eight voices started on the same sample (the real-time risk, not the average). Window preset: 0 default (120 ms block), 1 cheaper (100 ms), 2 short (30 ms block, 7.5 ms hop). Build in Release for the CPU figures.

Libraries are vendored in `ThirdParty/signalsmith-stretch` and `ThirdParty/signalsmith-linear` (both MIT; keep their LICENSE files and the copyright notice in any shipped product).
