// license:BSD-3-Clause
// copyright-holders:Helge Thorsten Kautz
/***************************************************************************************

    U220Reverb -- see u220_reverb.h

***************************************************************************************/

#include "u220_reverb.h"
#include "u220_core.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace ostrich {

namespace {

constexpr double FS = kCoreSampleRate;

// The fitted numbers, as text in ReverbTuning::parse's format.
constexpr const char BUILTIN_TUNING[] =
#include "u220_reverb_tuning.inc"
;

// Line lengths in samples at 32 kHz (mutually prime), scaled per type; rooms are smaller.
constexpr unsigned BASE_LEN[8] = { 1031, 1201, 1361, 1493, 1657, 1777, 1913, 2053 };
constexpr unsigned DIFF_LEN[4] = { 142, 107, 379, 277 };
constexpr float DIFF_G = 0.65f;
// Where the absorption's low and high shelves turn over.
constexpr double LOW_SHELF_HZ = 300.0, HIGH_SHELF_HZ = 3000.0;
constexpr double EQ_Q = 1.41;           // one octave

// Linear interpolation over the measured Time points.
void time_weights(int time, int &i, float &f)
{
	const int *T = ReverbTuning::TIME_AT;
	i = 0;
	while (i < ReverbTuning::TIMES - 2 && time > T[i + 1])
		i ++;
	f = float(std::clamp(time, T[i], T[i + 1]) - T[i]) / float(T[i + 1] - T[i]);
}

// First-order shelves by the bilinear transform: gain `g` below (low) or above (high)
// the turnover, unity on the other side.  b0 b1 a1 of y = b0 x + b1 x[-1] - a1 y[-1].
void low_shelf(double g, double hz, float c[3])
{
	const double k = std::tan(M_PI * hz / FS);
	const double a0 = 1.0 + k;
	c[0] = float((1.0 + g * k) / a0);
	c[1] = float((g * k - 1.0) / a0);
	c[2] = float((k - 1.0) / a0);
}

void high_shelf(double g, double hz, float c[3])
{
	const double k = std::tan(M_PI * hz / FS);
	const double a0 = 1.0 + k;
	c[0] = float((g + k) / a0);
	c[1] = float((k - g) / a0);
	c[2] = float((k - 1.0) / a0);
}

// Direct form II transposed, for the shelves: one state each.
inline float shelf(const float c[3], float &z, float x)
{
	const float y = c[0] * x + z;
	z = c[1] * x - c[2] * y;
	return y;
}

} // anonymous namespace


// ---- ReverbTuning ---------------------------------------------------------------------

bool ReverbTuning::parse(const std::string &text, std::string *error)
{
	std::istringstream in(text);
	std::string line;
	int lineno = 0;
	auto fail = [&](const char *what) {
		if (error)
			*error = "line " + std::to_string(lineno) + ": " + what;
		return false;
	};
	while (std::getline(in, line))
	{
		lineno ++;
		if (const size_t h = line.find('#'); h != std::string::npos)
			line.resize(h);
		std::istringstream ls(line);
		std::string key;
		if (!(ls >> key))
			continue;
		int t = -1, i = -1;
		if (!(ls >> t) || t < 0 || t >= TYPES)
			return fail("bad type");
		if (key == "rt" || key == "eq")
		{
			if (!(ls >> i) || i < 0 || i >= TIMES)
				return fail("bad time index");
			if (key == "rt")
			{
				if (t >= NET_TYPES)
					return fail("rt only for Room1..Gate");
				for (float &v : rt[t][i])
					if (!(ls >> v) || v <= 0.0f)
						return fail("bad rt");
			}
			else
				for (float &v : eq[t][i])
					if (!(ls >> v))
						return fail("bad eq");
		}
		else if (key == "gain")
		{
			if (!(ls >> gain[t]))
				return fail("bad gain");
		}
		else if (key == "size" || key == "pre")
		{
			if (t >= NET_TYPES)
				return fail("size/pre only for Room1..Gate");
			if (!(ls >> (key == "size" ? size[t] : pre_ms[t])))
				return fail("bad value");
		}
		else
			return fail("unknown key");
	}
	return true;
}

