// license:BSD-3-Clause
// copyright-holders:Helge Thorsten Kautz
/***************************************************************************************

    EffectsModule -- the U-220's effect processor (IC25, TC23SC140AF-007), as a part
    that can be swapped out

    The chip is not understood yet.  The plan (ROADMAP.md, Phase 3) is to approximate
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

class EffectsModule
{
public:
	virtual ~EffectsModule() = default;

	/// A CPU write to window offset `offset` (0..0x7FF) while bank 0x40 is selected.
	/// `seconds` is emulated time, for modules that care when the write happened.
	virtual void write(uint16_t offset, uint8_t data, double seconds) = 0;

	/// What a CPU read from that window returns.
	virtual uint8_t read(uint16_t offset) = 0;

	/// Process one block in place, at kCoreSampleRate.
	virtual void process(float *left, float *right, uint32_t nframes) = 0;

	virtual void reset() = 0;
};

/// Passes the audio through untouched and remembers the register writes, which is what
/// the MAME driver does today.
class BypassEffects final : public EffectsModule
{
public:
	void write(uint16_t offset, uint8_t data, double) override { m_regs[offset & 0x7f] = data; }
	uint8_t read(uint16_t offset) override { return m_regs[offset & 0x7f]; }
	void process(float *, float *, uint32_t) override { }
	void reset() override { }

private:
	uint8_t m_regs[0x80] = {};
};

} // namespace ostrich

#endif // OSTRICH_U220_EFFECTS_H
