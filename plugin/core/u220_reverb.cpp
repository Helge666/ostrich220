// license:BSD-3-Clause
// copyright-holders:Helge Thorsten Kautz
/***************************************************************************************

    U220Reverb -- see u220_reverb.h

***************************************************************************************/

#include "u220_reverb.h"
#include "u220_core.h"

#include <algorithm>
#include <cmath>

namespace ostrich {

namespace {

constexpr double FS = kCoreSampleRate;

// Reverberation time in seconds, measured on Helge's U-220: per type Room1..Hall2, at
// Time 0, 8, 16, 24, 31, in octave bands 250, 500, 1k, 2k, 4k, 8k Hz.  Only the 250 Hz
// and 8 kHz columns drive the network; the others document what the one-pole damping
// interpolates between.
constexpr float RT[5][5][6] = {
	{ // Room1
		{ 1.46f, 1.27f, 1.31f, 1.38f, 1.20f, 0.90f },   // time 0
		{ 1.82f, 1.49f, 1.40f, 1.34f, 1.21f, 0.97f },   // time 8
		{ 2.27f, 1.82f, 1.44f, 1.34f, 1.26f, 0.96f },   // time 16
		{ 2.63f, 2.13f, 1.55f, 1.37f, 1.27f, 0.94f },   // time 24
		{ 3.09f, 2.29f, 1.59f, 1.35f, 1.28f, 0.91f },   // time 31
	},
	{ // Room2
		{ 1.51f, 1.64f, 1.63f, 1.68f, 1.53f, 1.29f },
		{ 2.16f, 2.45f, 2.13f, 1.87f, 1.72f, 1.36f },
		{ 2.98f, 3.21f, 2.78f, 2.53f, 2.05f, 1.43f },
		{ 3.73f, 4.11f, 3.36f, 2.88f, 2.23f, 1.52f },
		{ 4.52f, 4.90f, 4.01f, 3.43f, 2.32f, 1.57f },
	},
	{ // Room3
		{ 1.62f, 1.53f, 1.27f, 1.50f, 1.31f, 1.23f },
		{ 2.30f, 2.20f, 1.72f, 1.88f, 1.56f, 1.33f },
		{ 2.95f, 2.84f, 2.40f, 2.45f, 1.82f, 1.42f },
		{ 3.79f, 3.57f, 3.09f, 2.94f, 2.07f, 1.48f },
		{ 4.50f, 4.13f, 3.46f, 2.93f, 2.23f, 1.60f },
	},
	{ // Hall1
		{ 2.66f, 2.62f, 2.48f, 2.29f, 1.80f, 1.28f },
		{ 3.72f, 3.52f, 3.40f, 2.98f, 2.22f, 1.36f },
		{ 5.01f, 4.71f, 4.30f, 3.56f, 2.54f, 1.45f },
		{ 5.99f, 5.59f, 5.23f, 4.29f, 2.79f, 1.56f },
		{ 7.01f, 6.53f, 5.82f, 4.68f, 3.00f, 1.58f },
	},
	{ // Hall2
		{ 2.59f, 2.57f, 2.48f, 2.23f, 1.97f, 1.70f },
		{ 3.71f, 3.63f, 3.39f, 3.11f, 2.39f, 1.92f },
		{ 4.97f, 4.62f, 4.32f, 3.83f, 2.80f, 2.09f },
		{ 5.98f, 5.62f, 5.25f, 4.33f, 3.13f, 2.23f },
		{ 6.94f, 6.58f, 6.09f, 5.17f, 3.34f, 2.35f },
	},
};

// Line lengths in samples at 32 kHz (mutually prime), scaled per type; rooms are smaller.
constexpr unsigned BASE_LEN[8] = { 1031, 1201, 1361, 1493, 1657, 1777, 1913, 2053 };
constexpr float TYPE_SIZE[6]   = { 0.55f, 0.75f, 0.65f, 1.15f, 1.0f, 0.6f };   // ..Hall2, Gate
constexpr float TYPE_PRE_MS[6] = { 6.0f, 10.0f, 8.0f, 22.0f, 18.0f, 4.0f };
// Output calibration per type (all 8 types), against the tail level of the hardware probe.
constexpr float TYPE_GAIN[8]   = { 0.35f, 0.35f, 0.35f, 0.35f, 0.35f, 0.35f, 0.5f, 0.5f };
constexpr unsigned DIFF_LEN[4] = { 142, 107, 379, 277 };
constexpr float DIFF_G = 0.65f;

float rt_at(int type, int time, int band)
{
	static constexpr int T[5] = { 0, 8, 16, 24, 31 };
	int i = 0;
	while (i < 3 && time > T[i + 1])
		i ++;
	const float f = float(time - T[i]) / float(T[i + 1] - T[i]);
	return RT[type][i][band] + f * (RT[type][i + 1][band] - RT[type][i][band]);
}

// One-pole y = b x + a y[-1] with gain g_lo at DC and g_hi at 8 kHz.
void absorption(double g_lo, double g_hi, float &b, float &a)
{
	const double w = 2.0 * M_PI * 8000.0 / FS;
	double lo = 0.0, hi = 0.999;
	for (int it = 0; it < 60; it ++)
	{
		const double am = 0.5 * (lo + hi);
		const double bb = g_lo * (1.0 - am);
		const double mag = bb / std::sqrt(1.0 - 2.0 * am * std::cos(w) + am * am);
		if (mag > g_hi) lo = am; else hi = am;
	}
	a = float(0.5 * (lo + hi));
	b = float(g_lo * (1.0 - a));
}

} // anonymous namespace


U220Reverb::U220Reverb()
{
	for (int i = 0; i < LINES; i ++)
		m_line[i].assign(size_t(BASE_LEN[i] * 1.2f) + 8, 0.0f);
	for (int i = 0; i < 4; i ++)
		m_diff[i].assign(DIFF_LEN[i], 0.0f);
	m_pre.assign(size_t(0.05 * FS) + 8, 0.0f);
	m_echo.assign(size_t(0.5 * FS) + 8, 0.0f);
	configure();
}

void U220Reverb::reset()
{
	for (auto &l : m_line) std::fill(l.begin(), l.end(), 0.0f);
	for (auto &d : m_diff) std::fill(d.begin(), d.end(), 0.0f);
	std::fill(m_pre.begin(), m_pre.end(), 0.0f);
	std::fill(m_echo.begin(), m_echo.end(), 0.0f);
	std::fill(std::begin(m_lp), std::end(m_lp), 0.0f);
	m_env = m_gate = 0.0f;
	m_hold = 0;
}

void U220Reverb::setParams(const EffectParams &p)
{
	if (p == m_p)
		return;
	m_p = p;
	configure();
}

void U220Reverb::configure()
{
	const int type = std::clamp(m_p.reverb_type, 0, 7);
	const int time = std::clamp(m_p.reverb_time, 0, 31);

	// Level: 31 is the reference; 20 and 10 measured 7.8 and 16.3 dB down, 0 is off.
	const float lv = float(std::clamp(m_p.reverb_level, 0, 31)) / 31.0f;
	m_gain = (m_p.reverb_on ? 1.0f : 0.0f) * std::pow(lv, 1.75f) * TYPE_GAIN[type];

	if (type <= EffectParams::Gate)
	{
		const int t = std::min(type, 4);
		// The gate runs the network as a mid-size hall and cuts it short.
		const float rt_lo = type == EffectParams::Gate ? 1.2f : rt_at(t, time, 0);
		const float rt_hi = type == EffectParams::Gate ? 0.9f : rt_at(t, time, 5);
		for (int i = 0; i < LINES; i ++)
		{
			m_len[i] = std::min<unsigned>(unsigned(BASE_LEN[i] * TYPE_SIZE[type]), unsigned(m_line[i].size() - 1));
			const double g_lo = std::pow(10.0, -3.0 * m_len[i] / (FS * rt_lo));
			const double g_hi = std::pow(10.0, -3.0 * m_len[i] / (FS * rt_hi));
			absorption(g_lo, g_hi, m_b[i], m_a[i]);
		}
		m_prelen = std::max(1u, unsigned(TYPE_PRE_MS[type] * 1e-3 * FS));
	}
	else
	{
		const double period = (10.0 + 12.9 * time) * 1e-3;
		m_echolen = std::clamp<unsigned>(unsigned(period * FS), 2u, unsigned(m_echo.size() - 1));
		const float fb = float(std::clamp(m_p.delay_feedback, 0, 31)) / 31.0f;
		m_echofb = 0.8f * fb * fb;
	}
}

// The network: predelay, diffusers, eight lines through an 8x8 Hadamard matrix.
float U220Reverb::network(float in, float &outR)
{
	m_pre[m_prepos] = in;
	unsigned rp = (m_prepos + m_pre.size() - m_prelen) % m_pre.size();
	float x = m_pre[rp];
	m_prepos = (m_prepos + 1) % m_pre.size();

	for (int d = 0; d < 4; d ++)
	{
		float &cell = m_diff[d][m_dpos[d]];
		const float v = x + DIFF_G * cell;
		const float y = cell - DIFF_G * v;
		cell = v;
		m_dpos[d] = (m_dpos[d] + 1) % DIFF_LEN[d];
		x = y;
	}

	float o[LINES];
	for (int i = 0; i < LINES; i ++)
	{
		const unsigned n = unsigned(m_line[i].size());
		const float raw = m_line[i][(m_pos[i] + n - m_len[i]) % n];
		m_lp[i] = m_b[i] * raw + m_a[i] * m_lp[i];
		o[i] = m_lp[i];
	}
	// Fast Walsh-Hadamard, scaled to stay energy preserving.
	float h[LINES];
	std::copy(o, o + LINES, h);
	for (int len = 1; len < LINES; len <<= 1)
		for (int i = 0; i < LINES; i += len << 1)
			for (int j = i; j < i + len; j ++)
			{
				const float u = h[j], v = h[j + len];
				h[j] = u + v;
				h[j + len] = u - v;
			}
	const float s = 1.0f / std::sqrt(float(LINES));
	for (int i = 0; i < LINES; i ++)
	{
		const unsigned n = unsigned(m_line[i].size());
		m_line[i][m_pos[i]] = h[i] * s + x;
		m_pos[i] = (m_pos[i] + 1) % n;
	}
	outR = o[1] - o[3] + o[5] - o[7];
	return o[0] - o[2] + o[4] - o[6];
}

void U220Reverb::process(const float *revL, const float *revR, const float *choL,
		const float *choR, float *outL, float *outR, uint32_t nframes)
{
	if (m_gain <= 0.0f)
		return;
	const int type = std::clamp(m_p.reverb_type, 0, 7);
	const bool pre_chorus = !m_p.chorus_post_reverb;

	for (uint32_t i = 0; i < nframes; i ++)
	{
		float in = 0.5f * (revL[i] + revR[i]);
		if (pre_chorus)
			in += 0.5f * (choL[i] + choR[i]);

		float l, r;
		if (type <= EffectParams::Gate)
		{
			l = network(in, r);
			if (type == EffectParams::Gate)
			{
				// Open on any input, hold ~250 ms after it stops, close in ~15 ms.
				const float a = std::fabs(in);
				m_env = std::max(a, m_env * 0.9995f);
				if (a > 1e-4f) m_hold = unsigned(0.25 * FS);
				else if (m_hold) m_hold --;
				const float target = m_hold ? 1.0f : 0.0f;
				m_gate += (target - m_gate) * (target > m_gate ? 0.05f : 0.002f);
				l *= m_gate;
				r *= m_gate;
			}
		}
		else
		{
			// s(t) = x(t) + fb s(t - D); left hears it at D, right at D (Delay) or D/2.
			const unsigned n = unsigned(m_echo.size());
			const float back = m_echo[(m_echopos + n - m_echolen) % n];
			m_echo[m_echopos] = in + m_echofb * back;
			l = back;
			r = type == EffectParams::CrossDelay
					? m_echo[(m_echopos + n - m_echolen / 2) % n] : back;
			m_echopos = (m_echopos + 1) % n;
		}
		outL[i] += m_gain * l;
		outR[i] += m_gain * r;
	}
}

} // namespace ostrich
