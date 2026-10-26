// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the DSP and the UI of the Ostrich plugin agree on: parameter indices, state keys,
// and the panel blob.

#ifndef OSTRICH_SHARED_HPP
#define OSTRICH_SHARED_HPP

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace ostrich {

enum Params : uint32_t
{
	kParamVolume = 0,           ///< dB, a post-gain like the unit's analogue pot

	// The twelve panel keys, as boolean parameters: the UI presses them, and a generic
	// host UI can too.  Order = kKeyBits below.  Never insert before existing ones: hosts
	// store automation by index.
	kParamKeyFirst,
	kParamKeyEdit = kParamKeyFirst,
	kParamKeyData,
	kParamKeyMark,
	kParamKeyJump,
	kParamKeyExit,
	kParamKeyEnter,
	kParamKeyPartLeft,
	kParamKeyPartRight,
	kParamKeyCursorLeft,
	kParamKeyCursorRight,
	kParamKeyValueDown,
	kParamKeyValueUp,
	kParamKeyEnd,

	/// The U-220's analogue output stage (a measured high shelf), on by default.  Off
	/// is the raw core, as MAME's driver produces it.
	kParamOutputStage = kParamKeyEnd,

	/// DIRECT OUT 1 and 2 on outputs of their own, where the host provides them (in VST3:
	/// the extra outputs switched on -- a plug in the jack).  Off, or without them, the
	/// direct sound joins MIX without effect, as on the unit with nothing plugged in.
	kParamDirectOuts,

	kParamCount
};

constexpr unsigned kNumKeys = kParamKeyEnd - kParamKeyFirst;

/// Each key parameter's bit in the U-220's key matrix (u220_core.h, Button).
constexpr int kKeyBits[kNumKeys] = { 0, 1, 8, 9, 2, 3, 4, 5, 10, 11, 12, 13 };

constexpr const char *kKeyNames[kNumKeys] = {
	"EDIT", "DATA", "MARK", "JUMP", "EXIT", "ENTER", "PART/INST <<", "PART/INST >>",
	"CURSOR <", "CURSOR >", "VALUE -", "VALUE +"
};
constexpr const char *kKeySymbols[kNumKeys] = {
	"key_edit", "key_data", "key_mark", "key_jump", "key_exit", "key_enter",
	"key_part_left", "key_part_right", "key_cursor_left", "key_cursor_right",
	"key_value_down", "key_value_up"
};

enum States : uint32_t
{
	kStatePanel = 0,            ///< DSP -> UI, the LCD
	kStateStatus,               ///< DSP -> UI, one line of text: ROMs, factory data
	kStateNvram,                ///< host session: IC10, hex
	kStateKey,                  ///< UI -> DSP, "<key> <0|1>": a panel key, see below
	kStateUiSize,               ///< host session: the panel's size, "<w> <h>" unscaled
	kStateCard1,                ///< host session and UI -> DSP: the card file in slot 1, "" = empty
	kStateCard2,                ///< the same for slot 2
	kStateCardInfo,             ///< DSP -> UI: what each slot shows, see cardInfo below
	kStateSysex,                ///< UI -> DSP, "<n> save|load <path>": a .syx file
	kStateCount
};

constexpr unsigned kCardSlots = 2;
constexpr const char *kCardStateKeys[kCardSlots] = { "card1", "card2" };

/// "cardinfo" is one field per slot, separated by '\x1e'; a field is empty (no card),
/// "ok\x1f<title>\x1f<subtitle>" or "err\x1f<message>\x1f<file name>".
constexpr char kCardInfoSlotSep = '\x1e', kCardInfoFieldSep = '\x1f';

/// What Roland called the SN-U110 cards, by the ID byte in their header (0x20).
inline const char *cardTitle(unsigned id)
{
	static const char *const names[] = {
		nullptr, "Pipe Organ & Harpsichord", "Latin & F.X. Percussions", "Ethnic",
		"Electric Grand & Clavi", "Orchestral Strings", "Orchestral Winds", "Electric Guitar",
		"Synthesizer", "Guitar & Keyboards", "Rock Drums", "Sound Effects", "Sax & Trombone",
		"Super Strings", "Super Ac Guitar", "Super Brass"
	};
	return id < sizeof(names) / sizeof(names[0]) ? names[id] : nullptr;
}

/// The LCD as the UI draws it.  Sent as hex, only when it changed.
struct PanelBlob
{
	uint8_t lcd[48];            ///< 24 x 2 character codes
	uint8_t cgram[64];          ///< the 8 custom glyphs
	uint8_t cursor_pos;         ///< 0..47 into lcd[], 0xFF = none
	uint8_t cursor_flags;       ///< bit 1 underline, bit 0 blinking block
	uint8_t flags;              ///< bit 0: machine running (ROMs found)
};

inline void encodeHex(const uint8_t *p, size_t n, char *out)
{
	static const char *const d = "0123456789abcdef";
	for (size_t i = 0; i < n; i ++)
	{
		out[i * 2] = d[p[i] >> 4];
		out[i * 2 + 1] = d[p[i] & 15];
	}
	out[n * 2] = 0;
}

inline int hexNibble(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

inline bool decodeHex(const char *s, uint8_t *out, size_t n)
{
	if (std::strlen(s) != n * 2)
		return false;
	for (size_t i = 0; i < n; i ++)
	{
		const int hi = hexNibble(s[i * 2]), lo = hexNibble(s[i * 2 + 1]);
		if (hi < 0 || lo < 0)
			return false;
		out[i] = uint8_t((hi << 4) | lo);
	}
	return true;
}

// Diagnostics while the panel freeze is hunted (ROADMAP.md, Phase 4): one line at a time
// appended to %PROGRAMDATA%\Ostrich\<name>.log (ProgramData: Cubase's MSIX packaging hides
// what a plugin writes to %LOCALAPPDATA%), restarted when it passes 2 MB.  Called once a
// second from a non-audio thread only.
inline void debugLog(const char *name, const char *line)
{
	const char *base = std::getenv("PROGRAMDATA");
	if (base == nullptr)
		return;
	char path[512];
	std::snprintf(path, sizeof(path), "%s/Ostrich/%s.log", base, name);
	FILE *f = std::fopen(path, "a");
	if (f == nullptr)
		return;
	if (std::ftell(f) > 2 * 1024 * 1024)
	{
		std::fclose(f);
		f = std::fopen(path, "w");
		if (f == nullptr)
			return;
	}
	const std::time_t now = std::time(nullptr);
	char stamp[32];
	std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
	std::fprintf(f, "%s %s\n", stamp, line);
	std::fclose(f);
}

// The LCD's two rows as printable text, for the logs.
inline void lcdText(const uint8_t *lcd, char *out /* 52 */)
{
	int k = 0;
	out[k ++] = '[';
	for (int i = 0; i < 48; i ++)
	{
		if (i == 24) { out[k ++] = ']'; out[k ++] = '['; }
		const uint8_t c = lcd[i];
		out[k ++] = (c >= 0x20 && c < 0x7f) ? char(c) : '~';
	}
	out[k ++] = ']';
	out[k] = 0;
}

} // namespace ostrich

#endif // OSTRICH_SHARED_HPP
