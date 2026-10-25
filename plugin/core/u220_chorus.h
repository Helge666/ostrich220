// license:BSD-3-Clause
// copyright-holders:Helge Thorsten Kautz
/***************************************************************************************

    U220Chorus -- the U-220's chorus block, approximated from measurements

    Not an emulation of IC25 (ROADMAP.md, Phase 3): a model of what a held sawtooth
    through the hardware shows (tools/make_chorus_probe.py, tools/chorus_analyse.py,
    25.10.2026).  Per side one delayed copy of the chorus send:

      delay     a minimum set by Delay -- per type, e.g. Chorus1 16.6 ms + 1.03 ms a step
                -- plus the LFO on top of it
      LFO       a triangle that moves at a fixed speed (ms of delay per second, set by
                the field at bits 11-15) between 0 and a span (set by the field at bits
                0-4: 15.6 ms at 0, 0.49 ms less a step); its rate is speed / (2 span).
                Left and right in antiphase.  Short Delay has none
      gain      the copy's gain by Level, 0.94 at 31, about 0.7 dB less a step
      Chorus2   adds a second copy: a second tap 12 ms under the main one, swept 3.3
                times as fast over the same span, unfiltered, 0.8 -- its growl
      feedback  one delay line for both sides, fed back from the right tap (FB-Chorus,
                Flanger, Short Delay): 0.027 a step of Feedback, sign included; the left
                tap hears the recirculation too, at tau_L + tau_R

    The menu agrees with EffectParams' names (Helge read P-02: Rate 4, Depth 5): Rate is
    the field at bits 0-4, Depth the one at 11-15.  So on the U-220 a higher Rate makes
    the sweep narrower -- and with it faster -- and a higher Depth makes it move faster
    over the same span.

***************************************************************************************/

#ifndef OSTRICH_U220_CHORUS_H
#define OSTRICH_U220_CHORUS_H

#pragma once

#include "u220_effects.h"

#include <vector>

namespace ostrich {

class U220Chorus
{
public:
	U220Chorus();

	void setParams(const EffectParams &p);
	void reset();

	/// Adds the wet copies of the chorus send (choL/choR) to out.
	void process(const float *choL, const float *choR, float *outL, float *outR, uint32_t nframes);

private:
	float read(const std::vector<float> &line, unsigned pos, float delay) const;

	EffectParams m_p;
	std::vector<float> m_line;
	unsigned m_pos = 0;
	float m_min = 0.0f;             // minimum delay, samples
	float m_span = 0.0f;            // LFO span, samples
	float m_step = 0.0f;            // LFO speed, samples per sample
	float m_lfo = 0.0f;             // m_lo .. m_hi, within 0 .. m_span
	float m_lo = 0.0f, m_hi = 0.0f;
	float m_dir = 1.0f;
	float m_gain = 0.0f;
	float m_fb = 0.0f;
	float m_yL = 0.0f, m_yR = 0.0f; // last outputs, for the feedback

	// Chorus2's second copy
	bool m_bass = false;
	float m_min2 = 0.0f, m_step2 = 0.0f, m_lfo2 = 0.0f, m_dir2 = 1.0f, m_span2 = 1.0f, m_gain2 = 0.0f;
	float m_lp[5] = {}, m_lpz[2][2] = {};       // low-pass b0 b1 b2 a1 a2, states
};

} // namespace ostrich

#endif // OSTRICH_U220_CHORUS_H