std::string ReverbTuning::format() const
{
	std::string s;
	char buf[256];
	for (int t = 0; t < TYPES; t ++)
	{
		std::snprintf(buf, sizeof(buf), "gain %d %.4f\n", t, gain[t]);
		s += buf;
		if (t < NET_TYPES)
		{
			std::snprintf(buf, sizeof(buf), "size %d %.3f\npre %d %.1f\n", t, size[t], t, pre_ms[t]);
			s += buf;
			for (int i = 0; i < TIMES; i ++)
			{
				std::snprintf(buf, sizeof(buf), "rt %d %d %.3f %.3f %.3f\n", t, i,
						rt[t][i][0], rt[t][i][1], rt[t][i][2]);
				s += buf;
			}
		}
		for (int i = 0; i < TIMES; i ++)
		{
			int n = std::snprintf(buf, sizeof(buf), "eq %d %d", t, i);
			for (float v : eq[t][i])
				n += std::snprintf(buf + n, sizeof(buf) - n, " %.2f", v);
			s += buf;
			s += '\n';
		}
	}
	return s;
}

const ReverbTuning &ReverbTuning::builtin()
{
	static const ReverbTuning t = [] {
		ReverbTuning r;
		r.parse(BUILTIN_TUNING);
		return r;
	}();
	return t;
}


// ---- U220Reverb -----------------------------------------------------------------------

U220Reverb::U220Reverb()
	: m_t(ReverbTuning::builtin())
{
	for (int i = 0; i < LINES; i ++)
		m_line[i].assign(size_t(BASE_LEN[i] * 1.6f) + 8, 0.0f);
	for (int i = 0; i < 4; i ++)
		m_diff[i].assign(DIFF_LEN[i], 0.0f);
	m_pre.assign(size_t(0.06 * FS) + 8, 0.0f);
	m_echo.assign(size_t(0.5 * FS) + 8, 0.0f);
	configure();
}

