// license:BSD-3-Clause
// copyright-holders:Helge Thorsten Kautz
/***************************************************************************************

    U220Fx -- the whole effect block: U220Reverb and U220Chorus behind one
    EffectsModule, as the core and the plugin use it

    The chorus's copies go straight to the output.  What "Chorus Out = Pre Rev" sends to
    the reverb is not measured yet; the reverb keeps taking the chorus send itself then
    (U220Reverb::process).

***************************************************************************************/

#ifndef OSTRICH_U220_FX_H
#define OSTRICH_U220_FX_H

#pragma once

#include "u220_chorus.h"
#include "u220_reverb.h"

namespace ostrich {

class U220Fx final : public EffectsModule
{
public:
	U220Reverb &reverb() { return m_rev; }

	void setParams(const EffectParams &p) override { m_rev.setParams(p); m_cho.setParams(p); }
	void write(uint16_t, uint8_t, double) override { }
	void process(const float *revL, const float *revR, const float *choL, const float *choR,
			float *outL, float *outR, uint32_t nframes) override
	{
		m_rev.process(revL, revR, choL, choR, outL, outR, nframes);
		m_cho.process(choL, choR, outL, outR, nframes);
	}
	void reset() override { m_rev.reset(); m_cho.reset(); }

private:
	U220Reverb m_rev;
	U220Chorus m_cho;
};

} // namespace ostrich

#endif // OSTRICH_U220_FX_H
