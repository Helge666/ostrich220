// license:BSD-3-Clause
// copyright-holders:Helge Thorsten Kautz
/***************************************************************************************

    U220Reverb -- the U-220's reverb/delay block, approximated from measurements

    Not an emulation of IC25: an algorithmic stand-in (ROADMAP.md, Phase 3), tuned to
    impulse responses of Helge's U-220 taken through its own reverb (tools/
    make_reverb_probe.py, deconvolved against the emulation's send bus):

      Room1-3, Hall1-2  an 8-line feedback delay network after four input diffusers; the
                        decay per band follows the measured reverberation time at 250 Hz
                        and 8 kHz, interpolated over Time from five measured points
      Gate              the same network with a short fixed gate on its output
      Delay             one echo line, period 10 + 12.9 ms x Time, feedback 0.8 (FB/31)^2
      CrossDelay        the same line, left tap at the period, right tap at half of it

    The chorus is not here yet; with "Out = Pre Rev" the chorus bus is fed in dry.

***************************************************************************************/

#ifndef OSTRICH_U220_REVERB_H
#define OSTRICH_U220_REVERB_H

#pragma once

#include "u220_effects.h"

#include <vector>

namespace ostrich {

class U220Reverb final : public EffectsModule
{
public:
	U220Reverb();

	void setParams(const EffectParams &p) override;
	void write(uint16_t, uint8_t, double) override { }
	void process(const float *revL, const float *revR, const float *choL, const float *choR,
			float *outL, float *outR, uint32_t nframes) override;
	void reset() override;

private:
	static constexpr int LINES = 8;

	void configure();
	float network(float in, float &outR);

	EffectParams m_p;
	bool m_dirty = true;

	// --- the network
	std::vector<float> m_line[LINES];
	unsigned m_len[LINES] = {}, m_pos[LINES] = {};
	float m_b[LINES] = {}, m_a[LINES] = {}, m_lp[LINES] = {};     // per-line absorption
	std::vector<float> m_diff[4];
	unsigned m_dlen[4] = {}, m_dpos[4] = {};
	std::vector<float> m_pre;
	unsigned m_prelen = 1, m_prepos = 0;

	// --- the gate
	float m_env = 0.0f, m_gate = 0.0f;
	unsigned m_hold = 0;

	// --- the echo line
	std::vector<float> m_echo;
	unsigned m_echolen = 1, m_echopos = 0;
	float m_echofb = 0.0f;

	float m_gain = 0.0f;            // Level and calibration
};

} // namespace ostrich

#endif // OSTRICH_U220_REVERB_H
