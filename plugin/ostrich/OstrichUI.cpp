// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ostrich -- the panel.  A plain first version (ROADMAP.md, Phase 4 step 4): the 24 x 2
// LCD, the twelve keys where the U-220 has them, a volume knob and a status line.  The
// drawn panel comes later; the layout here already follows Helge's photo of the unit.

#include "DistrhoUI.hpp"
#include "OstrichShared.hpp"

#include <cmath>
#include <string>

START_NAMESPACE_DISTRHO

namespace {

using namespace ostrich;

constexpr float kW = 960.0f, kH = 240.0f;           // design units

struct Rect { float x, y, w, h; bool contains(float px, float py) const
	{ return px >= x && px < x + w && py >= y && py < y + h; } };

// The keys, in parameter order (OstrichShared.hpp).  Block 1 is 2 x 2, block 2 is 2 x 4,
// top row first -- as on the unit.
constexpr float kKeyW = 62.0f, kKeyH = 26.0f;
constexpr float kB1x = 548.0f, kB2x = 700.0f, kRow0 = 92.0f, kRow1 = 150.0f, kGap = 74.0f;
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

const Rect kLcd    { 160.0f, 70.0f, 360.0f, 104.0f };
const Rect kKnob   { 62.0f, 96.0f, 64.0f, 64.0f };

} // anonymous namespace


class OstrichUI : public UI
{
public:
	OstrichUI()
		: UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
	{
		loadSharedResources();
		std::memset(&m_panel, 0, sizeof(m_panel));
		const double scale = getScaleFactor();
		if (d_isNotEqual(scale, 1.0))
			setSize(uint(DISTRHO_UI_DEFAULT_WIDTH * scale), uint(DISTRHO_UI_DEFAULT_HEIGHT * scale));
		setGeometryConstraints(uint(kW / 2), uint(kH / 2), true);
	}

protected:
	void parameterChanged(uint32_t index, float value) override
	{
		if (index == kParamVolume)
			m_volumeDb = value;
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
		else if (std::strcmp(key, "status") == 0)
		{
			m_status = value;
			repaint();
		}
	}

	void onNanoDisplay() override
	{
		const float s = getWidth() / kW;
		scale(s, s);

		// The body: black front, as the unit.
		beginPath();
		rect(0, 0, kW, kH);
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
		text(24, 44, "OSTRICH", nullptr);
		fontSize(11);
		fillColor(Color(170, 170, 170));
		text(140, 44, "RS-PCM SOUND MODULE  \xc2\xb7  U-220 EMULATION", nullptr);

		drawKnob();
		drawLcd();
		drawKeys();

		// Status line.
		fontSize(11);
		textAlign(ALIGN_LEFT | ALIGN_BASELINE);
		fillColor(m_panel.flags & 1 ? Color(140, 140, 140) : Color(240, 170, 60));
		textBox(24, 214, kW - 48, m_status.empty() ? " " : m_status.c_str(), nullptr);
	}

	bool onMouse(const MouseEvent &ev) override
	{
		if (ev.button != 1)
			return false;
		const float s = getWidth() / kW;
		const float x = float(ev.pos.getX()) / s, y = float(ev.pos.getY()) / s;

		if (ev.press)
		{
			for (unsigned k = 0; k < kNumKeys; k ++)
				if (kKeyRects[k].contains(x, y))
				{
					m_pressed = int(k);
					m_keyLit[k] = true;
					editParameter(kParamKeyFirst + k, true);
					setParameterValue(kParamKeyFirst + k, 1.0f);
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
			setParameterValue(kParamKeyFirst + k, 0.0f);
			editParameter(kParamKeyFirst + k, false);
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
		const float s = getWidth() / kW;
		const float y = float(ev.pos.getY()) / s;
		float db = m_dragStartDb + (m_dragY - y) * 0.3f;
		db = std::fmax(-40.0f, std::fmin(12.0f, db));
		m_volumeDb = db;
		setParameterValue(kParamVolume, db);
		repaint();
		return true;
	}

	bool onScroll(const ScrollEvent &ev) override
	{
		const float s = getWidth() / kW;
		if (!kKnob.contains(float(ev.pos.getX()) / s, float(ev.pos.getY()) / s))
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

		const float cw = kLcd.w / 24.0f, ch = kLcd.h / 2.0f;
		const Color ink(30, 45, 20);
		fontSize(ch * 0.62f);
		textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
		for (int row = 0; row < 2; row ++)
			for (int col = 0; col < 24; col ++)
			{
				const uint8_t c = m_panel.lcd[row * 24 + col];
				const float x = kLcd.x + col * cw, y = kLcd.y + row * ch;
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
		fontSize(10);
		textAlign(ALIGN_CENTER | ALIGN_BASELINE);
		fillColor(Color(170, 170, 170));
		text(kB1x + kGap / 2 + kKeyW / 2, kRow0 - 30, "\xe2\x80\x94 CHORUS / REVERB \xe2\x80\x94", nullptr);
		text(kB2x + kGap * 2 + kKeyW / 2 + kGap / 2, kRow0 - 30, "PART / INST", nullptr);
		text(kB2x + kGap / 2 + kKeyW / 2, kRow1 - 30, "CURSOR", nullptr);
		text(kB2x + kGap * 2 + kKeyW / 2 + kGap / 2, kRow1 - 30, "VALUE", nullptr);

		for (unsigned k = 0; k < kNumKeys; k ++)
		{
			const Rect &r = kKeyRects[k];
			beginPath();
			roundedRect(r.x, r.y, r.w, r.h, 3);
			fillColor(m_keyLit[k] ? Color(120, 120, 128) : Color(70, 70, 76));
			fill();
			strokeColor(Color(110, 110, 116));
			strokeWidth(1);
			stroke();
			// Labels for the lettered keys sit above them, as printed on the unit.
			fontSize(10);
			fillColor(Color(220, 220, 220));
			if (k < 6)
				text(r.x + r.w / 2, r.y - 6, kKeyLabels[k], nullptr);
			else
			{
				fontSize(13);
				text(r.x + r.w / 2, r.y + r.h / 2 + 5, kKeyLabels[k], nullptr);
			}
		}
	}

	PanelBlob m_panel;
	std::string m_status;
	float m_volumeDb = 0.0f;
	bool m_keyLit[kNumKeys] = {};
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
