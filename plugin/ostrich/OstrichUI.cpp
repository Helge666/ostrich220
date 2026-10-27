// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ostrich 220 -- the panel: the unit's front (the 24 x 2 LCD, the twelve keys where the
// U-220 has them, the volume knob, the two PCM card slots) and under it a bar for what the
// unit does not have (status line, HW-EQ, SysEx files).  The layout follows a photo of the
// unit.

#include "DistrhoUI.hpp"
#include "OstrichShared.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

START_NAMESPACE_DISTRHO

namespace {

using namespace ostrich;

// Design units: the unit's front is 1350 x 250, and under it a bar for what the unit does
// not have (SysEx files, the HW-EQ switch, the status line) -- kept off the front so that
// it stays the U-220's.  The total keeps DISTRHO_UI_DEFAULT_WIDTH/HEIGHT's ratio.
constexpr float kW = 1350.0f, kPanelH = 250.0f;
constexpr float kH = kW * DISTRHO_UI_DEFAULT_HEIGHT / DISTRHO_UI_DEFAULT_WIDTH;
constexpr float kBarY = kPanelH, kBarH = kH - kPanelH;

struct Rect { float x, y, w, h; bool contains(float px, float py) const
	{ return px >= x && px < x + w && py >= y && py < y + h; } };

// The keys, in parameter order (OstrichShared.hpp).  Block 1 is 2 x 2, block 2 is 2 x 4,
// top row first -- as on the unit.
constexpr float kKeyW = 62.0f, kKeyH = 28.0f;
constexpr float kB1x = 668.0f, kB2x = 840.0f, kRow0 = 96.0f, kRow1 = 168.0f, kGap = 72.0f;
const Rect kKeyRects[kNumKeys] = {
	{ kB1x,            kRow0, kKeyW, kKeyH },   // EDIT
	{ kB1x + kGap,     kRow0, kKeyW, kKeyH },   // DATA
	{ kB1x,            kRow1, kKeyW, kKeyH },   // MARK
	{ kB1x + kGap,     kRow1, kKeyW, kKeyH },   // JUMP
	{ kB2x,            kRow0, kKeyW, kKeyH },   // EXIT
	{ kB2x + kGap,     kRow0, kKeyW, kKeyH },   // ENTER
	{ kB2x + kGap * 2, kRow0, kKeyW, kKeyH },   // PART/INST <<
	{ kB2x + kGap * 3, kRow0, kKeyW, kKeyH },   // PART/INST >>
	{ kB2x,            kRow1, kKeyW, kKeyH },   // CURSOR <
	{ kB2x + kGap,     kRow1, kKeyW, kKeyH },   // CURSOR >
	{ kB2x + kGap * 2, kRow1, kKeyW, kKeyH },   // VALUE -
	{ kB2x + kGap * 3, kRow1, kKeyW, kKeyH },   // VALUE +
};
const char *const kKeyLabels[kNumKeys] = {
	"EDIT", "DATA", "MARK", "JUMP", "EXIT", "ENTER", "\xe2\x97\x80\xe2\x97\x80", "\xe2\x96\xb6\xe2\x96\xb6",
	"\xe2\x97\x80", "\xe2\x96\xb6", "\xe2\x96\xbc", "\xe2\x96\xb2"
};

const Rect kLcd    { 176.0f, 76.0f, 448.0f, 118.0f };     // the glass
constexpr float kLcdPadX = 18.0f, kLcdPadY = 8.0f;          // glass around the dots
const Rect kKnob   { 62.0f, 96.0f, 64.0f, 64.0f };

// The two PCM card slots, right of the keys and level with their rows; a click on a slot
// opens a file dialog, the eject button at its right end takes the card out.
constexpr float kSlotX = 1146.0f, kSlotW = 182.0f, kSlotH = 40.0f;
const Rect kSlots[kCardSlots] = {
	{ kSlotX, kRow0 + kKeyH / 2 - kSlotH / 2, kSlotW, kSlotH },
	{ kSlotX, kRow1 + kKeyH / 2 - kSlotH / 2, kSlotW, kSlotH },
};
inline Rect ejectRect(const Rect &slot) { return { slot.x + slot.w - 30, slot.y + 8, 24, slot.h - 16 }; }

// The bar, from the right: SysEx files (a bulk dump of the whole machine to a .syx, or a
// .syx in), then the HW-EQ switch; the status line takes the rest.
constexpr float kBarBtnH = 18.0f, kBarBtnY = kBarY + (kBarH - kBarBtnH) / 2;
const Rect kSyxSave { kW - 16.0f - 87.0f, kBarBtnY, 87.0f, kBarBtnH };
const Rect kSyxLoad { kSyxSave.x - 8.0f - 87.0f, kBarBtnY, 87.0f, kBarBtnH };
const Rect kHwEq    { kSyxLoad.x - 28.0f - 68.0f, kBarBtnY, 68.0f, kBarBtnH };

// While ROM images are missing they take the place of the display and the keys: what is
// needed, a chip per image (and the optional factory dump), and a button that asks for a
// file -- any one of them; the rest of its folder is found too.
const Rect kRomBox    { 160.0f, 62.0f, 970.0f, 176.0f };
const Rect kRomChoose { 180.0f, 196.0f, 150.0f, 26.0f };
constexpr unsigned kRomCells = kRomSlots + 1;           // + the factory dump
inline Rect romCell(unsigned i)
{
	// Firmware first, then A..F, then the factory dump: four to a row.
	const unsigned pos = i == kRomSlots - 1 ? 0 : i < kRomSlots - 1 ? i + 1 : kRomSlots;
	return { 640.0f + float(pos % 4) * 120.0f, 78.0f + float(pos / 4) * 76.0f, 112.0f, 66.0f };
}

} // anonymous namespace


