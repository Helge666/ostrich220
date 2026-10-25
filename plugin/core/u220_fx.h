// license:BSD-3-Clause
// copyright-holders:Helge Thorsten Kautz
/***************************************************************************************

    U220Fx -- the whole effect block: U220Reverb and U220Chorus behind one
    EffectsModule, as the core and the plugin use it

    The routing of "Chorus Out" (Edit > Patch > Effect), from the owner's manual (p. 54,
    and the Output Assign table on p. 61):

      Pre Rev   chorus, then reverb: "Reverb will be added to the sound that has been
                processed through the chorus" -- the reverb takes the chorus send and the
                chorus's copies
      Post Rev  "Reverb will not be added to the sound that has been processed through
                the chorus": parts on Cho get the chorus only

    And measured (make_chorus_probe.py --prepost, 25.10.2026): at Pre Rev the unit puts
    out the right side of everything on Cho -- dry and chorus alike -- one sample (31 us)
    late, which reads as a much wider top (4-15 kHz) and nowhere else; at Post Rev, or
    with the chorus off, not.  The dry of a part on Cho is the chorus send itself (equal
    level, measured), so that share of the dry mix is moved by a sample here, and the
    chorus's right copy likewise.

***************************************************************************************/

#ifndef OSTRICH_U220_FX_H
#define OSTRICH_U220_FX_H

#pragma once

#include "u220_chorus.h"
#include "u220_reverb.h"

#include <algorithm>

namespace ostrich {

class U220Fx final : public EffectsModule
{
public:
	U220Reverb &reverb() { return m_rev; }
	/// Leaves the reverb out whatever the patch says (A/B renders against a unit whose
	/// Setup has Reverb Off).
	void setReverbEnabled(bool on) { m_revOn = on; }

	void setParams(const EffectParams &p) override
	{
		m_preRev = !p.chorus_post_reverb;
		m_shiftRight = m_preRev && p.chorus_on;
		m_rev.setParams(p);
		m_cho.setParams(p);
	}
	void write(uint16_t, uint8_t, double) override { }
	void process(const float *revL, const float *revR, const float *choL, const float *choR,
			float *outL, float *outR, uint32_t nframes) override
	{
		static const float silence[CHUNK] = {};
		for (uint32_t done = 0; done < nframes; )
		{
			const uint32_t n = std::min<uint32_t>(CHUNK, nframes - done);
			std::fill(m_wetL, m_wetL + n, 0.0f);
			std::fill(m_wetR, m_wetR + n, 0.0f);
			m_cho.process(choL + done, choR + done, m_wetL, m_wetR, n);
			for (uint32_t i = 0; i < n; i ++)
			{
				float wr = m_wetR[i];
				if (m_shiftRight)
				{
					// The Cho parts' right side a sample late: their dry share of the mix
					// swapped for last sample's, the chorus copy likewise.
					outR[done + i] += m_lastChoR - choR[done + i];
					m_lastChoR = choR[done + i];
					const float w = wr;
					wr = m_lastWetR;
					m_lastWetR = w;
				}
				outL[done + i] += m_wetL[i];
				outR[done + i] += wr;
			}
			if (m_revOn)
			{
				if (m_preRev)
				{
					// The chorused sound into the reverb: the send plus the copies.
					for (uint32_t i = 0; i < n; i ++)
					{
						m_wetL[i] += choL[done + i];
						m_wetR[i] += choR[done + i];
					}
					m_rev.process(revL + done, revR + done, m_wetL, m_wetR, outL + done, outR + done, n);
				}
				else
					m_rev.process(revL + done, revR + done, silence, silence, outL + done, outR + done, n);
			}
			done += n;
		}
	}
	void reset() override { m_rev.reset(); m_cho.reset(); m_lastChoR = m_lastWetR = 0.0f; }

private:
	static constexpr uint32_t CHUNK = 256;
	U220Reverb m_rev;
	U220Chorus m_cho;
	float m_wetL[CHUNK] = {}, m_wetR[CHUNK] = {};
	bool m_preRev = true;
	bool m_shiftRight = false;
	float m_lastChoR = 0.0f, m_lastWetR = 0.0f;
	bool m_revOn = true;
};

} // namespace ostrich

#endif // OSTRICH_U220_FX_H
