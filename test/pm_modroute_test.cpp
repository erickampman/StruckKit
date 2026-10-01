// pm_modroute_test — the modulation routing end to end at the voice level. Wires an IFS
// patch, confirms IFSOsc's Bx mod input starts unconnected (wavetable path), connects an IFS LFO
// into it (what VoiceManager::setModSource does), confirms it now reports connected (eval path),
// and that the voice still renders audio through the modulated-eval path.

#include "ModuleFactory.hpp"
#include "PitchConverter.hpp"
#include "Voice.hpp"
#include "pm_addresses.h"
#include "pm_midi.h"
#include "pm_params.h"

#include <cmath>
#include <cstdio>
#include <cstring>

static int g_fails = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", (msg)); ++g_fails; } } while (0)

static void wirePatch(Voice& v)
{
    Module* pc   = v.getModule(pitchConverter1Address);
    Module* slew = v.getModule(slew1Address);
    Module* osc  = v.getModule(ifsOsc1Address);
    Module* env  = v.getModule(ifsEnv1Address);
    Module* comb = v.getModule(combine1Address);
    Module* fin  = v.getModule(finalAddress);
    v.connectModules(comb, fin, finalInputAddress);
    v.connectModules(osc, comb, combine1Input1Address);
    v.connectModules(env, comb, combine1Input2Address);
    v.connectModules(slew, osc, ifsOsc1PitchInputAddress);
    v.connectModules(pc, slew, slew1InputAddress);
}

static float peakOf(const float* b, uint32_t n)
{
    float p = 0.0f;
    for (uint32_t i = 0; i < n; ++i) p = std::fmax(p, std::fabs(b[i]));
    return p;
}

int main()
{
    const double sr = 48000.0;
    const uint32_t N = 512;
    ModuleFactory factory(sr, N);
    PitchConverter pc(440.0);
    Voice v(sr);
    v.initializeModules(&factory, &pc, nullptr);
    wirePatch(v);

    Module* osc  = v.getModule(ifsOsc1Address);
    Module* lfo1 = v.getModule(ifsLFO1Address);
    CHECK(osc && lfo1, "IFSOsc + IFSLFO present");

    CHECK(!osc->isInputConnected(IFSOscParamItemBxModInput), "Bx mod initially unconnected (wavetable path)");
    v.connectModules(lfo1, osc, ifsOsc1BxModInputAddress); // == what setModSource() does
    CHECK(osc->isInputConnected(IFSOscParamItemBxModInput), "Bx mod connected after routing (eval path)");

    v.handleMIDIMessage(pm::makeNoteOn(69, 0x8000)); // A4
    float L[512], R[512];
    float peak = 0.0f;
    for (int b = 0; b < 20; ++b) {
        std::memset(L, 0, sizeof(L));
        std::memset(R, 0, sizeof(R));
        v.process(N, L, R);
        peak = std::fmax(peak, peakOf(L, N));
    }
    CHECK(peak > 0.05f, "renders audio with a shape mod connected (eval path active)");
    std::printf("modroute: peak=%.3f\n", peak);

    if (g_fails == 0) std::printf("ALL PASS\n");
    return g_fails ? 1 : 0;
}
