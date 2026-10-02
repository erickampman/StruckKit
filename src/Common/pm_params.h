// pm_params.h — portable replacement for PolygraphModular's ParamGroupData.h.
//
// The original is Objective-C (NSString*/NSArray in the param-constants struct, NS_ENUM
// throughout), so rather than vendor it we provide the same type NAMES the framework expects,
// with plain C++ types (const char*, float, uint32). The per-module parameter-ITEM enums are
// added here as each module is ported. The actual per-module constant DATA (names/ranges) is
// vendored/converted separately, in pm_param_data.cpp.

#ifndef PM_PARAMS_H
#define PM_PARAMS_H

#include <cstddef>
#include <cstdint>

#include "pm_compat.h"

// First param enum value is 1 (0 is "unspecified"); enums double as array indices.
#define PARAM_ENUM_TO_INDEX(e) ((e) - 1)

// Module kinds — the high byte of a param address. Values must stay stable (they are the
// module identity in every param clap_id). Mirrors PolygraphModular's ModuleParamKind.
enum ModuleParamKind : NSUInteger {
   ModuleParamKindUnspecified = 0,
   ModuleParamKindGlobal      = 1,
   ModuleParamKindPitchConverter,
   ModuleParamKindVelocityConverter,
   ModuleParamKindSlew,
   ModuleParamKindSinOsc,
   ModuleParamKindTriSawOsc,
   ModuleParamKindNoise,
   ModuleParamKindADR,
   ModuleParamKindADSR,
   ModuleParamKindLFO, /* 0x0A */
   ModuleParamKindButterworthLowPassFilter,
   ModuleParamKindChebyshevLowPassFilter,
   ModuleParamKindButterworthBandpassFilter,
   ModuleParamKindMultimodeFilter,
   ModuleParamKindBiquadFilter,
   ModuleParamKindFormantFilter, /* 0x10 */
   ModuleParamKindVariableSlopeFilter,
   ModuleParamKindRezNotchFilter,
   ModuleParamKindSineWarp,
   ModuleParamKindPolynomial,
   ModuleParamKindScaleOffset,
   ModuleParamKindLevel,
   ModuleParamKindComparator,
   ModuleParamKindThresholdImpulse,
   ModuleParamKindDeltaImpulse,
   ModuleParamKindOneShot,
   ModuleParamKindSampleAndHold,
   ModuleParamKindSequencer,
   ModuleParamKindDiscretizer,
   ModuleParamKindEDODiscretizer,
   ModuleParamKindCombine,
   ModuleParamKindQuadCombine, /* 0x20 */
   ModuleParamKindQuadStereoMixer,
   ModuleParamKindXFade,
   ModuleParamKindPitchBendConverter,
   ModuleParamKindControlChangeConverter,
   ModuleParamKindChannelPressure,
   ModuleParamKindNoteGate,
   ModuleParamKindLogicGate,
   ModuleParamKindClockDivider,
   ModuleParamKindSampleDelay,
   ModuleParamKindFinalOutput,
   ModuleParamKindTunnelInput,
   ModuleParamKindTunnelOutput,
   ModuleParamKindChorus,
   ModuleParamKindFlanger,
   ModuleParamKindPhaser,
   ModuleParamKindDelay, /* 0x30 */
   ModuleParamKindGlobalDelay,
   ModuleParamKindGlobalReverb,
   ModuleParamKindGlobalEnhance,
   ModuleParamKindMIDI,
   ModuleParamKindNode,
   ModuleParamKindValue,
   ModuleParamKindOverdrive,
   ModuleParamKindSplitLevel,
   ModuleParamKindMIDINoteNumberConverter,
   ModuleParamKindUnsplit,
   ModuleParamKindPan,
   ModuleParamKindIFSOsc,
   ModuleParamKindIFSEnv,
   ModuleParamKindIFSLFO,
   ModuleParamKindIFSConv,
   ModuleParamKindIFSRes,
   ModuleParamKindIFSGran,
   ModuleParamKindMandOsc,
   ModuleParamKindJuliaGran,
   ModuleParamKindClockGran,
   ModuleParamKindCVMixer, // IFSynth: combine mod sources (add/multiply, per-input enable)
   ModuleParamKindStrike,    // StruckSynth: note/gate → shaped excitation burst
   ModuleParamKindModalBank, // StruckSynth: bank of damped modal resonators
   ModuleParamKindDrumKit,   // StruckKit: note->drum kit voice with cross-voice hi-hat choke (PMU port)
};

