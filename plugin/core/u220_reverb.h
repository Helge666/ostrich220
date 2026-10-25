// license:BSD-3-Clause
// copyright-holders:Helge Thorsten Kautz
/***************************************************************************************

    U220Reverb -- the U-220's reverb/delay block, approximated from measurements

    Not an emulation of IC25: an algorithmic stand-in (ROADMAP.md, Phase 3), tuned to
    recordings of Helge's U-220 through its own reverb (tools/make_reverb_probe.py):

      Room1-3, Hall1-2  an 8-line feedback delay network after four input diffusers; the
                        decay per band (low / mid / high shelving absorption in every
                        line) follows the measured reverberation time, interpolated over
                        Time from five measured points
      Gate              the same network with a short fixed gate on its output
      Delay             one echo line, period 10 + 12.9 ms x Time, feedback 0.8 (FB/31)^2
      CrossDelay        the same line, left tap at the period, right tap at half of it

    Every type ends in an octave-band output EQ, also per Time point: the colour of the
    hardware's reverb, band by band.

    The numbers live in ReverbTuning, which tools/reverb_fit.py fits automatically
    against a hardware recording (band-by-band energy decay, tools/reverb_edr.py) and
    writes out as u220_reverb_tuning.inc; plugin/tools/u220_reverb_run loads a tuning
    file at run time so the fit needs no rebuild.

    The chorus is not here yet; with "Out = Pre Rev" the chorus bus is fed in dry.

***************************************************************************************/

#ifndef OSTRICH_U220_REVERB_H
#define OSTRICH_U220_REVERB_H

#pragma once

#include "u220_effects.h"

#include <string>
#include <vector>

namespace ostrich {

struct ReverbTuning
{
	static constexpr int TYPES = 8;          // Room1 .. CrossDelay
	static constexpr int NET_TYPES = 6;      // Room1 .. Gate run the network
	static constexpr int TIMES = 5;          // the measured Time points 0, 8, 16, 24, 31
	static constexpr int DECAY_BANDS = 3;    // RT at the low shelf, mid, high shelf
	static constexpr int EQ_BANDS = 7;       // octaves 125 Hz .. 8 kHz
	static constexpr int TIME_AT[TIMES] = { 0, 8, 16, 24, 31 };
	static constexpr float EQ_HZ[EQ_BANDS] = { 125, 250, 500, 1000, 2000, 4000, 8000 };

	float rt[NET_TYPES][TIMES][DECAY_BANDS] = {};     // seconds
	float eq[TYPES][TIMES][EQ_BANDS] = {};            // dB
	float gain[TYPES] = {};                           // linear, at Level 31
	float size[NET_TYPES] = {};                       // line length scale
	float pre_ms[NET_TYPES] = {};
	// The gate: open for `hold` after an attack, then a release at so many dB/s.
	float gate_hold_ms[TIMES] = {};
	float gate_release_dbps[TIMES] = {};

	/// Lines of "rt T I lo mid hi", "eq T I b0..b6", "gain T g", "size T s", "pre T ms",
	/// "gate 5 I hold_ms release_dB/s"; '#' starts a comment.  Anything not mentioned
	/// keeps its value.
	bool parse(const std::string &text, std::string *error = nullptr);
	std::string format() const;

	/// The fitted tuning built into the plugin (u220_reverb_tuning.inc).
	static const ReverbTuning &builtin();
};

class U220Reverb final : public EffectsModule
{
public:
	U220Reverb();

	void setTuning(const ReverbTuning &t) { m_t = t; configure(); }

	void setParams(const EffectParams &p) override;
	void write(uint16_t, uint8_t, double) override { }
	void process(const float *revL, const float *revR, const float *choL, const float *choR,
			float *outL, float *outR, uint32_t nframes) override;
	void reset() override;

private:
	static constexpr int LINES = 8;
	static constexpr int EQ_BANDS = ReverbTuning::EQ_BANDS;

	void configure();
	float network(float in, float &outR);

	ReverbTuning m_t;
	EffectParams m_p;

	// --- the network
	std::vector<float> m_line[LINES];
	unsigned m_len[LINES] = {}, m_pos[LINES] = {};
	float m_gm[LINES] = {};                                     // mid-band loop gain
	float m_ls[LINES][3] = {}, m_hs[LINES][3] = {};             // first-order shelves b0 b1 a1
	float m_lsz[LINES] = {}, m_hsz[LINES] = {};                 // ...their states
	std::vector<float> m_diff[4];
	unsigned m_dlen[4] = {}, m_dpos[4] = {};
	std::vector<float> m_pre;
	unsigned m_prelen = 1, m_prepos = 0;

	// --- the gate
	float m_env = 0.0f, m_gate = 0.0f;
	unsigned m_hold = 0, m_holdLen = 0;
	float m_release = 0.0f;         // per-sample gain once the hold has run out

	// --- the echo line
	std::vector<float> m_echo;
	unsigned m_echolen = 1, m_echopos = 0;
	float m_echofb = 0.0f;

	// --- the output EQ: peaking biquads per octave, both sides
	float m_eq[EQ_BANDS][5] = {};                               // b0 b1 b2 a1 a2
	float m_eqz[2][EQ_BANDS][2] = {};
	bool m_eqFlat = true;

	float m_gain = 0.0f;            // Level and calibration
};

} // namespace ostrich

#endif // OSTRICH_U220_REVERB_H
