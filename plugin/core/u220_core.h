// license:BSD-3-Clause
// copyright-holders:Helge Thorsten Kautz
/***************************************************************************************

    U220Core -- the Roland U-220 emulation, with no MAME framework around it

    The U-220 counterpart of U110Core, and built the same way: MAME's device sources
    (the P8098 CPU, the MB87419/MB87420 tone generator) compiled against plugin/compat,
    with the machine wiring of mame/src/mame/roland/roland_u220.cpp written a second time
    here.  u110_core.h explains why the framework is replaced and the devices are not;
    all of that applies unchanged.

    LICENCE, THREADING and SAMPLE RATE as in u110_core.h: BSD-3-Clause, render() is
    real-time safe, the load* methods are not, and the core produces 32000 Hz only.

    WHAT IS NOT HERE YET.  The effect processor (IC25) is not emulated.  The firmware's
    writes to it are passed to an EffectsModule (u220_effects.h), which for now is a
    bypass; Phase 3 replaces the module without touching this class.  The MIX/DIRECT
    output split and the MIDI lamp are not modelled either.

***************************************************************************************/

#ifndef OSTRICH_U220_CORE_H
#define OSTRICH_U220_CORE_H

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace ostrich {

class EffectsModule;

/// The chip's native rate: 32.768 MHz / 1024.  Not configurable.
inline constexpr uint32_t kCoreSampleRate = 32000;

/// The panel switches, by their bit in the key matrix (window bank 0x30: 0x080E holds
/// bits 0-7, 0x080D bits 8-13).  Bits 6 and 7 exist in the matrix but have no key.
enum Button : int
{
	kButtonEdit = 0,
	kButtonData = 1,
	kButtonExit = 2,
	kButtonEnter = 3,
	kButtonPartLeft = 4,        ///< PART/INST <<
	kButtonPartRight = 5,       ///< PART/INST >>
	kButtonMark = 8,
	kButtonJump = 9,
	kButtonCursorLeft = 10,
	kButtonCursorRight = 11,
	kButtonValueDown = 12,
	kButtonValueUp = 13,
	kButtonCount = 14
};

/// Program EPROM (HN27C101, 128 KB) and the six wave ROMs A-F (512 KB each, as dumped).
inline constexpr size_t   kProgramRomBytes = 0x20000;
inline constexpr size_t   kWaveRomBytes    = 0x80000;
inline constexpr unsigned kNumWaveRoms     = 6;
/// IC10, the battery-backed SRAM.  This is what persists, as on the hardware.
inline constexpr size_t   kNvramBytes      = 0x8000;

inline constexpr unsigned kLcdColumns = 24;
inline constexpr unsigned kLcdRows    = 2;

/***************************************************************************************
    What the UI needs to draw the panel.  Fixed layout, versioned, fire and forget --
    see PanelState in u110_core.h for why it must never be in the emulation's path.
***************************************************************************************/
struct PanelState
{
	static constexpr uint16_t kVersion = 1;

	uint16_t version;

	/// Character codes, row 0 then row 1.  0x00-0x07 are the custom glyphs in cgram.
	uint8_t  lcd[kLcdColumns * kLcdRows];
	uint8_t  cgram_dirty;
	uint8_t  cgram[64];         ///< 8 glyphs x 8 rows, low 5 bits = the dots across
	uint8_t  leds;              ///< reserved: the MIDI lamp is not wired up yet
	uint32_t voice_active;      ///< one bit per voice; voice 0 is the ROM read port
	float    peak[2];
};

enum class LoadResult : int
{
	Ok = 0,
	WrongSize,
	BadImage,
	NoSuchSlot,
};

class U220Core
{
public:
	U220Core();
	~U220Core();

	U220Core(const U220Core &) = delete;
	U220Core &operator=(const U220Core &) = delete;

	// --- images.  Worker thread, between render() calls.  These allocate. ------------

	/// The firmware EPROM, 128 KB (v1.00, v1.01 or v1.02).  Required before anything runs.
	LoadResult loadProgramRom(const uint8_t *data, size_t len);

	/// Wave ROM `index` 0..5 = A..F (IC19-IC24), as dumped; descrambled and placed here.
	LoadResult loadWaveRom(unsigned index, const uint8_t *data, size_t len);

	/// Hard reset.  The firmware boots from whatever the NVRAM holds.
	void reset();

	/// Run emulated time as fast as possible, discarding the audio.
	void runUntilIdle(uint32_t timeoutMs = 5000);

	/// Where the firmware's effect-processor writes go and what processes the mix.
	/// nullptr (the default) is a bypass.  Not owned.  Call between render() calls.
	void setEffects(EffectsModule *fx);

	/// The analogue output stage.  On by default; off gives exactly what MAME's driver
	/// produces, which is what the null test compares.
	void setOutputFilter(bool on);

	// --- the audio thread ------------------------------------------------------------

	/// Queue MIDI bytes; sampleOffset is where the first byte starts arriving within the
	/// next render() call.  Bytes are then delivered at the wire's 31250 baud spacing.
	void midiIn(const uint8_t *bytes, size_t n, uint32_t sampleOffset);

	/// Deliver bytes at an exact emulated time, in seconds since reset (offline / tests).
	void midiInAtTime(const uint8_t *bytes, size_t n, double seconds);

	/// Bytes the firmware sent during the last render(), with sample offsets.
	size_t midiOut(uint8_t *buf, size_t cap, uint32_t *offsets);

	/// The stereo output at kCoreSampleRate.  Deterministic and allocation free.
	void renderStereo(float *left, float *right, uint32_t nframes);

	/// Send edges, and hold a press for ~100 ms of emulated time: the key scan runs on
	/// the firmware's 10 ms tick and debounces.
	void setButton(Button sw, bool down);

	// --- observation -----------------------------------------------------------------

	void snapshot(PanelState &out) const;

	/// The CPU's view of memory (the window at 0x0800 shows whatever bank is selected).
	uint8_t readMem(uint16_t addr) const;
	void writeMem(uint16_t addr, uint8_t value);

	/// Emulated seconds since the core was built: the clock midiInAtTime() counts in.
	double emulatedSeconds() const;

	/// IC10 by its own address, 0..0x7FFF, independent of the window.
	uint8_t readNvram(uint32_t addr) const;

	/// The 24 x 2 text, '.' for anything that is not printable ASCII.  For tests and logs.
	void lcdText(char *row0, char *row1) const;     // each kLcdColumns + 1 bytes

	// --- persistence: the NVRAM and nothing else, as on the hardware -----------------

	size_t saveState(uint8_t *buf, size_t cap) const;
	/// Restores the NVRAM and reboots into it.
	bool   loadState(const uint8_t *buf, size_t n);

	/// Replace IC10's contents (kNvramBytes) without rebooting; reset() afterwards.  For
	/// tools and tests that must start the clock at zero with a given memory.
	bool   setNvram(const uint8_t *data, size_t len);

	/// True if the NVRAM is blank, i.e. the firmware will ask for "Memory Initialize".
	bool   nvramBlank() const;

private:
	struct Impl;
	std::unique_ptr<Impl> m_impl;
};

} // namespace ostrich

#endif // OSTRICH_U220_CORE_H
