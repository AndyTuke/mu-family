// mu-layer-link-test: the four layer libraries compiled and linked into one throw-away console target.
// Nothing is run. The point is the build: four Source/ roots on one include path (a relative-path collision
// shows as the wrong header or a redefinition) and four sets of sources in one link (a duplicate symbol shows
// at link time). It is the last guard before a host links all four layer types (extraction-plan 2.6).
#include "Sequencer/SampleRhythm.h"       // mu-clid-layer
#include "Persistence/RhythmParams.h"
#include "Sequencer/Pattern.h"            // mu-tant-layer
#include "Persistence/PatternParams.h"
#include "Sequencer/Track.h"              // mu-on-layer
#include "Persistence/TrackParams.h"
#include "Sequencer/Arp.h"                // mu-toni-layer
#include "Persistence/ArpParams.h"

int main() { return 0; }