// Module instance — the middle byte of a param address (1..24).
enum ModuleInstance : NSUInteger {
   ModuleInstance1 = 1,
   ModuleInstance2, ModuleInstance3, ModuleInstance4, ModuleInstance5, ModuleInstance6,
   ModuleInstance7, ModuleInstance8, ModuleInstance9, ModuleInstance10, ModuleInstance11,
   ModuleInstance12, ModuleInstance13, ModuleInstance14, ModuleInstance15, ModuleInstance16,
   ModuleInstance17, ModuleInstance18, ModuleInstance19, ModuleInstance20, ModuleInstance21,
   ModuleInstance22, ModuleInstance23, ModuleInstance24,
};

enum ModuleItem : NSUInteger {
   ModuleItemUnspecified = 0,
};

// --- per-module parameter-item enums (added as each module is ported) --------------------

enum OscSyncType : NSUInteger {
   OscSyncTypeDown = 0,
   OscSyncTypeUp,
   OscSyncTypeBoth,
};

enum SinOscParamItem : NSUInteger {
   SinOscParamItemPitch = 1,
   SinOscParamItemPitchFine,
   SinOscParamItemPitchInput,
   SinOscParamItemModPitch,
   SinOscParamItemModPitch2,
   SinOscParamItemModPhase,
   SinOscParamItemFreqModLevel,
   SinOscParamItemPhaseModLevel,
   SinOscParamItemFreqModLevel2,
   SinOscParamItemModSync,
   SinOscParamItemModSyncType,
   SinOscParamItemDetune,   // stereo detune, cents (splits L/R by ±half); 0 = mono
   SinOscParamItemWaveform, // 0 = Sine, 1 = Triangle, 2 = Saw (band-limited via PolyBLEP/BLAMP)
};

// Waveform selector values for SinOscParamItemWaveform.
enum OscWaveform : int {
   OscWaveformSine     = 0,
   OscWaveformTriangle = 1,
   OscWaveformSaw      = 2,
};

// Level — a simple gain stage: output = input × level. Used to scale the sine osc's audible mix
// level *after* its raw output has been tapped for the IFS hard-sync input.
enum LevelParamItem : NSUInteger {
   LevelParamItemInput = 1, // connection
   LevelParamItemLevel,     // 0..1
};

// IFS fractal oscillator — trimmed v1 set (PM's modulation matrix is deferred). PitchInput is a
// connection; the five fractal-shape items + smoothing + iterations drive the wavetable.
enum IFSOscParamItem : NSUInteger {
   IFSOscParamItemPitchInput = 1, // connection: pitch from the voice chain
   IFSOscParamItemPitch,          // coarse tune (semitones)
   IFSOscParamItemPitchFine,      // fine tune (cents)
   IFSOscParamItemIFS1Squash,     // left-region self-similar vertical scale
   IFSOscParamItemIFS2Squash,     // right-region self-similar vertical scale
   IFSOscParamItemBx,             // split point (x)
   IFSOscParamItemBy,             // junction height (y)
   IFSOscParamItemStartEndY,      // shared endpoint level
   IFSOscParamItemSmoothing,      // 0 = Catmull-Rom, 1 = piecewise-linear
   IFSOscParamItemIterations,     // picker index: 0 → 3 iters, 1 → 4 iters
   // Modulation inputs (connection ports) + their bipolar depth levels. Pitch mod is applied
   // per-sample (wavetable-compatible); the shape mods switch IFSOsc to the chunked eval path.
   IFSOscParamItemPitchModInput,
   IFSOscParamItemPitchModLevel,
   IFSOscParamItemBxModInput,
   IFSOscParamItemBxModLevel,
   IFSOscParamItemByModInput,
   IFSOscParamItemByModLevel,
   IFSOscParamItemSquash1ModInput,
   IFSOscParamItemSquash1ModLevel,
   IFSOscParamItemSquash2ModInput,
   IFSOscParamItemSquash2ModLevel,
   IFSOscParamItemDetune,         // stereo detune, cents (splits L/R by ±half); 0 = mono
   IFSOscParamItemSyncInput,      // connection: hard-sync source (stereo, from the sine osc)
   IFSOscParamItemSyncMode,       // 0 Off, 1 Up, 2 Down, 3 Both (zero-crossing of the sync input)
   IFSOscParamItemLevel,          // output mix level
};

