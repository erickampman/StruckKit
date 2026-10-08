// pm_compat.h — portability shim for vendoring the PolygraphModular DSP/voice framework
// off Apple platforms. The PM framework is only shallowly coupled to Apple: a few scalar
// typedefs, UMP MIDI, and os_log. This header collapses that surface so the framework
// compiles under a plain C++20 toolchain (Linux/clang/gcc) with no AudioToolbox/CoreMIDI.
//
// The scalar typedefs + logging no-ops below are final. UMP MIDI is not here: the PM modules
// read pm::UmpMessage (pm_midi.h), decoded through uw-core's uw::ump.

#ifndef PM_COMPAT_H
#define PM_COMPAT_H

#include <cstdint>
#include <cstddef>

// These are plain C++ translation units on every platform (including macOS — the PM
// framework is vendored for a CLAP-only build, not an AUv3 host), so the real Apple SDK
// typedefs are never reliably visible here even when AudioToolbox/CoreMIDI are present:
// NSUInteger/NSInteger/AUParameterAddress etc. live behind Objective-C-only paths in Apple's
// headers. Define the same portable aliases unconditionally instead of branching on
// __APPLE__.

// --- scalar typedefs (Apple aliases → plain C++ types) ---------------------------------
typedef float    AUValue;
typedef float    AudioUnitParameterValue;
typedef uint32_t AudioUnitParameterUnit;
typedef uint32_t AudioUnitParameterOptions;
typedef uint64_t AUParameterAddress;
typedef uint32_t AUAudioFrameCount;
typedef int64_t  AUEventSampleTime;
typedef unsigned long NSUInteger;
typedef long          NSInteger;

// --- logging (os_log → no-op; framework LOG_* macros funnel through these) ---------------
#define os_log_with_type(...)   ((void)0)
#define OS_LOG_TYPE_INFO        0

#endif // PM_COMPAT_H
