#include "Sequencer/MidiArpCore.h"
#include "Persistence/LayerParamIds.h"   // mu-toni: layerParamId
#include "Persistence/ArpParams.h"       // mu-toni: vpi suffixes

namespace mu_toni_midi
{

MidiArpCore::MidiArpCore(juce::AudioProcessorValueTreeState& apvts)
{
    using namespace mu_toni;
    auto row = [&](int k) { return apvts.getRawParameterValue(layerParamId(0, vpi::suffix[k])); };
    scale = row(vpi::scale);   root = row(vpi::root);   roct = row(vpi::roct);   chord = row(vpi::chord);
    inv   = row(vpi::inv);     octs = row(vpi::octs);   dir  = row(vpi::dir);    rate  = row(vpi::rate);
    gate  = row(vpi::gate);    leg  = row(vpi::leg);    snap = row(vpi::snap);   trig  = row(vpi::trig);
    acc   = row(vpi::acc);     accLen = row(vpi::accLen); accPat = row(vpi::accPat); stepMask = row(vpi::stepMask);
    inCh  = apvts.getRawParameterValue(kMidiInChId);
    outCh = apvts.getRawParameterValue(kMidiOutChId);
    vel   = apvts.getRawParameterValue(kMidiVelId);
}

void MidiArpCore::prepare(double sr, int blockSize)
{
    sampleRate = sr > 0.0 ? sr : 44100.0;
    maxBlock = juce::jmax(1, blockSize);
    runner.prepare(sampleRate, maxBlock);
    dummy.setSize(1, maxBlock);
    outBuf.ensureSize(4096);
    reset();
}

void MidiArpCore::reset() noexcept
{
    runner.reset();
    heldNotes.clear();
    lastInCh = -1;
    uiLastStep.store(-1);
    uiNote.store(-1);
    uiPlaying.store(false);
}

void MidiArpCore::flushNoteOff(juce::MidiBuffer& midi)
{
    auto& voice = runner.getVoice();
    const int note = voice.soundingNote();
    if (note < 0) return;
    midi.addEvent(juce::MidiMessage::noteOff(voice.soundingChannel(), note), 0);
    runner.reset();
    uiNote.store(-1);
}

void MidiArpCore::processBlock(bool playing, double startBeat, double bpm, juce::MidiBuffer& midi, int numSamples)
{
    if (numSamples <= 0) return;
    const int inChannel = juce::roundToInt(p(inCh));
    const bool inputReset = inChannel != lastInCh;   // MIDI In changed: a release on the old channel will not arrive
    if (inputReset) { heldNotes.clear(); lastInCh = inChannel; }

    // 1. The block's input: notes feed the held keys and are consumed; anything else passes through.
    outBuf.clear();
    bool noteOnEdge = false;
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        const bool isNote = m.isNoteOnOrOff();
        const bool isAllOff = m.isAllNotesOff() || m.isAllSoundOff();
        if (isNote || isAllOff)
        {
            mu_core::MidiEvent e {};
            e.frame = meta.samplePosition;
            e.size = (uint8_t) juce::jmin(3, meta.numBytes);
            for (int i = 0; i < e.size; ++i) e.bytes[i] = meta.data[i];
            heldNotes.apply(e, inChannel, true, noteOnEdge);
        }
        else
            outBuf.addEvent(m, meta.samplePosition);
    }

    // 2. The arp for this block, from the parameters.
    mu_toni::ArpParams ap;
    ap.scale        = (int) p(scale, 1.0f);
    ap.rootNote     = (int) p(root);
    ap.rootOctave   = (int) p(roct, 4.0f);
    ap.chord        = (int) p(chord, 3.0f);
    ap.inversion    = (int) p(inv);
    ap.octavesSpan  = (int) p(octs, 2.0f);
    ap.direction    = p(dir, 100.0f);
    ap.diatonicSnap = p(snap) > 0.5f;
    mu_toni::ArpAccent accent;
    accent.amount   = p(acc) * 0.01f;
    accent.length   = (int) p(accLen, 4.0f);
    accent.pattern  = (unsigned) juce::jmax(0, (int) p(accPat, 1.0f));
    accent.stepMask = (unsigned) juce::jmax(0, (int) p(stepMask, 65535.0f));
    runner.setArp(ap);
    runner.setAccent(accent);
    runner.setStep(juce::roundToInt(p(rate, 6.0f)), p(gate, 50.0f) * 0.01f, p(trig) > 0.5f);
    runner.setLegato(p(leg) > 0.5f);

    mu_toni::ArpContext ctx;
    ctx.playing          = playing;
    ctx.sampleRate       = sampleRate;
    ctx.bpm              = bpm > 0.0 ? bpm : 120.0;
    ctx.anyNoteHeld      = heldNotes.anyHeld();
    ctx.rootOverrideMidi = heldNotes.newest();
    ctx.noteOnEdge       = noteOnEdge;
    ctx.beatsPerSample   = ctx.bpm / 60.0 / sampleRate;

    // 3. Run the arp, in pieces no longer than the prepared block, and write its notes out at their samples.
    auto& voice = runner.getVoice();
    const int channel = juce::jlimit(1, 16, juce::roundToInt(p(outCh, 1.0f)));
    const int velocity = juce::roundToInt(p(vel, 100.0f));
    for (int done = 0; done < numSamples; done += maxBlock)
    {
        const int n = juce::jmin(maxBlock, numSamples - done);
        ctx.startBeat = startBeat + ctx.beatsPerSample * (double) done;
        voice.beginBlock(channel, velocity);
        runner.render(dummy, n, ctx);
        ctx.noteOnEdge = false;   // only the first piece sees the press
        for (int i = 0; i < voice.numEvents(); ++i)
        {
            const auto& ev = voice.event(i);
            outBuf.addEvent(juce::MidiMessage((int) (ev.status | (ev.channel - 1)), (int) ev.note, (int) ev.velocity), done + ev.sample);
        }
    }

    uiLastStep.store(runner.lastStep(), std::memory_order_relaxed);
    uiNote.store(voice.soundingNote(), std::memory_order_relaxed);
    uiPlaying.store(playing, std::memory_order_relaxed);
    // Copy rather than swap: outBuf keeps the capacity prepare() gave it, so this block's work never grows it.
    midi.clear();
    midi.addEvents(outBuf, 0, numSamples, 0);
}

} // namespace mu_toni_midi