// IFS fractal envelope — 3-transform shape (blue + green interior points), phase over Duration.
enum IFSEnvParamItem : NSUInteger {
   IFSEnvParamItemGateInput = 1, // connection (unused in v1 voice; MIDI drives the gate)
   IFSEnvParamItemDuration,      // full-traversal time, seconds
   IFSEnvParamItemStartEndY,     // level at both ends
   IFSEnvParamItemBlueX,         // first interior point x [0,1]
   IFSEnvParamItemBlueY,         // first interior point y [-1,1]
   IFSEnvParamItemGreenX,        // second interior point x [0,1]
   IFSEnvParamItemGreenY,        // second interior point y [-1,1]
   IFSEnvParamItemIFS1Squash,
   IFSEnvParamItemIFS2Squash,
   IFSEnvParamItemIFS3Squash,
   IFSEnvParamItemSmoothing,
   IFSEnvParamItemIterations,    // picker index: 0 → 1 iteration
   IFSEnvParamItemHoldMode,      // 0 None, 1 Blue, 2 Green, 3 Loop
   IFSEnvParamItemLegatoMode,    // 0 NoteSteal, 1 FindMatchingY
};

// IFS fractal LFO — free-running mod source, 2-transform shape (same family as IFSOsc). Trimmed
// v1 set (no mod matrix / FM / reset / L-R phase).
enum IFSLFOParamItem : NSUInteger {
   IFSLFOParamItemPitch = 1,    // rate, octaves, -7..7, 0 = 1 Hz
   IFSLFOParamItemBx,
   IFSLFOParamItemBy,
   IFSLFOParamItemIFS1Squash,
   IFSLFOParamItemIFS2Squash,
   IFSLFOParamItemStartEndY,
   IFSLFOParamItemSmoothing,
   IFSLFOParamItemIterations,   // picker index: 0 → 1 iteration
};

// CV mixer — combines an LFO input, an Env input, and a MIDI CC into one output. Each input can
// be enabled/disabled (a disabled input is SKIPPED, not treated as 0); Mode adds or multiplies
// the enabled inputs. No per-input level (depth lives on the destination mod-input side).
enum CVMixerParamItem : NSUInteger {
   CVMixerParamItemLFOInput = 1, // connection port (from an IFSLFO)
   CVMixerParamItemEnvInput,     // connection port (from an IFSEnv)
   CVMixerParamItemEnableLFO,    // 0/1
   CVMixerParamItemEnableEnv,    // 0/1
   CVMixerParamItemEnableCC,     // 0/1
   CVMixerParamItemCCNumber,     // 1..127 (default 1 = mod wheel)
   CVMixerParamItemMode,         // 0 = add, 1 = multiply
};