class OstrichUI : public UI
{
public:
	OstrichUI()
		// DPF scales for the desktop itself and keeps the aspect ratio; everything below
		// draws in design units scaled to whatever size the window has.
		: UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT, true)
	{
		loadSharedResources();
		std::memset(&m_panel, 0, sizeof(m_panel));
	}

protected:
	void parameterChanged(uint32_t index, float value) override
	{
		if (index == kParamVolume)
			m_volumeDb = value;
		else if (index == kParamOutputStage)
			m_hwEq = value > 0.5f;
		else if (index >= kParamKeyFirst && index < kParamKeyEnd)
			m_keyLit[index - kParamKeyFirst] = value > 0.5f;
		repaint();
	}

	void stateChanged(const char *key, const char *value) override
	{
		if (std::strcmp(key, "panel") == 0)
		{
			PanelBlob b;
			if (decodeHex(value, reinterpret_cast<uint8_t *>(&b), sizeof(b)))
			{
				m_panel = b;
				repaint();
			}
		}
		else if (std::strcmp(key, "uisize") == 0)
			restoreSize(value);
		else if (std::strcmp(key, "status") == 0)
		{
			m_status = value;
			m_romChecking = false;          // the answer to a picked ROM file, if one was asked
			repaint();
		}
		else if (std::strcmp(key, "cardinfo") == 0)
		{
			setCardInfo(value);
			repaint();
		}
		else if (std::strcmp(key, "romset") == 0)
		{
			setRomSet(value);
			repaint();
		}
		else
			for (unsigned s = 0; s < kCardSlots; s ++)
				if (std::strcmp(key, kCardStateKeys[s]) == 0)
					m_cardPath[s] = value;
	}

	void uiFileBrowserSelected(const char *filename) override
	{
		const int what = m_browseSlot;
		m_browseSlot = -1;
		if (what < 0 || filename == nullptr || filename[0] == '\0')
			return;                         // cancelled
		if (what == kBrowseRom)
		{
			m_romDir = dirOf(filename);
			m_romChecking = true;
			m_romAskedAt = std::chrono::steady_clock::now();
			const std::string msg = std::to_string(++ m_syxSeq) + " " + filename;
			setState("romfile", msg.c_str());
			repaint();
			return;
		}
		if (what == kBrowseSyxLoad || what == kBrowseSyxSave)
		{
			std::string path = filename;
			m_syxDir = dirOf(path);
			rememberFolder("sysex", m_syxDir);
			if (what == kBrowseSyxSave)
			{
				const size_t dot = path.find_last_of('.'), cut = path.find_last_of("/\\");
				if (dot == std::string::npos || (cut != std::string::npos && dot < cut))
					path += ".syx";
			}
			const std::string msg = std::to_string(++ m_syxSeq) + (what == kBrowseSyxSave ? " save " : " load ") + path;
			setState("sysex", msg.c_str());
			return;
		}
		const unsigned s = unsigned(what);
		rememberFolder("card", dirOf(filename));
		m_cardPath[s] = filename;
		setState(kCardStateKeys[s], filename);
	}

	void onNanoDisplay() override
	{
		// Nothing may escape a draw: under Windows it unwinds through the window
		// procedure, which swallows it and leaves pugl believing it is still drawing --
		// the panel then never redraws (patches/0008).
		try
		{
			drawPanel();
		}
		catch (...)
		{
		}
	}

	void drawPanel()
	{
		const float s = getWidth() / kW;
		scale(s, s);

		// The body: black front, as the unit.
		beginPath();
		rect(0, 0, kW, kPanelH);
		fillColor(Color(24, 24, 26));
		fill();
		beginPath();
		rect(0, 0, kW, 4);
		fillColor(Color(60, 60, 64));
		fill();

		fontFace(NANOVG_DEJAVU_SANS_TTF);
		fillColor(Color(235, 235, 235));
		textAlign(ALIGN_LEFT | ALIGN_BASELINE);
		fontSize(26);
		text(24, 44, "OSTRICH 220", nullptr);
		fontSize(11);
		fillColor(Color(170, 170, 170));
		text(196, 44, "RS-PCM SOUND MODULE  \xc2\xb7  U-220 EMULATION", nullptr);

		drawMidiLamp();
		drawKnob();
		if (romSetup())
			drawRomSetup();
		else
		{
			drawLcd();
			drawKeys();
		}
		drawSlots();
		drawBar();
	}

	// What the unit does not have, under its front.
	void drawBar()
	{
		beginPath();
		rect(0, kBarY, kW, kBarH);
		fillColor(Color(14, 14, 16));
		fill();
		beginPath();
		rect(0, kBarY, kW, 1);
		fillColor(Color(52, 52, 56));
		fill();

		drawHwEq();
		drawSyxButtons();

		// The status line, one line, cut where the controls begin.
		const float w = kHwEq.x - 24 - 16;
		fontSize(11);
		textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
		fillColor(m_panel.flags & 1 ? Color(140, 140, 140) : Color(240, 170, 60));
		text(16, kBarY + kBarH / 2 + 1, fit(m_status, w, 11).c_str(), nullptr);
	}

	bool onMouse(const MouseEvent &ev) override
	{
		if (ev.button != 1)
			return false;
		const float s = mouseScale();
		const float x = float(ev.pos.getX()) / s, y = float(ev.pos.getY()) / s;

		if (ev.press)
		{
			getWindow().focus();                // for the computer keyboard (onKeyboard)
			if (romSetup())
			{
				bool hit = kRomChoose.contains(x, y);
				for (unsigned i = 0; i < kRomCells; i ++)
					hit = hit || romCell(i).contains(x, y);
				if (hit)
				{
					browseRom();
					return true;
				}
			}
			else if (kLcd.contains(x, y))
			{
				// A field in the display: the plugin presses CURSOR (PART/INST on the main
				// page) until the firmware's selection stands on it (tickGoto).
				const float cw = (kLcd.w - 2 * kLcdPadX) / 24.0f, ch = (kLcd.h - 2 * kLcdPadY) / 2.0f;
				const int col = int(std::floor((x - kLcd.x - kLcdPadX) / cw));
				const int row = int(std::floor((y - kLcd.y - kLcdPadY) / ch));
				if (col >= 0 && col < 24 && row >= 0 && row < 2)
				{
					// A second click on the same cell within 500 ms (Windows' default) is a
					// double click: ENTER as well, once the field is selected.
					const int cell = row * 24 + col;
					const auto now = std::chrono::steady_clock::now();
					const bool twice = cell == m_lastClickCell && now - m_lastClickAt < std::chrono::milliseconds(500);
					m_lastClickCell = twice ? -1 : cell;
					m_lastClickAt = now;
					const std::string msg = std::to_string(++ m_gotoSeq) + " " + std::to_string(cell) + (twice ? " enter" : "");
					setState("lcdgoto", msg.c_str());
				}
				return true;
			}
			else for (unsigned k = 0; k < kNumKeys; k ++)
				if (kKeyRects[k].contains(x, y))
				{
					m_pressed = int(k);
					m_keyLit[k] = true;
					sendKey(k, true);
					repaint();
					return true;
				}
			if (kSyxLoad.contains(x, y) || kSyxSave.contains(x, y))
			{
				const bool save = kSyxSave.contains(x, y);
				FileBrowserOptions opts;
				opts.saving = save;
				opts.defaultName = save ? "U-220 bulk dump.syx" : nullptr;
				opts.title = save ? "Save a bulk dump of the whole machine (.syx)" : "Load SysEx into the machine (.syx)";
				const std::string dir = m_syxDir.empty() ? lastFolder("sysex") : m_syxDir;
				opts.startDir = dir.empty() ? nullptr : dir.c_str();
				m_browseSlot = save ? kBrowseSyxSave : kBrowseSyxLoad;
				if (!openFileBrowser(opts))
					m_browseSlot = -1;
				return true;
			}
			for (unsigned s = 0; s < kCardSlots; s ++)
				if (kSlots[s].contains(x, y))
				{
					if (ejectRect(kSlots[s]).contains(x, y))
					{
						if (!m_cardPath[s].empty())
						{
							m_cardPath[s].clear();
							setState(kCardStateKeys[s], "");
						}
					}
					else
						browseCard(s);
					return true;
				}
			if (kHwEq.contains(x, y))
			{
				// A host parameter on purpose: it is a setting, saved with the project
				// and automatable, and a lost click here costs nothing.
				m_hwEq = !m_hwEq;
				editParameter(kParamOutputStage, true);
				setParameterValue(kParamOutputStage, m_hwEq ? 1.0f : 0.0f);
				editParameter(kParamOutputStage, false);
				repaint();
				return true;
			}
			if (kKnob.contains(x, y))
			{
				m_dragKnob = true;
				m_dragY = y;
				m_dragStartDb = m_volumeDb;
				editParameter(kParamVolume, true);
				return true;
			}
			return false;
		}

		if (m_pressed >= 0)
		{
			const uint32_t k = uint32_t(m_pressed);
			m_pressed = -1;
			m_keyLit[k] = false;
			sendKey(k, false);
			repaint();
			return true;
		}
		if (m_dragKnob)
		{
			m_dragKnob = false;
			editParameter(kParamVolume, false);
			return true;
		}
		return false;
	}

	bool onMotion(const MotionEvent &ev) override
	{
		if (!m_dragKnob)
			return false;
		const float s = mouseScale();
		const float y = float(ev.pos.getY()) / s;
		float db = m_dragStartDb + (m_dragY - y) * 0.3f;
		db = std::fmax(-40.0f, std::fmin(12.0f, db));
		m_volumeDb = db;
		setParameterValue(kParamVolume, db);
		repaint();
		return true;
	}

	// The computer keyboard plays the panel keys (README.md has the table): arrows CURSOR,
	// with Shift PART/INST, up/down VALUE, Enter ENTER, Esc EXIT, E D M J.  Held as long as
	// the key is; the system's auto-repeat is not passed on -- the firmware repeats itself.
	// Keys with Ctrl, Alt or the Windows key stay the host's.
	bool onKeyboard(const KeyboardEvent &ev) override
	{
		if (ev.mod & (kModifierControl | kModifierAlt | kModifierSuper))
			return false;
		if (!ev.press)
		{
			for (unsigned k = 0; k < kNumKeys; k ++)
				if (m_kbdHeld[k] == ev.key)
				{
					m_kbdHeld[k] = 0;
					m_keyLit[k] = false;
					sendKey(k, false);
					repaint();
					return true;
				}
			return false;
		}
		const bool shift = (ev.mod & kModifierShift) != 0;
		int k = -1;
		switch (ev.key)
		{
		case 'e': k = 0; break;                         // EDIT
		case 'd': k = 1; break;                         // DATA
		case 'm': k = 2; break;                         // MARK
		case 'j': k = 3; break;                         // JUMP
		case kKeyEscape: k = 4; break;                  // EXIT
		case kKeyEnter: case kKeyPadEnter: k = 5; break;
		// The keypad's arrows (NumLock off) as well.
		case kKeyLeft:  case kKeyPadLeft:  k = shift ? 6 : 8; break;   // PART/INST <<, CURSOR <
		case kKeyRight: case kKeyPadRight: k = shift ? 7 : 9; break;   // PART/INST >>, CURSOR >
		case kKeyDown:  case kKeyPadDown:  k = 10; break;              // VALUE -
		case kKeyUp:    case kKeyPadUp:    k = 11; break;              // VALUE +
		default: return false;
		}
		if (m_kbdHeld[k] == ev.key)
			return true;                                // auto-repeat
		m_kbdHeld[k] = ev.key;
		m_keyLit[k] = true;
		sendKey(uint32_t(k), true);
		repaint();
		return true;
	}

	bool onScroll(const ScrollEvent &ev) override
	{
		const float s = mouseScale();
		const float x = float(ev.pos.getX()) / s, y = float(ev.pos.getY()) / s;
		if (!romSetup() && kLcd.contains(x, y))
		{
			// The wheel over the display: VALUE, one press a notch (the plugin queues them).
			m_wheel += float(ev.delta.getY());
			for (; m_wheel >= 1.0f; m_wheel -= 1.0f)
			{
				sendKey(11, true);
				sendKey(11, false);
			}
			for (; m_wheel <= -1.0f; m_wheel += 1.0f)
			{
				sendKey(10, true);
				sendKey(10, false);
			}
			return true;
		}
		if (!kKnob.contains(x, y))
			return false;
		const float db = std::fmax(-40.0f, std::fmin(12.0f, m_volumeDb + float(ev.delta.getY())));
		m_volumeDb = db;
		editParameter(kParamVolume, true);
		setParameterValue(kParamVolume, db);
		editParameter(kParamVolume, false);
		repaint();
		return true;
	}