void U220Reverb::reset()
{
	for (auto &l : m_line) std::fill(l.begin(), l.end(), 0.0f);
	for (auto &d : m_diff) std::fill(d.begin(), d.end(), 0.0f);
	std::fill(m_pre.begin(), m_pre.end(), 0.0f);
	std::fill(m_echo.begin(), m_echo.end(), 0.0f);
	std::fill(std::begin(m_lsz), std::end(m_lsz), 0.0f);
	std::fill(std::begin(m_hsz), std::end(m_hsz), 0.0f);
	std::fill(&m_eqz[0][0][0], &m_eqz[0][0][0] + sizeof(m_eqz) / sizeof(float), 0.0f);
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
	int ti;
	float tf;
	time_weights(time, ti, tf);

	// Level: 31 is the reference, 0 is off.  The hardware's tail (Hall2, Time 8, reverb
	// energy alone) at Level 5..31, in dB below Level 31, interpolated linearly in dB;
	// about 0.8 dB a step, a little steeper at both ends.
	static constexpr float LEVEL_DB[7][2] = {
		{ 0, -40.0f }, { 5, -22.1f }, { 10, -16.2f }, { 15, -11.9f }, { 20, -8.2f },
		{ 25, -4.9f }, { 31, 0.0f },
	};
	const int level = std::clamp(m_p.reverb_level, 0, 31);
	float db = 0.0f;
	for (int k = 1; k < 7; k ++)
		if (level <= LEVEL_DB[k][0])
		{
			const float f = (level - LEVEL_DB[k - 1][0]) / (LEVEL_DB[k][0] - LEVEL_DB[k - 1][0]);
			db = LEVEL_DB[k - 1][1] + f * (LEVEL_DB[k][1] - LEVEL_DB[k - 1][1]);
			break;
		}
	m_gain = (m_p.reverb_on && level > 0 ? 1.0f : 0.0f) * std::pow(10.0f, db / 20.0f) * m_t.gain[type];

	// The output EQ, interpolated in dB between the Time points.
	m_eqFlat = true;
	for (int b = 0; b < EQ_BANDS; b ++)
	{
		const float g = m_t.eq[type][ti][b] + tf * (m_t.eq[type][ti + 1][b] - m_t.eq[type][ti][b]);
		if (std::fabs(g) > 0.01f)
			m_eqFlat = false;
		const double A = std::pow(10.0, g / 40.0);
		const double w = 2.0 * M_PI * ReverbTuning::EQ_HZ[b] / FS;
		const double al = std::sin(w) / (2.0 * EQ_Q), c = std::cos(w);
		const double a0 = 1.0 + al / A;
		m_eq[b][0] = float((1.0 + al * A) / a0);
		m_eq[b][1] = float(-2.0 * c / a0);
		m_eq[b][2] = float((1.0 - al * A) / a0);
		m_eq[b][3] = float(-2.0 * c / a0);
		m_eq[b][4] = float((1.0 - al / A) / a0);
	}

	if (type <= EffectParams::Gate)
	{
		float rt[ReverbTuning::DECAY_BANDS];
		for (int k = 0; k < ReverbTuning::DECAY_BANDS; k ++)
			rt[k] = m_t.rt[type][ti][k] + tf * (m_t.rt[type][ti + 1][k] - m_t.rt[type][ti][k]);
		for (int i = 0; i < LINES; i ++)
		{
			m_len[i] = std::min<unsigned>(unsigned(BASE_LEN[i] * m_t.size[type]), unsigned(m_line[i].size() - 1));
			auto loop_gain = [&](float t) { return std::pow(10.0, -3.0 * m_len[i] / (FS * t)); };
			const double gm = loop_gain(rt[1]);
			m_gm[i] = float(gm);
			low_shelf(loop_gain(rt[0]) / gm, LOW_SHELF_HZ, m_ls[i]);
			high_shelf(loop_gain(rt[2]) / gm, HIGH_SHELF_HZ, m_hs[i]);
		}
		m_prelen = std::clamp(unsigned(m_t.pre_ms[type] * 1e-3 * FS), 1u, unsigned(m_pre.size() - 1));
	}
	else
	{
		const double period = (10.0 + 12.9 * time) * 1e-3;
		m_echolen = std::clamp<unsigned>(unsigned(period * FS), 2u, unsigned(m_echo.size() - 1));
		// Measured per-repeat gains: Delay 0.26 at FB 15 and 0.75 at FB 31 (none at 0);
		// CrossDelay rings on even at FB 0 and less than Delay at 31.
		const float fb = float(std::clamp(m_p.delay_feedback, 0, 31)) / 31.0f;
		m_echofb = type == EffectParams::CrossDelay ? 0.25f + 0.38f * std::pow(fb, 2.2f)
				: 0.75f * std::pow(fb, 1.5f);
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
		// Absorption: the mid-band loop gain, shelved towards the low and high decay.
		o[i] = shelf(m_hs[i], m_hsz[i], shelf(m_ls[i], m_lsz[i], m_gm[i] * raw));
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
		// The input goes in with alternating signs, so no two lines start out alike.
		m_line[i][m_pos[i]] = h[i] * s + ((i & 1) ? -x : x);
		m_pos[i] = (m_pos[i] + 1) % n;
	}
	// Disjoint sets of lines for the two sides: the hardware's room and hall tails are
	// uncorrelated left to right (measured -0.03..+0.05).
	outR = o[1] + o[2] + o[4] + o[7];
	return o[0] + o[3] + o[5] + o[6];
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
				// Opened by an attack (a new peak), held ~450 ms whatever follows, closed in
				// ~15 ms: the hardware's gate is as short after a sustained note as after
				// a drum hit.
				const float a = std::fabs(in);
				if (a > 1e-4f && a >= m_env)
					m_hold = unsigned(0.45 * FS);
				else if (m_hold)
					m_hold --;
				m_env = std::max(a, m_env * 0.99995f);
				const float target = m_hold ? 1.0f : 0.0f;
				m_gate += (target - m_gate) * (target > m_gate ? 0.05f : 0.002f);
				// The gate is nearly mono on the hardware (L/R correlation 0.74): cross-mix.
				const float gl = m_gate * (l + 0.45f * r), gr = m_gate * (r + 0.45f * l);
				l = gl;
				r = gr;
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
		if (!m_eqFlat)
		{
			float *side[2] = { &l, &r };
			for (int c = 0; c < 2; c ++)
				for (int b = 0; b < EQ_BANDS; b ++)
				{
					// Direct form II transposed.
					const float *k = m_eq[b];
					float *z = m_eqz[c][b];
					const float x = *side[c];
					const float y = k[0] * x + z[0];
					z[0] = k[1] * x - k[3] * y + z[1];
					z[1] = k[2] * x - k[4] * y;
					*side[c] = y;
				}
		}
		outL[i] += m_gain * l;
		outR[i] += m_gain * r;
	}
}

} // namespace ostrich