// 24 dB/oct Butterworth low-pass. Item 1 is the audio input; Pitch is the (absolute) cutoff in
// octaves through the shared PitchConverter (0 = base A440); Q adds resonance. The Freq/Q mod
// inputs + their levels exist for a future LPF-mod panel (dormant until connected).
enum LPFParamItem : NSUInteger {
   LPFParamItemInput = 1,       // audio input
   LPFParamItemPitch,           // cutoff, octaves (0 = 440 Hz)
   LPFParamItemFreqModLevel,    // first freq mod depth
   LPFParamItemQ,               // resonance, 1..20
   LPFParamItemModFreqInput,    // first freq modulation input (connection)
   LPFParamItemModFreqInput2,   // second freq modulation input (connection)
   LPFParamItemFreqModLevel2,   // second freq mod depth
   LPFParamItemModQInput,       // q modulation input (connection)
   LPFParamItemModQInputLevel,  // q mod depth
};

enum ADSRParamItem : NSUInteger {
   ADSRParamItemAttack = 1,
   ADSRParamItemDecay,
   ADSRParamItemSustain,
   ADSRParamItemRelease,
   ADSRParamItemGateInput,
   ADSRParamItemLegato, // 0=Steal, 1=LegatoRetrigger, 2=LegatoTrue
   ADSRParamItemLinear, // 0=exponential, 1=linear
};

enum ParamItemCombineType : NSInteger {
   ParamItemCombineType_Add = 0,
   ParamItemCombineType_Multiply,
};

enum CombineParamItem : NSUInteger {
   CombineParamItemInput1 = 1,
   CombineParamItemInput2,
   CombineParamItemInput3,
   CombineParamItemType,
   CombineParamItemLevel,
};

enum FinalOutputParamItem : NSUInteger {
   FinalOutputParamItemInput = 1,
   FinalOutputParamItemSpread,
   FinalOutputParamItemLevel,
   FinalOutputParamItemClipLevel,
   FinalOutputParamItemFilterDC,
};

enum SlewParamItem : NSUInteger {
   SlewParamItemInput = 1,
   SlewParamItemRise,
   SlewParamItemFall,
   SlewParamItemCurve,
};

enum PitchConverterParamItem : NSUInteger {
   PitchConverterParamItemDummy = 1,
};

// Referenced by Voice::connectTunnelPairs (no tunnel modules in the v1 patch, but it must compile).
enum TunnelOutParamItem : NSUInteger {
   TunnelOutParamItemInput = 1,
};

// VoiceManager's own params (polyphony / filtering / mode).
enum MIDIParamItem : NSUInteger {
   MIDIParamItemVoiceCount = 1,
   MIDIParamItemGroupFilterValue,   // 0 = disabled, 1..16 = group
   MIDIParamItemChannelFilterValue, // 0 = disabled, 1..16 = channel
   MIDIParamItemVoiceStealAlgorithm,
   MIDIParamItemFreeRun,
   MIDIParamItemLegato, // 0=Steal, 1=Legato
};

enum VoiceStealAlgorithm : NSUInteger {
   VoiceStealAlgorithmOldest = 0,
   VoiceStealAlgorithmLowest,
   VoiceStealAlgorithmHighest,
   VoiceStealAlgorithmClosest,
   VoiceStealAlgorithmFurthest,
};

// --- StruckSynth: Strike (exciter) -------------------------------------------------------
// Turns a note/gate event into a shaped excitation burst for a resonator. MIDI note-on drives
// it directly (velocity → strength); the optional Gate input takes over for non-MIDI triggering.
enum StrikeParamItem : NSUInteger {
   StrikeParamItemGateInput = 1,     // connection: optional external trigger (rising edge)
   StrikeParamItemHardnessInput,     // connection: optional hardness modulation
   StrikeParamItemType,              // 0 = Mallet (bump), 1 = Noise burst
   StrikeParamItemHardness,          // 0 = 1-sample impulse (hard/bright) .. 1 = soft/dark
   StrikeParamItemHardnessModLevel,  // scales HardnessInput
   StrikeParamItemVelToLevel,        // 0 = fixed amplitude .. 1 = fully velocity-scaled
   StrikeParamItemVelToHardness,     // harder hit → shorter/brighter contact
   StrikeParamItemLevel,             // output gain
   StrikeParamItemContactMode,       // how contact time is measured (see StrikeContactMode)
};

