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
constexpr float MIN_BASE[5] = { 16.6f, 8.6f, 16.6f, 0.66f, 0.5f };
constexpr float MIN_STEP[5] = { 1.03f, 0.516f, 1.03f, 0.516f, 2.05f };
// The copy's gain at Level 31, and the feedback per step of Feedback.
constexpr float GAIN31[5]   = { 0.94f, 0.94f, 0.94f, 0.94f, 0.99f };
constexpr float FB_STEP[5]  = { 0.0f, 0.0f, 0.0274f, 0.0270f, 0.0284f };

// LFO speed in ms of delay per second, measured at these settings of the field at bits
// 11-15; linear between.  At 0 it barely moves (0.9 ms in 4 s).
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
	m_lineL.assign(size_t(0.1 * FS), 0.0f);
	m_lineR.assign(size_t(0.1 * FS), 0.0f);
	m_p.chorus_level = -1;          // force the first setParams through
}

void U220Chorus::reset()
{
	std::fill(m_lineL.begin(), m_lineL.end(), 0.0f);
	std::fill(m_lineR.begin(), m_lineR.end(), 0.0f);
	m_lfo = 0.0f;
	m_dir = 1.0f;
	m_yL = m_yR = 0.0f;
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
		m_span = std::max(15.63f - 0.49f * std::clamp(p.chorus_rate, 0, 31), 0.0f) * ms;
		m_step = table(SPEED_AT, 6, float(std::clamp(p.chorus_depth, 0, 31))) / 1000.0f;
	}
	m_lfo = std::min(m_lfo, m_span);
	m_gain = (p.chorus_on && level > 0)
			? GAIN31[type] * std::pow(10.0f, table(LEVEL_DB, 5, float(level)) / 20.0f) : 0.0f;
	const float fb = FB_STEP[type] * std::clamp(p.chorus_feedback, -31, 31);
	m_fbR = fb;
	m_fbL = type == EffectParams::ShortDelay ? fb : 0.0f;
}

// Linear interpolation, `delay` samples back from the write position `pos`.
float U220Chorus::read(const std::vector<float> &line, unsigned pos, float delay) const
{
	const unsigned n = unsigned(line.size());
	const float d = std::clamp(delay, 1.0f, float(n - 2));
	const unsigned i = unsigned(d);
	const float f = d - float(i);
	const float a = line[(pos + n - i) % n], b = line[(pos + n - i - 1) % n];
	return a + f * (b - a);
}

void U220Chorus::process(const float *choL, const float *choR, float *outL, float *outR, uint32_t nframes)
{
	if (m_gain <= 0.0f)
		return;
	const unsigned n = unsigned(m_lineL.size());
	for (uint32_t i = 0; i < nframes; i ++)
	{
		const float in = 0.5f * (choL[i] + choR[i]);
		m_lineL[m_pos] = in + m_fbL * m_yL;
		m_lineR[m_pos] = in + m_fbR * m_yR;

		// Triangle between 0 and the span; the two sides in antiphase.
		m_lfo += m_dir * m_step;
		if (m_lfo >= m_span) { m_lfo = m_span; m_dir = -1.0f; }
		else if (m_lfo <= 0.0f) { m_lfo = 0.0f; m_dir = 1.0f; }

		m_yL = read(m_lineL, m_pos, m_min + m_lfo);
		m_yR = read(m_lineR, m_pos, m_min + (m_span - m_lfo));
		m_pos = (m_pos + 1) % n;

		outL[i] += m_gain * m_yL;
		outR[i] += m_gain * m_yR;
	}
}

} // namespace ostrich
