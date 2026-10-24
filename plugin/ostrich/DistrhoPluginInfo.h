// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

// "Ostrich" for now; the project may become "Ostrich 220" later (ROADMAP.md, Phase 5).
#define DISTRHO_PLUGIN_BRAND   "Ostrich"
#define DISTRHO_PLUGIN_NAME    "Ostrich"
#define DISTRHO_PLUGIN_URI     "urn:ostrich:u220"
#define DISTRHO_PLUGIN_CLAP_ID "org.ostrich.u220"

#define DISTRHO_PLUGIN_BRAND_ID  Ostr
#define DISTRHO_PLUGIN_UNIQUE_ID U220

#define DISTRHO_PLUGIN_HAS_UI           1
#define DISTRHO_PLUGIN_IS_SYNTH         1
#define DISTRHO_PLUGIN_IS_RT_SAFE       1
#define DISTRHO_PLUGIN_NUM_INPUTS       0
#define DISTRHO_PLUGIN_NUM_OUTPUTS      2
#define DISTRHO_PLUGIN_WANT_MIDI_INPUT  1
#define DISTRHO_PLUGIN_WANT_MIDI_OUTPUT 0
#define DISTRHO_PLUGIN_WANT_LATENCY     1

// The panel goes to the UI as one blob through updateStateValue() (patches 0001/0003/0005
// give JACK, CLAP and VST3 that path), the NVRAM into the host's session.
#define DISTRHO_PLUGIN_WANT_STATE       1
#define DISTRHO_PLUGIN_WANT_FULL_STATE  1

#define DISTRHO_UI_USE_NANOVG           1
#define DISTRHO_UI_DEFAULT_WIDTH        960
#define DISTRHO_UI_DEFAULT_HEIGHT       240
#define DISTRHO_UI_USER_RESIZABLE       1

#define DISTRHO_PLUGIN_LV2_CATEGORY "lv2:InstrumentPlugin"
#define DISTRHO_PLUGIN_VST3_CATEGORIES "Instrument|Synth"
#define DISTRHO_PLUGIN_CLAP_FEATURES "instrument", "synthesizer", "stereo"

#endif // DISTRHO_PLUGIN_INFO_H_INCLUDED
