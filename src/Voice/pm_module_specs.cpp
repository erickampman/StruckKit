// pm_module_specs.cpp — the trimmed v1 module registry. getModuleInitSpecs() lists exactly the
// six modules the fixed v1 voice instantiates (PitchConverter, Slew, SinOsc, ADSR envelope,
// Combine VCA, Final). Each entry: {address, param group, init/deinit/process/midi/param/reset/
// sampleRate procs}. Envelope is ADSR (the v1 choice).

#include "ModuleInitSpecs.hpp"

#include "ADSRModule.hpp"
#include "CVMixerModule.hpp"
#include "CombineModule.hpp"
#include "FinalModule.hpp"
#include "IFSEnvModule.hpp"
#include "IFSLFOModule.hpp"
#include "IFSOscModule.hpp"
#include "LPFilterModule.hpp"
#include "LevelModule.hpp"
#include "ModalBankModule.hpp"
#include "PitchConverterModule.hpp"
#include "SinOscillatorModule.hpp"
#include "SlewModule.hpp"
#include "StrikeModule.hpp"
#include "DrumKitModule.hpp"
#include "ChokeGroupState.hpp"
#include "pm_addresses.h"
#include "pm_param_data.h"

const std::vector<ModuleInitSpec>& getModuleInitSpecs() {
   static const std::vector<ModuleInitSpec> specs = {
      {pitchConverter1Address, &pitchConverterModuleConstantsGroup, InitPitchConverter,
       DeinitPitchConverter, ProcessPitchConverter, MIDIPitchConverter, nullptr, nullptr, nullptr},
      {slew1Address, &slewModuleConstantsGroup, InitSlewModule, DeinitSlewModule, ProcessSlewModule,
       nullptr, nullptr, nullptr, nullptr},
      {sinOsc1Address, &sinOscConstantsGroup, InitSinOscillator, DeinitSinOscillator,
       ProcessSinOscillator, MIDISinOscillator, nullptr, ResetSinOscillator, SampleRateSinOscillator},
      // IFSOsc is the active source (see buildDefaultPatch); SinOsc stays instantiated but
      // unconnected — available as a second source / future phase-mod source.
      {ifsOsc1Address, &ifsOscConstantsGroup, InitIFSOscModule, DeinitIFSOscModule,
       ProcessIFSOscModule, MIDIIFSOscModule, ParamIFSOscModule, ResetIFSOscModule,
       SampleRateIFSOscModule},
      {adsr1Address, &adsrConstantsGroup, InitADSRModule, DeinitADSRModule, ProcessADSRModule,
       MIDIProcADSRModule, ParamADSR, nullptr, SampleRateADSRModule},
      // IFSEnv is the active amplitude envelope (see buildDefaultPatch); ADSR stays instantiated
      // but unconnected.
      {ifsEnv1Address, &ifsEnvConstantsGroup, InitIFSEnvModule, DeinitIFSEnvModule,
       ProcessIFSEnvModule, MIDIIFSEnvModule, ParamIFSEnvModule, ResetIFSEnvModule,
       SampleRateIFSEnvModule},
      // IFSLFO is instantiated (params exposed, UI visible) but NOT connected in the graph yet
      // — it's a mod source awaiting the CV/mod system. Free-running, no MIDI/reset/sr procs.
      {ifsLFO1Address, &ifsLFOConstantsGroup, InitIFSLFOModule, DeinitIFSLFOModule,
       ProcessIFSLFOModule, nullptr, ParamIFSLFOModule, nullptr, nullptr},
      // Second env + LFO instances (mod sources, also unconnected). Same procs, instance-2
      // addresses distinguish them; each has its own state/params.
      {ifsEnv2Address, &ifsEnvConstantsGroup, InitIFSEnvModule, DeinitIFSEnvModule,
       ProcessIFSEnvModule, MIDIIFSEnvModule, ParamIFSEnvModule, ResetIFSEnvModule,
       SampleRateIFSEnvModule},
      {ifsLFO2Address, &ifsLFOConstantsGroup, InitIFSLFOModule, DeinitIFSLFOModule,
       ProcessIFSLFOModule, nullptr, ParamIFSLFOModule, nullptr, nullptr},
      // Two CV mixers (combine LFO+Env+CC). Inputs are wired in buildDefaultPatch; their outputs
      // await the destination mod-inputs, so they're dormant for now.
      {cvMixer1Address, &cvMixerConstantsGroup, InitCVMixerModule, DeinitCVMixerModule,
       ProcessCVMixerModule, MIDICVMixerModule, nullptr, nullptr, nullptr},
      {cvMixer2Address, &cvMixerConstantsGroup, InitCVMixerModule, DeinitCVMixerModule,
       ProcessCVMixerModule, MIDICVMixerModule, nullptr, nullptr, nullptr},
      // 24 dB/oct LPF, inserted IFSOsc → LPF → Combine(in1) in buildDefaultPatch. No MIDI/param
      // procs (all its params are plain scalars read each process); sample-rate proc resets state.
      {lpf1Address, &lpfConstantsGroup, InitLPFilterModule, DeinitLPFilterModule,
       ProcessLPFilterModule, nullptr, nullptr, nullptr, SampleRateLPFilterModule},
      {combine1Address, &combineConstantsGroup, nullptr, nullptr, ProcessCombineModule, nullptr,
       nullptr, nullptr, nullptr},
      // Combine 2 — oscillator mixer (Add mode, set in buildDefaultPatch): SinOsc + IFSOsc.
      {combine2Address, &combineConstantsGroup, nullptr, nullptr, ProcessCombineModule, nullptr,
       nullptr, nullptr, nullptr},
      // Level — scales the sine osc's audible path (raw sine still feeds IFS sync).
      {level1Address, &levelConstantsGroup, nullptr, nullptr, ProcessLevelModule, nullptr,
       nullptr, nullptr, nullptr},
      {finalAddress, &finalConstantsGroup, InitFinalModule, DeinitFinalModule, ProcessFinalModule,
       MIDIFinalModule, nullptr, nullptr, SampleRateFinalModule},

      // --- StruckKit modules (present but unconnected until buildDefaultPatch wires them) ---
      // Strike: the exciter. Uses the MIDI slot (note-on → burst); Reset clears any active burst.
      {strike1Address, &strikeConstantsGroup, InitStrikeModule, DeinitStrikeModule,
       ProcessStrikeModule, MIDIStrikeModule, nullptr, ResetStrikeModule, SampleRateStrikeModule},
      // ModalBank: the resonator. No MIDI/param procs; Reset flushes filter state (voice alloc,
      // NOT note-on — re-striking a ringing bank must not reset the ring-down).
      {modalBank1Address, &modalBankConstantsGroup, InitModalBankModule, DeinitModalBankModule,
       ProcessModalBankModule, nullptr, nullptr, ResetModalBankModule, SampleRateModalBankModule},

      // --- StruckKit ---
      // DrumKit: the whole kit in one module (note pitch class -> drum). One ChokeGroupState is
      // shared by every voice's copy so a newer hi-hat can fast-release an older one.
      {drumKit1Address, &drumKitConstantsGroup, InitDrumKitModule, DeinitDrumKitModule,
       ProcessDrumKitModule, MIDIDrumKitModule, nullptr, ResetDrumKitModule, nullptr,
       CreateChokeGroupState},
   };
   return specs;
}
