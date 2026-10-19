// plugin_gui.h — the seam between the DSP side (src/plugin.cpp, portable C++ that owns the raw
// CLAP plugin) and the GUI shell (src/UI/StruckKitUI.cpp, DPF + NanoVG).
//
// Neither side includes the other's internals; they meet only at these C-linkage functions. This is
// the same discipline StruckSynth and PlainOldSynth use.
//
// Provided by plugin.cpp, called by the GUI shell:
//   sk_plugin_host / sk_plugin_get_gui   — per-instance plumbing
//   sk_param_*                           — read a value, and edit it with proper gestures
// Provided by the GUI shell, called by plugin.cpp:
//   sk_gui_extension                     — the CLAP gui vtable for this platform
//   sk_gui_push_param                    — host automation → the open editor
//   sk_gui_on_timer                      — CLAP timer-support → pumps the DPF/pugl event loop

#pragma once

#include <clap/clap.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// --- provided by plugin.cpp, called by the GUI shell --------------------------------------------
const clap_host_t* sk_plugin_host(const clap_plugin_t* plugin);

// The GUI shell owns its per-instance state object; it registers it here so plugin.cpp can reach an
// open editor (host automation → the editor) without either side knowing the other's types.
void  sk_plugin_set_gui(const clap_plugin_t* plugin, void* gui);
void* sk_plugin_get_gui(const clap_plugin_t* plugin);

uint32_t sk_param_count(void);

double sk_param_value(const clap_plugin_t* plugin, uint32_t index);

// A knob drag is begin_gesture → set_from_gui (repeatedly) → end_gesture. plugin.cpp turns these
// into CLAP param-gesture/param-value events for the host, so the move is recorded as automation
// rather than silently changing state.
void sk_param_begin_gesture(const clap_plugin_t* plugin, uint32_t index);
void sk_param_set_from_gui (const clap_plugin_t* plugin, uint32_t index, double value);
void sk_param_end_gesture  (const clap_plugin_t* plugin, uint32_t index);

// --- provided by the GUI shell, called by plugin.cpp --------------------------------------------
const clap_plugin_gui_t* sk_gui_extension(void);
void sk_gui_push_param(void* gui, uint32_t index, double value);
void sk_gui_on_timer(const clap_plugin_t* plugin, clap_id timer_id);

#ifdef __cplusplus
}
#endif