private:
	// Straight to the DSP, not through the host: see kStateKey in OstrichPlugin.cpp.
	void sendKey(uint32_t k, bool down)
	{
		char msg[16];
		std::snprintf(msg, sizeof(msg), "%u %u", unsigned(k), down ? 1u : 0u);
		setState("key", msg);
	}

	void browseCard(unsigned s)
	{
		// Start where the last card was picked from, in any instance or session; else where
		// this slot's card came from, or the other slot's.
		std::string dir = lastFolder("card");
		for (const std::string &p : { m_cardPath[s], m_cardPath[s ^ 1] })
			if (dir.empty())
				dir = dirOf(p);
		const std::string title = "Insert a PCM card into slot " + std::to_string(s + 1) + " (a card dump, or its MAME zip)";
		FileBrowserOptions opts;
		opts.title = title.c_str();
		opts.startDir = dir.empty() ? nullptr : dir.c_str();
		m_browseSlot = int(s);
		if (!openFileBrowser(opts))
			m_browseSlot = -1;
	}

	void browseRom()
	{
		if (m_romChecking)
			return;
		FileBrowserOptions opts;
		opts.title = "Pick a U-220 ROM image or a zip -- the rest of its folder is searched too";
		opts.startDir = m_romDir.empty() ? nullptr : m_romDir.c_str();
		m_browseSlot = kBrowseRom;
		if (!openFileBrowser(opts))
			m_browseSlot = -1;
	}

	void setRomSet(const char *value)
	{
		std::string part[kRomSlots + 2];
		unsigned k = 0;
		for (const char *c = value; *c; c ++)
		{
			if (*c == kCardInfoFieldSep) { if (++ k >= kRomSlots + 2) break; continue; }
			part[k] += *c;
		}
		m_romsAll = k >= kRomSlots;
		for (unsigned s = 0; s < kRomSlots; s ++)
		{
			m_romWhat[s] = part[s];
			m_romsAll = m_romsAll && !part[s].empty();
		}
		m_romFactory = part[kRomSlots];
		m_romTarget = part[kRomSlots + 1];
		m_romSetSeen = k >= kRomSlots;
		m_romChecking = false;
	}

	// Only once the plugin has said what is missing, and not once the machine runs.
	bool romSetup() const
	{
		return m_romSetSeen && !m_romsAll && !(m_panel.flags & 1);
	}

	void drawRomSetup()
	{
		const Rect &b = kRomBox;
		beginPath();
		roundedRect(b.x, b.y, b.w, b.h, 4);
		fillColor(Color(14, 14, 16));
		fill();
		strokeColor(Color(70, 70, 76));
		strokeWidth(1);
		stroke();

		textAlign(ALIGN_LEFT | ALIGN_BASELINE);
		fontSize(15);
		fillColor(Color(240, 170, 60));
		text(180, 92, "ROM IMAGES NEEDED", nullptr);
		fontSize(11);
		textLineHeight(1.35f);
		fillColor(Color(190, 190, 194));
		textBox(180, 114, 430,
			"Ostrich 220 runs the U-220's own firmware and needs dumps of its ROM chips. "
			"Click CHOOSE FILE and pick any one of the images, or a zip such as MAME's u220.zip: "
			"the rest of that folder is searched too, by contents, not names. They are copied to:", nullptr);
		fillColor(Color(140, 140, 146));
		text(180, 176, fit(m_romTarget, 430, 11).c_str(), nullptr);
		textLineHeight(1.0f);

		barButton(kRomChoose, m_romChecking ? "CHECKING\xe2\x80\xa6" : "CHOOSE FILE\xe2\x80\xa6", !m_romChecking);
		fontSize(10);
		textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
		fillColor(Color(110, 110, 116));
		text(kRomChoose.x + kRomChoose.w + 14, kRomChoose.y + kRomChoose.h / 2 + 1,
		     "roms/README.md on GitHub lists them all", nullptr);

		for (unsigned i = 0; i < kRomCells; i ++)
		{
			const Rect r = romCell(i);
			const bool factory = i == kRomSlots;
			const std::string &what = factory ? m_romFactory : m_romWhat[i];
			const bool ok = !what.empty();
			// A chip: dark body, a notch, pins along both long edges.
			fillColor(Color(120, 120, 126));
			for (float px = r.x + 10; px < r.x + r.w - 8; px += 10)
			{
				beginPath();
				rect(px, r.y - 3, 4, 3);
				rect(px, r.y + r.h, 4, 3);
				fill();
			}
			beginPath();
			roundedRect(r.x, r.y, r.w, r.h, 2);
			fillColor(ok ? Color(34, 44, 26) : Color(28, 28, 31));
			fill();
			strokeColor(ok ? Color(150, 190, 60) : factory ? Color(84, 84, 90) : Color(240, 170, 60));
			strokeWidth(1);
			stroke();
			beginPath();
			circle(r.x + 8, r.y + 8, 2.5f);
			fillColor(Color(10, 10, 12));
			fill();

			textAlign(ALIGN_LEFT | ALIGN_BASELINE);
			fontSize(9);
			fillColor(Color(130, 130, 136));
			text(r.x + 16, r.y + 15, factory ? "SYSEX \xc2\xb7 OPTIONAL" : kRomChips[i], nullptr);
			fontSize(12);
			fillColor(Color(225, 225, 228));
			text(r.x + 10, r.y + 36, factory ? "Factory data" : kRomTitles[i], nullptr);
			fontSize(10);
			fillColor(ok ? Color(150, 190, 60) : factory ? Color(110, 110, 116) : Color(240, 170, 60));
			std::string state;
			if (factory)
				state = ok ? fit(what, r.w - 20, 10) : "not found";
			else
				state = ok ? (i == kRomSlots - 1 ? what + " \xc2\xb7 found" : "found")
				           : std::string(i == kRomSlots - 1 ? "128 KB" : "512 KB") + " \xc2\xb7 missing";
			text(r.x + 10, r.y + 54, state.c_str(), nullptr);
		}
	}

	static std::string dirOf(const std::string &p)
	{
		const size_t cut = p.find_last_of("/\\");
		return cut == std::string::npos ? std::string() : p.substr(0, cut);
	}

	// The folders last picked from, for every instance and the next session: lines of
	// "<kind> <folder>" in %APPDATA%\Ostrich 220\folders.txt (~/.config/Ostrich 220/ on Linux).
	static std::filesystem::path foldersFile()
	{
		namespace fs = std::filesystem;
#ifdef _WIN32
		const wchar_t *base = _wgetenv(L"APPDATA");
		if (base == nullptr || *base == L'\0')
			return {};
		return fs::path(base) / L"Ostrich 220" / L"folders.txt";
#else
		if (const char *x = std::getenv("XDG_CONFIG_HOME"))
			if (*x)
				return fs::u8path(x) / "Ostrich 220" / "folders.txt";
		const char *home = std::getenv("HOME");
		if (home == nullptr || *home == '\0')
			return {};
		return fs::u8path(home) / ".config" / "Ostrich 220" / "folders.txt";
#endif
	}

	static std::vector<std::string> readFolders()
	{
		std::vector<std::string> lines;
		const auto p = foldersFile();
		if (p.empty())
			return lines;
#ifdef _WIN32
		FILE *f = _wfopen(p.c_str(), L"rb");
#else
		FILE *f = std::fopen(p.c_str(), "rb");
#endif
		if (f == nullptr)
			return lines;
		std::string line;
		for (int c; (c = std::fgetc(f)) != EOF; )
			if (c == '\n')
			{
				lines.push_back(line);
				line.clear();
			}
			else if (c != '\r')
				line += char(c);
		if (!line.empty())
			lines.push_back(line);
		std::fclose(f);
		return lines;
	}

	static std::string lastFolder(const std::string &kind)
	{
		for (const std::string &l : readFolders())
			if (l.compare(0, kind.size() + 1, kind + " ") == 0)
				return l.substr(kind.size() + 1);
		return std::string();
	}

	static void rememberFolder(const std::string &kind, const std::string &dir)
	{
		const auto p = foldersFile();
		if (p.empty() || dir.empty() || dir.find('\n') != std::string::npos)
			return;
		std::vector<std::string> lines;
		for (const std::string &l : readFolders())
			if (l.compare(0, kind.size() + 1, kind + " ") != 0)
				lines.push_back(l);
		lines.push_back(kind + " " + dir);
		std::error_code ec;
		std::filesystem::create_directories(p.parent_path(), ec);
#ifdef _WIN32
		FILE *f = _wfopen(p.c_str(), L"wb");
#else
		FILE *f = std::fopen(p.c_str(), "wb");
#endif
		if (f == nullptr)
			return;
		for (const std::string &l : lines)
			std::fprintf(f, "%s\n", l.c_str());
		std::fclose(f);
	}

	void drawSyxButtons()
	{
		barButton(kSyxLoad, "LOAD SYSEX", false);
		barButton(kSyxSave, "SAVE SYSEX", false);
	}

	// A control in the bar: dark with a thin edge, as a host's own; lit when on.
	void barButton(const Rect &r, const char *label, bool lit)
	{
		beginPath();
		roundedRect(r.x + 0.5f, r.y + 0.5f, r.w - 1, r.h - 1, 3);
		fillColor(lit ? Color(150, 190, 60) : Color(30, 30, 33));
		fill();
		strokeColor(lit ? Color(150, 190, 60) : Color(84, 84, 90));
		strokeWidth(1);
		stroke();
		fontSize(10);
		textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
		fillColor(lit ? Color(30, 45, 20) : Color(200, 200, 204));
		text(r.x + r.w / 2, r.y + r.h / 2 + 1, label, nullptr);
	}

	void setCardInfo(const char *value)
	{
		std::string v = value, field[kCardSlots];
		unsigned s = 0;
		for (char c : v)
		{
			if (c == kCardInfoSlotSep) { if (++ s >= kCardSlots) break; continue; }
			field[s] += c;
		}
		for (unsigned i = 0; i < kCardSlots; i ++)
		{
			CardView &cv = m_card[i];
			cv = CardView();
			std::string part[3];
			unsigned k = 0;
			for (char c : field[i])
			{
				if (c == kCardInfoFieldSep) { if (++ k >= 3) break; continue; }
				part[k] += c;
			}
			cv.ok = part[0] == "ok";
			cv.error = part[0] == "err";
			cv.line1 = part[1];
			cv.line2 = part[2];
		}
	}

	void drawSlots()
	{
		for (unsigned s = 0; s < kCardSlots; s ++)
		{
			const Rect &r = kSlots[s];
			const CardView &cv = m_card[s];
			fontSize(10);
			textAlign(ALIGN_CENTER | ALIGN_BASELINE);
			fillColor(Color(220, 220, 220));
			char label[24];
			std::snprintf(label, sizeof(label), "PCM CARD %u", s + 1);
			text(r.x + r.w / 2, r.y - 8, label, nullptr);

			// The slot: a dark recess with the card's edge in it, or empty.
			beginPath();
			roundedRect(r.x, r.y, r.w, r.h, 3);
			fillColor(Color(8, 8, 9));
			fill();
			strokeColor(Color(90, 90, 96));
			strokeWidth(1);
			stroke();
			const float tx = r.x + 10, tw = r.w - 46;
			if (cv.ok)
			{
				beginPath();
				roundedRect(r.x + 4, r.y + 4, r.w - 40, r.h - 8, 2);
				fillColor(Color(196, 200, 206));
				fill();
				textAlign(ALIGN_LEFT | ALIGN_BASELINE);
				fontSize(12);
				fillColor(Color(20, 20, 24));
				textBox(tx, r.y + 18, tw, cv.line1.c_str(), nullptr);
				fontSize(9);
				fillColor(Color(60, 60, 66));
				textBox(tx, r.y + 31, tw, fit(cv.line2, tw, 9).c_str(), nullptr);
			}
			else
			{
				beginPath();
				rect(r.x + 8, r.y + r.h / 2 - 1, r.w - 46, 2);     // the slit
				fillColor(Color(40, 40, 44));
				fill();
				textAlign(ALIGN_LEFT | ALIGN_BASELINE);
				fontSize(cv.error ? 10 : 9);
				fillColor(cv.error ? Color(240, 170, 60) : Color(110, 110, 116));
				textBox(tx, r.y + 15, tw, cv.error ? cv.line1.c_str() : "empty", nullptr);
				if (cv.error)
				{
					fontSize(9);
					textBox(tx, r.y + 31, tw, fit(cv.line2, tw, 9).c_str(), nullptr);
				}
				else
				{
					fillColor(Color(90, 90, 96));
					textBox(tx, r.y + 33, tw, "click to insert", nullptr);
				}
			}

			// Eject: a triangle over a bar, lit while there is something to take out.
			const Rect e = ejectRect(r);
			const bool live = !m_cardPath[s].empty();
			beginPath();
			roundedRect(e.x, e.y, e.w, e.h, 3);
			fillColor(Color(70, 70, 76));
			fill();
			const Color ink = live ? Color(230, 230, 230) : Color(110, 110, 116);
			const float cx = e.x + e.w / 2, top = e.y + 5;
			beginPath();
			moveTo(cx, top);
			lineTo(cx + 7, top + 7);
			lineTo(cx - 7, top + 7);
			closePath();
			fillColor(ink);
			fill();
			beginPath();
			rect(cx - 7, top + 10, 14, 2.5f);
			fill();
		}
	}

	// Cut text to a width, with an ellipsis -- the font is set by the caller.
	std::string fit(const std::string &t, float w, float size)
	{
		fontSize(size);
		std::string s = t;
		Rectangle<float> b;
		while (!s.empty() && textBounds(0, 0, (s + (s.size() < t.size() ? "\xe2\x80\xa6" : "")).c_str(), nullptr, b) > w)
			s.pop_back();
		return s.size() < t.size() ? s + "\xe2\x80\xa6" : s;
	}

	void drawHwEq()
	{
		barButton(kHwEq, "HW-EQ", m_hwEq);
	}

	// MIDI MESSAGE: green while MIDI comes in.  Narrow, as the unit's; under VOLUME, since
	// the panel has no POWER switch for it to sit above.
	void drawMidiLamp()
	{
		const bool lit = (m_panel.flags & 2) != 0;
		const float cx = kKnob.x + kKnob.w / 2, cy = 204, w = 18, h = 5;
		if (lit)
		{
			beginPath();
			rect(cx - w / 2 - 8, cy - h / 2 - 8, w + 16, h + 16);
			fillPaint(boxGradient(cx - w / 2, cy - h / 2, w, h, 2, 8, Color(90, 255, 90, 110), Color(90, 255, 90, 0)));
			fill();
		}
		beginPath();
		roundedRect(cx - w / 2, cy - h / 2, w, h, 1.5f);
		fillColor(lit ? Color(120, 255, 120) : Color(28, 58, 30));
		fill();
		strokeColor(Color(70, 70, 76));
		strokeWidth(1);
		stroke();
		fontSize(9);
		textAlign(ALIGN_CENTER | ALIGN_BASELINE);
		fillColor(Color(170, 170, 170));
		text(cx, cy + 20, "MIDI MESSAGE", nullptr);
	}

	void drawKnob()
	{
		const float cx = kKnob.x + kKnob.w / 2, cy = kKnob.y + kKnob.h / 2, r = kKnob.w / 2;
		beginPath();
		circle(cx, cy, r);
		fillColor(Color(48, 48, 52));
		fill();
		strokeColor(Color(90, 90, 96));
		strokeWidth(2);
		stroke();
		// -40 .. +12 dB over 270 degrees, from bottom left.
		const float t = (m_volumeDb + 40.0f) / 52.0f;
		const float a = float(M_PI) * (0.75f + 1.5f * t);
		beginPath();
		moveTo(cx + std::cos(a) * r * 0.3f, cy + std::sin(a) * r * 0.3f);
		lineTo(cx + std::cos(a) * r * 0.9f, cy + std::sin(a) * r * 0.9f);
		strokeColor(Color(240, 240, 240));
		strokeWidth(3);
		stroke();
		fontSize(11);
		textAlign(ALIGN_CENTER | ALIGN_BASELINE);
		fillColor(Color(200, 200, 200));
		text(cx, kKnob.y - 10, "VOLUME", nullptr);
		char buf[32];
		std::snprintf(buf, sizeof(buf), "%+.1f dB", double(m_volumeDb));
		fillColor(Color(140, 140, 140));
		text(cx, kKnob.y + kKnob.h + 18, buf, nullptr);
	}

	void drawLcd()
	{
		beginPath();
		roundedRect(kLcd.x - 8, kLcd.y - 8, kLcd.w + 16, kLcd.h + 16, 4);
		fillColor(Color(10, 10, 10));
		fill();
		beginPath();
		rect(kLcd.x, kLcd.y, kLcd.w, kLcd.h);
		fillColor(Color(150, 190, 60));            // the yellow-green backlight
		fill();

		const float gx = kLcd.x + kLcdPadX, gy = kLcd.y + kLcdPadY;
		const float cw = (kLcd.w - 2 * kLcdPadX) / 24.0f, ch = (kLcd.h - 2 * kLcdPadY) / 2.0f;
		const Color ink(30, 45, 20);
		fontSize(ch * 0.62f);
		textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
		for (int row = 0; row < 2; row ++)
			for (int col = 0; col < 24; col ++)
			{
				const uint8_t c = m_panel.lcd[row * 24 + col];
				const float x = gx + col * cw, y = gy + row * ch;
				if (c < 0x10)
				{
					drawGlyph(c & 7, x, y, cw, ch, ink);
					continue;
				}
				const char *str = nullptr;
				char one[2] = { char(c), 0 };
				switch (c)
				{
				case 0x5c: str = "\xc2\xa5"; break;          // the HD44780's yen sign
				case 0x7e: str = "\xe2\x86\x92"; break;      // right arrow
				case 0x7f: str = "\xe2\x86\x90"; break;      // left arrow
				case 0xdf: str = "\xc2\xb0"; break;
				case 0xff: str = "\xe2\x96\x88"; break;
				default:   str = (c >= 0x20 && c < 0x7f) ? one : "?"; break;
				}
				if (c == 0x20)
					continue;
				fillColor(ink);
				text(x + cw / 2, y + ch / 2 + 1, str, nullptr);
			}

		// The controller's cursor: an underline in the 8th dot row, and/or the whole cell
		// blinking.  The firmware uses the underline to mark the selected part.
		const uint8_t cp = m_panel.cursor_pos;
		if (cp < 48 && m_panel.cursor_flags)
		{
			const float x = gx + (cp % 24) * cw, y = gy + (cp / 24) * ch;
			const float dw = cw * 0.8f / 5.0f, dh = ch * 0.62f / 8.0f;
			const float ox = x + cw * 0.1f, oy = y + ch * 0.19f;
			fillColor(ink);
			if (m_panel.cursor_flags & 2)
			{
				beginPath();
				rect(ox, oy + 7 * dh + dh * 0.5f, 5 * dw - dw * 0.12f, dh * 0.88f);
				fill();
			}
			if ((m_panel.cursor_flags & 1) && blinkPhase())
			{
				beginPath();
				rect(ox, oy, 5 * dw, 8 * dh);
				fill();
			}
		}
	}

	// The panel's size lives in the plugin's state, so the host saves it with the session.
	// Unscaled -- window pixels over the desktop scale factor -- so that it opens the same on
	// a screen with other scaling.
	void restoreSize(const char *value)
	{
		unsigned w = 0, h = 0;
		if (std::sscanf(value, "%u %u", &w, &h) != 2 || w < 100 || h < 20)
			return;
		// A size saved by an earlier panel (before the card slots, before the bar): keep
		// its width, the height follows.
		if (std::fabs(double(w) / h - kW / kH) > 0.05)
			h = unsigned(std::lround(w * kH / kW));
		m_sizeSaved = value;
		const double scale = getWindow().getScaleFactor();
		const uint pw = uint(std::lround(w * scale)), ph = uint(std::lround(h * scale));
		if (std::abs(int(pw) - int(getWindow().getWidth())) > 2 || std::abs(int(ph) - int(getWindow().getHeight())) > 2)
			getWindow().setSize(pw, ph);
	}

	// Sends the size once it has stood still for a second -- not while the user drags, and
	// not in the first seconds, while the host still settles the window and the saved size
	// is on its way (restoreSize).
	void saveSizeWhenSettled()
	{
		const auto now = std::chrono::steady_clock::now();
		const uint pw = getWindow().getWidth(), ph = getWindow().getHeight();
		if (pw != m_sizeSeenW || ph != m_sizeSeenH)
		{
			m_sizeSeenW = pw;
			m_sizeSeenH = ph;
			m_sizeSeenSince = now;
			return;
		}
		if (now - m_created < std::chrono::seconds(3) || now - m_sizeSeenSince < std::chrono::seconds(1))
			return;
		const double scale = getWindow().getScaleFactor();
		char value[32];
		std::snprintf(value, sizeof(value), "%u %u", unsigned(std::lround(pw / scale)), unsigned(std::lround(ph / scale)));
		if (m_sizeSaved == value)
			return;
		unsigned sw = 0, sh = 0;
		if (std::sscanf(m_sizeSaved.c_str(), "%u %u", &sw, &sh) == 2
				&& std::abs(int(sw * scale) - int(pw)) <= 2 && std::abs(int(sh * scale) - int(ph)) <= 2)
			return;                     // the host's rounding, not a resize
		m_sizeSaved = value;
		setState("uisize", value);
	}

	// Cubase at 125 % tells a new view "scale 1.0" before it opens and sizes it for that,
	// while its own window is made for 125 %: the view sat in a black margin right and below
	// until reopened (the second time Cubase gives the right size).  A view below its least
	// size at the desktop's scale is that; it asks the host for the least size, as a
	// reopened view gets.  Once, after DPF's first idle (until then it drops the request).
	void fitHostFrame()
	{
		if (std::chrono::steady_clock::now() - m_created < std::chrono::milliseconds(200))
			return;
		m_frameFitted = true;
		bool keep = false;
		const Size<uint> least = getWindow().getGeometryConstraints(keep);
		const double scale = getWindow().getScaleFactor();
		const uint lw = uint(std::lround(least.getWidth() * scale)), lh = uint(std::lround(least.getHeight() * scale));
		if (getWindow().getWidth() + 2 < lw || getWindow().getHeight() + 2 < lh)
			getWindow().setSize(lw, lh);
	}

	// Mouse events come in the window's own pixels.  The panel is drawn in design units
	// scaled by getWidth() / kW on top of DGL's automatic scaling (the window is larger than
	// the minimum size), so the full factor is the window width over the design width --
	// getWidth() / kW alone is only right while the window is at its minimum size.
	float mouseScale() const
	{
		return float(getWindow().getWidth()) / kW;
	}

	// The HD44780 blinks its block cursor at about 1.9 Hz.
	bool blinkPhase() const
	{
		const double t = std::chrono::duration<double>(
				std::chrono::steady_clock::now().time_since_epoch()).count() * 1.9;
		return (t - std::floor(t)) < 0.5;
	}

	void uiIdle() override
	{
		if (!m_frameFitted && std::chrono::steady_clock::now() - m_created < std::chrono::seconds(2))
			fitHostFrame();
		saveSizeWhenSettled();
		if (m_panel.cursor_flags & 1)
			repaint();
		if (m_romChecking && std::chrono::steady_clock::now() - m_romAskedAt > std::chrono::seconds(15))
		{
			m_romChecking = false;          // no answer: let the user try again
			repaint();
		}
	}

	// A CGRAM glyph as the 5 x 8 dots the firmware defined.
	void drawGlyph(int g, float x, float y, float cw, float ch, const Color &ink)
	{
		const float dw = cw * 0.8f / 5.0f, dh = ch * 0.62f / 8.0f;
		const float ox = x + cw * 0.1f, oy = y + ch * 0.19f;
		fillColor(ink);
		for (int r = 0; r < 8; r ++)
		{
			const uint8_t bits = m_panel.cgram[g * 8 + r];
			for (int b = 0; b < 5; b ++)
				if (bits & (0x10 >> b))
				{
					beginPath();
					rect(ox + b * dw, oy + r * dh, dw * 0.88f, dh * 0.88f);
					fill();
				}
		}
	}

	void drawKeys()
	{
		// Group prints, as on the unit: PART/INST over the two arrow keys top right,
		// CURSOR and VALUE over the bottom pairs, CHORUS / REVERB between the rows of
		// block 1 (EDIT + MARK and DATA + JUMP are the effect shortcuts).
		fontSize(10);
		textAlign(ALIGN_CENTER | ALIGN_BASELINE);
		fillColor(Color(170, 170, 170));
		const float mid1 = kB1x + (kGap + kKeyW) / 2;
		text(mid1, kRow0 + kKeyH + 24, "\xe2\x80\x94 CHORUS / REVERB \xe2\x80\x94", nullptr);
		text(kB2x + kGap * 2 + (kGap + kKeyW) / 2, kRow0 - 8, "PART / INST", nullptr);
		text(kB2x + (kGap + kKeyW) / 2, kRow1 - 8, "CURSOR", nullptr);
		text(kB2x + kGap * 2 + (kGap + kKeyW) / 2, kRow1 - 8, "VALUE", nullptr);

		for (unsigned k = 0; k < kNumKeys; k ++)
		{
			const Rect &r = kKeyRects[k];
			beginPath();
			roundedRect(r.x, r.y, r.w, r.h, 3);
			fillColor(m_keyLit[k] ? Color(130, 130, 138) : Color(70, 70, 76));
			fill();
			strokeColor(Color(110, 110, 116));
			strokeWidth(1);
			stroke();
			if (k < 6)
			{
				// The lettered keys carry their name above them.
				fontSize(10);
				fillColor(Color(220, 220, 220));
				text(r.x + r.w / 2, r.y - 8, kKeyLabels[k], nullptr);
			}
			else
			{
				fontSize(13);
				fillColor(Color(220, 220, 220));
				text(r.x + r.w / 2, r.y + r.h / 2 + 5, kKeyLabels[k], nullptr);
			}
		}
	}

	struct CardView { bool ok = false, error = false; std::string line1, line2; };

	PanelBlob m_panel;
	std::string m_status;
	CardView m_card[kCardSlots];
	std::string m_cardPath[kCardSlots];
	// What the open file dialog is for: a card slot (0, 1) or a SysEx file.
	static constexpr int kBrowseSyxLoad = 10, kBrowseSyxSave = 11, kBrowseRom = 12;
	int m_browseSlot = -1;
	unsigned m_syxSeq = 0;
	std::string m_syxDir;
	// The ROM images, while some are missing (drawRomSetup).
	std::string m_romWhat[kRomSlots], m_romFactory, m_romTarget, m_romDir;
	bool m_romSetSeen = false, m_romsAll = false, m_romChecking = false;
	std::chrono::steady_clock::time_point m_romAskedAt {};
	const std::chrono::steady_clock::time_point m_created = std::chrono::steady_clock::now();
	std::string m_sizeSaved;                    // "<w> <h>" unscaled, as in the plugin state
	uint m_sizeSeenW = 0, m_sizeSeenH = 0;
	std::chrono::steady_clock::time_point m_sizeSeenSince {};
	bool m_frameFitted = false;                 // fitHostFrame
	float m_volumeDb = 0.0f;
	bool m_hwEq = true;
	bool m_keyLit[kNumKeys] = {};
	uint m_kbdHeld[kNumKeys] = {};              // onKeyboard: the computer key holding each
	float m_wheel = 0.0f;                       // onScroll over the display, part notches
	unsigned m_gotoSeq = 0;                     // makes the same cell clicked twice two clicks
	int m_lastClickCell = -1;                   // the display's last click, for double clicks
	std::chrono::steady_clock::time_point m_lastClickAt;
	int m_pressed = -1;
	bool m_dragKnob = false;
	float m_dragY = 0, m_dragStartDb = 0;

	DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OstrichUI)
};

UI *createUI()
{
	return new OstrichUI();
}

END_NAMESPACE_DISTRHO