enum StrikeType : NSUInteger {
   StrikeTypeMallet = 0,
   StrikeTypeNoise,
};

enum StrikeContactMode : NSUInteger {
   StrikeContactNoteRelative = 0,  // contact spans a fixed number of periods of the note
   StrikeContactWallClock,         // fixed contact time in ms (physical mallet)
};

// --- StruckSynth: ModalBank (resonator) --------------------------------------------------
// A bank of independently damped two-pole resonators, one per vibrational mode, excited by an
// input signal. Items 1-5 are connection ports; the rest are object/decay/strike/output
// controls. Core (framework-free): ModalBank.hpp/.cpp, ModalMaterials/Generators.hpp.
enum ModalBankParamItem : NSUInteger {
   ModalBankParamItemInput = 1,      // connection: excitation audio in (L/R)
   ModalBankParamItemPitchInput,     // connection: from Pitch Converter; octaves re A440
   ModalBankParamItemStrikeInput,    // connection: patch velocity here
   ModalBankParamItemDecayModInput,  // connection
   ModalBankParamItemBrightModInput, // connection
   // Object
   ModalBankParamItemPitch,          // transpose offset (octaves), ADDED to input
   ModalBankParamItemPitchModLevel,  // scales PitchInput (1 = unity tracking, 0 = fixed pitch)
   ModalBankParamItemMaterial,       // indexed; see ModalBankMaterial
   ModalBankParamItemModeCount,      // 1..64; clamps to the table's depth
   ModalBankParamItemStretch,        // INDEX into kModalStretchValues (inharmonicity)
   // Decay
   ModalBankParamItemDecayT60,       // seconds, T60 of mode 0
   ModalBankParamItemDecayModLevel,
   ModalBankParamItemDampTilt,       // T60_i *= ratio^(-tilt); 0 metal, ~2 wood
   ModalBankParamItemDampBellAmount, // octaves of T60 at the centre; 0 = off
   ModalBankParamItemDampBellCentre, // octaves above the fundamental
   ModalBankParamItemDampBellWidth,  // Gaussian sigma, in octaves
   // Strike
   ModalBankParamItemStrikePosition, // 0..1; 0.5 cancels even modes
   ModalBankParamItemStrikeModLevel,
   ModalBankParamItemBrightness,     // amplitude tilt across modes
   ModalBankParamItemBrightModLevel,
   // Output
   ModalBankParamItemSpread,         // stereo distribution of modes
   ModalBankParamItemMix,            // 1 = source, <1 = resonator in a path
   ModalBankParamItemLevel,
};

// Material selector for ModalBankParamItemMaterial. Order MUST match kModalMaterials
// (ModalMaterials.hpp) for the static tables, then the procedural generators whose order MUST
// match ModalSeriesID (ModalGenerators.hpp): materialIdx - MODAL_BANK_FIRST_GENERATOR = series.
enum ModalBankMaterial : NSUInteger {
   ModalBankMaterialString = 0,
   ModalBankMaterialBar,
   ModalBankMaterialTunedBar,
   ModalBankMaterialMembrane,
   ModalBankMaterialOpenTube,
   ModalBankMaterialClosedTube,
   ModalBankMaterialTine,
   ModalBankMaterialBell,
   ModalBankMaterialGuitarBody,
   ModalBankMaterialViolinBody,
   ModalBankMaterialSoundboard,
   ModalBankMaterialGenericBody,
   ModalBankMaterialGen0,          // first generator (Harmonic n)
   ModalBankMaterialGenOctave,
   ModalBankMaterialGenFibonacci,
   ModalBankMaterialGenPrime,
   ModalBankMaterialGenTriangular,
   ModalBankMaterialGenTetrahedral,
   ModalBankMaterialGenPartition,
   ModalBankMaterialGenGolden,
   ModalBankMaterialGenSilver,
   ModalBankMaterialGenPiano,
   ModalBankMaterialGenPower,
   ModalBankMaterialGenSqrt,
   ModalBankMaterialGenBox3D,
   ModalBankMaterialGenHarmonicNumber,
   ModalBankMaterialCount,
};

