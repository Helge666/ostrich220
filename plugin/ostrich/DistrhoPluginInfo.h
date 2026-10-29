// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

// Ostrich 220 (O-220).  Its IDs are its own: a host takes it for another plugin than the
// "Ostrich" of the test builds before the first public version.
#define DISTRHO_PLUGIN_BRAND   "Ostrich 220"
#define DISTRHO_PLUGIN_NAME    "Ostrich 220"
#define DISTRHO_PLUGIN_URI     "urn:ostrich220:u220"
#define DISTRHO_PLUGIN_CLAP_ID "org.ostrich220.u220"

#define DISTRHO_PLUGIN_BRAND_ID  O220
#define DISTRHO_PLUGIN_UNIQUE_ID U220

#define DISTRHO_PLUGIN_HAS_UI           1
#define DISTRHO_PLUGIN_IS_SYNTH         1
#define DISTRHO_PLUGIN_IS_RT_SAFE       1
#define DISTRHO_PLUGIN_NUM_INPUTS       0
#define DISTRHO_PLUGIN_NUM_OUTPUTS      6     // MIX OUT, DIRECT OUT 1, DIRECT OUT 2
#define DISTRHO_PLUGIN_WANT_MIDI_INPUT  1
#define DISTRHO_PLUGIN_WANT_MIDI_OUTPUT 1     // the unit's MIDI OUT (SysEx: our DPF fork)
#define DISTRHO_PLUGIN_WANT_LATENCY     1

// The panel goes to the UI as one blob through updateStateValue() (our DPF fork
// gives JACK, CLAP and VST3 that path), the NVRAM into the host's session.
#define DISTRHO_PLUGIN_WANT_STATE       1
#define DISTRHO_PLUGIN_WANT_FULL_STATE  1

#define DISTRHO_UI_USE_NANOVG           1
// The unit's front (OstrichUI.cpp, 1350 x 250 design units, the card slots at the right)
// and the bar under it, both sides divisible by 4: at every Windows scaling in quarter
// steps the size is whole pixels (125 %: 1755 x 360), and a host that converts to its own
// units and back -- Cubase -- has nothing to round.  1150 x 250 once came out as
// 1437.5 x 312.5 at 125 %, and Cubase shrank the view a pixel per layout pass.
#define DISTRHO_UI_DEFAULT_WIDTH        1404
#define DISTRHO_UI_DEFAULT_HEIGHT       288
#define DISTRHO_UI_USER_RESIZABLE       1
#define DISTRHO_UI_FILE_BROWSER         1     // the PCM card slots

#define DISTRHO_PLUGIN_LV2_CATEGORY "lv2:InstrumentPlugin"
#define DISTRHO_PLUGIN_VST3_CATEGORIES "Instrument|Synth"
#define DISTRHO_PLUGIN_CLAP_FEATURES "instrument", "synthesizer", "stereo"

#endif // DISTRHO_PLUGIN_INFO_H_INCLUDED
