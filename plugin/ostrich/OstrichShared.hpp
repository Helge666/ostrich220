// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the DSP and the UI of the Ostrich plugin agree on: parameter indices, state keys,
// and the panel blob.

#ifndef OSTRICH_SHARED_HPP
#define OSTRICH_SHARED_HPP

#include <cstdint>
#include <cstring>

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

	kParamCount = kParamKeyEnd
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
	kStateCount
};

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

} // namespace ostrich

#endif // OSTRICH_SHARED_HPP
