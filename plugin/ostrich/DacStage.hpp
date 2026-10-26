// SPDX-License-Identifier: GPL-3.0-or-later
//
// The U-220's 32 kHz DAC as heard behind it: no steep reconstruction filter, so every
// component at f under 16 kHz comes out a second time at 32 kHz - f.  Measured on Helge's
// unit (dry held sawtooth, chorus probe, 25.10.2026): the image is 3 dB under its source
// just above 16 kHz, 16 dB at 19.5 kHz, 38 dB at 23.5 kHz -- the bright "scratch" that
// the unit has and a band-limited resampler takes away.
//
// Core (32 kHz) -> 64 kHz, where the image is the upsampled signal times (-1)^k shaped
// by the measured curve (FIR), added -> host rate.  With the images off it is a plain
// two-stage resampler that passes up to 15.5 kHz, as the unit does.

#ifndef OSTRICH_DAC_STAGE_HPP
#define OSTRICH_DAC_STAGE_HPP

#include "Resampler.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ostrich {

class DacStage
{
public:
	static constexpr double kMid = 64000.0;

	void setup(double coreRate, double hostRate, uint32_t maxHostFrames)
	{
		m_up.setup(coreRate, kMid, 0.97);
		m_down.setup(kMid, hostRate);
		designImageFilter();
		m_mid.assign(m_down.inputsFor(maxHostFrames) + 16, 0.0f);
		reset();
	}

	void reset()
	{
		m_up.reset();
		m_down.reset();
		std::fill(m_hist.begin(), m_hist.end(), 0.0f);
		m_histPos = 0;
		m_sign = 1.0f;
	}

	void setImages(bool on) { m_images = on; }

	uint32_t inputsFor(uint32_t hostFrames) const { return m_up.inputsFor(m_down.inputsFor(hostFrames)); }

	/// In host frames: both resamplers (the image filter adds nothing to the direct path).
	uint32_t latency() const
	{ return m_down.latency() + uint32_t(double(m_up.latency()) * m_down.L() / m_down.M()); }

	void process(const float *in, uint32_t inFrames, float *out, uint32_t outFrames)
	{
		const uint32_t n = m_down.inputsFor(outFrames);
		if (n > m_mid.size())
			m_mid.resize(n);                // only on a block larger than announced
		m_up.process(in, inFrames, m_mid.data(), n);
		for (uint32_t i = 0; i < n; i ++)
		{
			// The image: the spectrum mirrored about 16 kHz, through the measured curve.
			const float v = m_mid[i];
			m_hist[m_histPos] = v * m_sign;
			m_sign = -m_sign;
			float acc = 0.0f;
			unsigned idx = m_histPos;
			for (size_t k = 0; k < m_fir.size(); k ++)
			{
				acc += m_fir[k] * m_hist[idx];
				idx = (idx + unsigned(m_hist.size()) - 1) % unsigned(m_hist.size());
			}
			m_histPos = (m_histPos + 1) % unsigned(m_hist.size());
			if (m_images)
				m_mid[i] = v + acc;
		}
		m_down.process(m_mid.data(), n, out, outFrames);
	}

private:
	// Linear-phase FIR at 64 kHz, image level relative to its source in dB by frequency
	// (the image's own frequency, 16-32 kHz); the direct path is already band-limited by
	// the upsampler, so only this range matters.
	void designImageFilter()
	{
		static const double T[][2] = {
			{ 16000, -2.0 }, { 16300, -2.9 }, { 17100, -4.3 }, { 17500, -6.5 }, { 18300, -9.0 },
			{ 19100, -14.0 }, { 19500, -16.1 }, { 19900, -19.9 }, { 20300, -22.5 }, { 20700, -23.7 },
			{ 21100, -27.0 }, { 21500, -29.0 }, { 21900, -30.8 }, { 22300, -32.3 }, { 22700, -33.5 },
			{ 23100, -35.2 }, { 23500, -37.9 }, { 24500, -45.0 }, { 32000, -60.0 },
		};
		const int N = 63, G = 2048;
		std::vector<double> mag(G + 1);
		for (int k = 0; k <= G; k ++)
		{
			const double f = k * (kMid / 2) / G;
			double db = T[0][1];
			for (size_t j = 1; j < sizeof(T) / sizeof(T[0]); j ++)
				if (f <= T[j][0])
				{
					const double a = (f - T[j - 1][0]) / (T[j][0] - T[j - 1][0]);
					db = f < T[0][0] ? T[0][1] : T[j - 1][1] + std::clamp(a, 0.0, 1.0) * (T[j][1] - T[j - 1][1]);
					break;
				}
			mag[k] = std::pow(10.0, db / 20.0);
		}
		m_fir.assign(N, 0.0f);
		for (int n = 0; n < N; n ++)
		{
			const double t = n - (N - 1) / 2.0;
			double h = 0.0;
			for (int k = 0; k <= G; k ++)
				h += (k == 0 || k == G ? 0.5 : 1.0) * mag[k] * std::cos(M_PI * k * t / G);
			h /= G;
			const double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * (n + 0.5) / N);
			m_fir[n] = float(h * w);
		}
		m_hist.assign(N, 0.0f);
	}

	voltaire::Resampler m_up, m_down;
	std::vector<float> m_mid, m_fir, m_hist;
	unsigned m_histPos = 0;
	float m_sign = 1.0f;
	bool m_images = true;
};

} // namespace ostrich

#endif // OSTRICH_DAC_STAGE_HPP
