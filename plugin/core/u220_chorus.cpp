// license:BSD-3-Clause
// copyright-holders:Helge Thorsten Kautz
/***************************************************************************************

    U220Chorus -- see u220_chorus.h

***************************************************************************************/

#include "u220_chorus.h"
#include "u220_core.h"

#include <algorithm>
#include <cmath>

namespace ostrich {

namespace {

constexpr double FS = kCoreSampleRate;

// Minimum delay in ms: base + step x Delay, per type (Chorus1, Chorus2, FB-Chorus,
// Flanger, Short Delay).
// (The chorus types 0.6 ms under what the frozen-LFO slots suggested: the modulated
// slots, which show the bottom of the sweep directly, put it there.)
constexpr float MIN_BASE[5] = { 16.0f, 8.0f, 16.0f, 0.06f, 0.5f };
constexpr float MIN_STEP[5] = { 1.03f, 0.516f, 1.03f, 0.516f, 2.05f };
// The copy's gain at Level 31, and the feedback per step of Feedback.
constexpr float GAIN31[5]   = { 0.94f, 0.94f, 0.94f, 0.94f, 0.99f };
constexpr float FB_STEP[5]  = { 0.0f, 0.0f, 0.0274f, 0.0270f, 0.0284f };
// Chorus2's bass copy: low-pass corner and gain.
constexpr double BASS_HZ = 220.0;
constexpr float BASS_GAIN = 0.65f;

// LFO speed in ms of delay per second, measured at these settings of the field at bits
// 11-15; linear between.  At 0 see setParams.
constexpr float SPEED_AT[6][2] = { { 0, 0.45f }, { 5, 4.1f }, { 8, 7.3f }, { 16, 16.7f },
		{ 24, 31.6f }, { 31, 60.1f } };
// Gain by Level, in dB under Level 31: measured at 24, 16, 8.
constexpr float LEVEL_DB[5][2] = { { 1, -22.2f }, { 8, -17.2f }, { 16, -11.5f }, { 24, -5.6f }, { 31, 0.0f } };

float table(const float (*t)[2], int n, float x)
{
	if (x <= t[0][0])
		return t[0][1];
	for (int k = 1; k < n; k ++)
		if (x <= t[k][0])
			return t[k - 1][1] + (x - t[k - 1][0]) / (t[k][0] - t[k - 1][0]) * (t[k][1] - t[k - 1][1]);
	return t[n - 1][1];
}

} // anonymous namespace

U220Chorus::U220Chorus()
{
	// The longest: Chorus1 at Delay 31 plus the full span, or Short Delay at 31.
	m_line.assign(size_t(0.1 * FS), 0.0f);
	m_p.chorus_level = -1;          // force the first setParams through
}

void U220Chorus::reset()
{
	std::fill(m_line.begin(), m_line.end(), 0.0f);
	m_lfo = 0.0f;
	m_dir = 1.0f;
	m_lfo2 = 0.0f;
	m_dir2 = 1.0f;
	m_yL = m_yR = 0.0f;
	std::fill(&m_lpz[0][0], &m_lpz[0][0] + 4, 0.0f);
}

void U220Chorus::setParams(const EffectParams &p)
{
	if (p == m_p)
		return;
	m_p = p;
	const int type = std::clamp(p.chorus_type, 0, 4);
	const int delay = std::clamp(p.chorus_delay, 0, 31);
	const int level = std::clamp(p.chorus_level, 0, 31);
	const float ms = float(FS) / 1000.0f;

	m_min = (MIN_BASE[type] + MIN_STEP[type] * delay) * ms;
	if (type == EffectParams::ShortDelay)
	{
		m_span = 0.0f;
		m_step = 0.0f;
	}
	else
	{
		// 16 ms at Rate 0, 0.49 ms at 31 as measured -- by ear 0.44 ms ran too fast and
		// 0.51 too slow: at Rate 31 the rate goes with 1 / span (~17 Hz at Depth 16).
		m_span = std::max(16.0f - 0.5004f * std::clamp(p.chorus_rate, 0, 31), 0.0f) * ms;
		m_step = table(SPEED_AT, 6, float(std::clamp(p.chorus_depth, 0, 31))) / 1000.0f;
	}
	// At Depth 0 the hardware parks the LFO near the top of its span -- the left copy
	// 0.6 ms under the longest delay, the right one 0.6 ms over the shortest, in every
	// such slot of every type -- and lets it wobble there by 0.87 ms at 0.26 Hz.
	m_lo = 0.0f;
	m_hi = m_span;
	if (p.chorus_depth <= 0 && m_span > 0.0f)
	{
		m_hi = std::max(m_span - 0.17f * ms, 0.0f);
		m_lo = std::max(m_span - 1.04f * ms, 0.0f);
	}
	m_lfo = std::clamp(m_lfo, m_lo, m_hi);
	m_gain = (p.chorus_on && level > 0)
			? GAIN31[type] * std::pow(10.0f, table(LEVEL_DB, 5, float(level)) / 20.0f) : 0.0f;
	m_fb = FB_STEP[type] * std::clamp(p.chorus_feedback, -31, 31);

	// Chorus2's bass copy (its "growl", 25.10.2026): a second tap 12 ms under the main
	// one's shortest delay, swept over the same span 3.3 times as fast, the sides in
	// antiphase, through a low-pass -- 0.63 of the dry below 250 Hz, little above.  Found
	// as what dry + main copy leave unexplained in the held-sawtooth recording.
	m_bass = type == EffectParams::Chorus2;
	m_min2 = std::max(m_min - 12.0f * ms, 1.0f);
	m_step2 = 3.3f * m_step;
	m_lfo2 = std::clamp(m_lfo2, m_lo, m_hi);
	const double w = 2.0 * M_PI * BASS_HZ / FS, al = std::sin(w) / (2.0 * 0.7071), c = std::cos(w);
	const double a0 = 1.0 + al;
	m_lp[0] = float((1.0 - c) / 2.0 / a0);
	m_lp[1] = float((1.0 - c) / a0);
	m_lp[2] = m_lp[0];
	m_lp[3] = float(-2.0 * c / a0);
	m_lp[4] = float((1.0 - al) / a0);
}

// `delay` samples back from the write position `pos`, cubic (Catmull-Rom): linear
// interpolation dulled the copy, ~0.5 dB under the hardware in the edge heights.
float U220Chorus::read(const std::vector<float> &line, unsigned pos, float delay) const
{
	const unsigned n = unsigned(line.size());
	const float d = std::clamp(delay, 1.0f, float(n - 3));
	const unsigned i = unsigned(d);
	const float t = d - float(i);
	const float y0 = line[(pos + n - i + 1) % n], y1 = line[(pos + n - i) % n];
	const float y2 = line[(pos + n - i - 1) % n], y3 = line[(pos + n - i - 2) % n];
	return y1 + 0.5f * t * (y2 - y0 + t * (2.0f * y0 - 5.0f * y1 + 4.0f * y2 - y3
			+ t * (3.0f * (y1 - y2) + y3 - y0)));
}

void U220Chorus::process(const float *choL, const float *choR, float *outL, float *outR, uint32_t nframes)
{
	if (m_gain <= 0.0f)
		return;
	const unsigned n = unsigned(m_line.size());
	for (uint32_t i = 0; i < nframes; i ++)
	{
		// One line for both sides, fed back from the right tap: the left output shows the
		// recirculation too, at tau_L + tau_R (Flanger FB +31: 0.81 there, 25.10.2026).
		m_line[m_pos] = 0.5f * (choL[i] + choR[i]) + m_fb * m_yR;

		// Triangle between 0 and the span; the two sides in antiphase.
		m_lfo += m_dir * m_step;
		if (m_lfo >= m_hi) { m_lfo = m_hi; m_dir = -1.0f; }
		else if (m_lfo <= m_lo) { m_lfo = m_lo; m_dir = 1.0f; }

		m_yL = read(m_line, m_pos, m_min + m_lfo);
		m_yR = read(m_line, m_pos, m_min + (m_span - m_lfo));
		float bL = 0.0f, bR = 0.0f;
		if (m_bass)
		{
			m_lfo2 += m_dir2 * m_step2;
			if (m_lfo2 >= m_hi) { m_lfo2 = m_hi; m_dir2 = -1.0f; }
			else if (m_lfo2 <= m_lo) { m_lfo2 = m_lo; m_dir2 = 1.0f; }
			float *side[2] = { &bL, &bR };
			const float in2[2] = { read(m_line, m_pos, m_min2 + m_lfo2),
			                       read(m_line, m_pos, m_min2 + (m_span - m_lfo2)) };
			for (int c = 0; c < 2; c ++)
			{
				// Direct form II transposed.
				float *z = m_lpz[c];
				const float y = m_lp[0] * in2[c] + z[0];
				z[0] = m_lp[1] * in2[c] - m_lp[3] * y + z[1];
				z[1] = m_lp[2] * in2[c] - m_lp[4] * y;
				*side[c] = BASS_GAIN * y;
			}
		}
		m_pos = (m_pos + 1) % n;

		outL[i] += m_gain * (m_yL + bL);
		outR[i] += m_gain * (m_yR + bR);
	}
}

} // namespace ostrich
