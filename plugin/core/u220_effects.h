// license:BSD-3-Clause
// copyright-holders:Helge Thorsten Kautz
/***************************************************************************************

    EffectsModule -- the U-220's effect processor (IC25, TC23SC140AF-007), as a part
    that can be swapped out

    The chip is not understood yet.  The plan (analysis/U220-EFFECTS.md) is to approximate
    chorus and reverb by ear first and to replace that with an emulation if the chip and
    its program are ever analysed -- so the rest of the core talks to this interface and
    to nothing more specific.

    What crosses it is exactly what crosses the board: the CPU's writes into the chip's
    window (bank 0x40), and the dry stereo mix in, wet stereo out.  An approximation is
    free to ignore the writes and read the effect settings from firmware RAM instead.

    Threading: every call comes from the audio thread, inside render().  No allocation,
    no locks.

***************************************************************************************/

#ifndef OSTRICH_U220_EFFECTS_H
#define OSTRICH_U220_EFFECTS_H

#pragma once

#include <cstdint>

namespace ostrich {

/// The effect settings of the current (temp) patch and the two global switches, decoded
/// by the core from firmware RAM once per block -- the same fields the firmware reads,
/// so a module needs no knowledge of the CPU.  Ranges as in the U-220's menus.
struct EffectParams
{
	enum ReverbType : int { Room1 = 0, Room2, Room3, Hall1, Hall2, Gate, Delay, CrossDelay };
	enum ChorusType : int { Chorus1 = 0, Chorus2, FBChorus, Flanger, ShortDelay };

	bool reverb_on = true;          ///< Edit/Setup/Efct
	bool chorus_on = true;
	int  reverb_type = Hall2;
	int  reverb_time = 4;           ///< 0..31
	int  reverb_level = 0;          ///< 0..31
	int  delay_feedback = 0;        ///< 0..31
	int  chorus_type = Chorus1;
	bool chorus_post_reverb = false;
	int  chorus_level = 0;          ///< 0..31
	int  chorus_delay = 0;          ///< 0..31
	int  chorus_rate = 0;           ///< 0..31
	int  chorus_depth = 0;          ///< 0..31
	int  chorus_feedback = 0;       ///< -31..+31

	bool operator==(const EffectParams &o) const = default;
};

class EffectsModule
{
public:
	virtual ~EffectsModule() = default;

	/// Called before every process(); cheap to call with unchanged values.
	virtual void setParams(const EffectParams &) { }

	/// A CPU write to window offset `offset` (0..0x7FF) while bank 0x40 is selected.
	/// `seconds` is emulated time, for modules that care when the write happened.
	virtual void write(uint16_t offset, uint8_t data, double seconds) = 0;

	/// One block at kCoreSampleRate: the reverb and chorus SEND buses in (IC25's mixer has
	/// already split the voices), the returns ADDED to out, which holds the dry mix.
	virtual void process(const float *revL, const float *revR, const float *choL,
			const float *choR, float *outL, float *outR, uint32_t nframes) = 0;

	virtual void reset() = 0;
};

/// No effect returns: the dry mix only.
class BypassEffects final : public EffectsModule
{
public:
	void write(uint16_t offset, uint8_t data, double) override { m_regs[offset & 0x7f] = data; }
	void process(const float *, const float *, const float *, const float *,
			float *, float *, uint32_t) override { }
	void reset() override { }

private:
	uint8_t m_regs[0x80] = {};
};

} // namespace ostrich

#endif // OSTRICH_U220_EFFECTS_H