// First generator index; everything at or above this is procedural (see ModalGenerators.hpp).
#define MODAL_BANK_FIRST_GENERATOR ModalBankMaterialGen0

// --- StruckKit: DrumKit (PMU port) ------------------------------------------------------
// One MIDI-driven module that renders a whole kit; the played note's pitch class picks the drum.
// Only the "finalized" per-drum user controls are exposed. See DrumKitModule.cpp.
enum DrumKitParamItem : NSUInteger {
   DrumKitParamItemLevel = 1,     // master output gain
   DrumKitParamItemKeyMap,        // 0 = StruckKit (repeats every two octaves), 1 = General MIDI

   // --- Crash ---
   DrumKitParamItemCrashSize,     // C# crash diameter (13" .. 22")
   DrumKitParamItemCrashSize2,    // D# crash diameter (13" .. 22")
   DrumKitParamItemCrashThin,     // thickness: heavy <-> thin (thin blooms into a wash sooner)
   DrumKitParamItemCrashDecay,    // bronze loss factor: short <-> long ring
   DrumKitParamItemCrashStroke,   // shoulder on the edge (crash) <-> tip on the bow (ride ping)

   // --- Snare ---
   DrumKitParamItemSnareTune,     // batter-head tension (pitch)
   DrumKitParamItemSnareRing,     // head damping: muffled <-> open ring
   DrumKitParamItemSnarePosition, // strike radius: centre <-> edge (E only; F is a rimshot)
   DrumKitParamItemSnareTension,  // snare-wire tension: loose long buzz <-> tight crisp burst
   DrumKitParamItemSnareWires,    // wire level

   // --- Toms ---
   DrumKitParamItemTomTune,       // batter-head tension: all three toms move together
   DrumKitParamItemTomRing,       // head damping: muffled <-> open ring
   DrumKitParamItemTomPosition,   // strike radius: centre <-> edge
   DrumKitParamItemTomReso,       // resonant-head tuning relative to the batter: low <-> high

   // --- Hi-hats ---
   DrumKitParamItemHatSize,       // diameter, 12" .. 15"
   DrumKitParamItemHatThin,       // thickness: heavy <-> thin
   DrumKitParamItemHatSizzle,     // half-open: how close the cymbals sit, i.e. how much they chatter

   // --- Hand drums (congas, bongos, timbales) ---
   DrumKitParamItemCongaTune,     // head tension: the three congas move together
   DrumKitParamItemBongoTune,     // head tension: both bongos
   DrumKitParamItemTimbaleTune,   // head tension: both timbales
   DrumKitParamItemHandRing,      // head damping: muffled <-> open ring

   // --- Percussion (cowbell, agogo, claves, wood blocks, triangle) ---
   DrumKitParamItemPercTune,      // size: all of them move together, +-5 semitones
   DrumKitParamItemPercDecay,     // ring: short <-> long
};

// Per-parameter constants (portable): the source for CLAP clap_param_info generation.
struct ModuleParamConstants {
   const char*               name;
   uint8_t                   item;
   AudioUnitParameterUnit    unit;
   AudioUnitParameterOptions flags;
   const char* const*        valueStrings; // null or null-terminated list (stepped params)
   AudioUnitParameterValue   minValue;
   AudioUnitParameterValue   maxValue;
   AudioUnitParameterValue   defValue;
   uint64_t                  paramInfo;
};

struct ModuleParamConstantsGroup {
   char*                       name;
   size_t                      count;
   const ModuleParamConstants* items;
};

#endif // PM_PARAMS_H
