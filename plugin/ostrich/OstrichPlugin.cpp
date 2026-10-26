// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ostrich -- the Roland U-220 as a plugin.  The DSP half: ROMs, the machine, MIDI in,
// 32 kHz -> host rate, the panel out to the UI and the battery-backed memory into the
// host's session.
//
// The machine is U220Core (plugin/core), null-tested against the MAME driver.  Everything
// here is about being a plugin; nothing here may change how the machine behaves.

#include "DistrhoPlugin.hpp"
#include "OstrichShared.hpp"

#include "u220_core.h"
#include "u220_fx.h"
#include "DacStage.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

START_NAMESPACE_DISTRHO

namespace {

namespace fs = std::filesystem;
using namespace ostrich;

/// A path as UTF-8 in a std::string (C++20's u8string() is a different type).
std::string pstr(const fs::path &p)
{
	const auto u = p.u8string();
	return std::string(u.begin(), u.end());
}

// ---- SHA-1, to recognise the ROM images whatever they are called -----------------

std::string sha1(const std::vector<uint8_t> &data)
{
	uint32_t h[5] = { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 };
	std::vector<uint8_t> m(data);
	const uint64_t bits = uint64_t(data.size()) * 8;
	m.push_back(0x80);
	while (m.size() % 64 != 56)
		m.push_back(0);
	for (int i = 7; i >= 0; i --)
		m.push_back(uint8_t(bits >> (i * 8)));
	auto rol = [](uint32_t x, int n) { return (x << n) | (x >> (32 - n)); };
	for (size_t off = 0; off < m.size(); off += 64)
	{
		uint32_t w[80];
		for (int i = 0; i < 16; i ++)
			w[i] = uint32_t(m[off + i * 4]) << 24 | uint32_t(m[off + i * 4 + 1]) << 16
				 | uint32_t(m[off + i * 4 + 2]) << 8 | m[off + i * 4 + 3];
		for (int i = 16; i < 80; i ++)
			w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
		uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
		for (int i = 0; i < 80; i ++)
		{
			uint32_t f, k;
			if (i < 20)      { f = (b & c) | (~b & d);           k = 0x5A827999; }
			else if (i < 40) { f = b ^ c ^ d;                    k = 0x6ED9EBA1; }
			else if (i < 60) { f = (b & c) | (b & d) | (c & d);  k = 0x8F1BBCDC; }
			else             { f = b ^ c ^ d;                    k = 0xCA62C1D6; }
			const uint32_t t = rol(a, 5) + f + e + k + w[i];
			e = d; d = c; c = rol(b, 30); b = a; a = t;
		}
		h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
	}
	char out[41];
	for (int i = 0; i < 5; i ++)
		std::snprintf(out + i * 8, 9, "%08x", h[i]);
	return out;
}

// ---- the images, by SHA-1 (roms/README.md) --------------------------------------

struct KnownImage { const char *sha1; int slot; const char *what; };
constexpr int kSlotProgram = 6;             // 0..5 = wave ROMs A..F
const KnownImage kKnown[] = {
	{ "9d72b1688a173505b5e1c5ddac01efa78ee76c7f", kSlotProgram, "firmware v1.02" },
	{ "d0b66c0ea0e3af284a1806226aed79d8da2f3dd4", kSlotProgram, "firmware v1.01" },
	{ "e1c6f1ee29cebe8663cb0c6989b8faebb3d841fd", kSlotProgram, "firmware v1.00" },
	{ "55d36199ea489969a729782afe4cbff5b1cede0e", 0, "wave ROM A (IC19)" },
	{ "8b8067fbc6fa7b2e8694a3785f123627ea458d1f", 1, "wave ROM B (IC20)" },
	{ "8dd9826d7d3e67b9a568eadeaaf7856a05f3e068", 2, "wave ROM C (IC21)" },
	{ "35b98a0fb5cf643fb7b761a74581486eee3bd8c3", 3, "wave ROM D (IC22)" },
	{ "71b6f97df6dd4e573db5743c10c77e86d67965a3", 4, "wave ROM E (IC23)" },
	{ "aea715a0a554c071a2e912894805a8d819802e1d", 5, "wave ROM F (IC24)" },
};
const char *const kSlotNames[7] = { "A", "B", "C", "D", "E", "F", "firmware" };

std::vector<uint8_t> readFile(const fs::path &p, size_t limit = 0x200000)
{
	std::vector<uint8_t> data;
	std::error_code ec;
	const auto n = fs::file_size(p, ec);
	if (ec || n == 0 || n > limit)
		return data;
	FILE *f = nullptr;
#ifdef _WIN32
	f = _wfopen(p.c_str(), L"rb");
#else
	f = std::fopen(p.c_str(), "rb");
#endif
	if (!f)
		return data;
	data.resize(size_t(n));
	if (std::fread(data.data(), 1, data.size(), f) != data.size())
		data.clear();
	std::fclose(f);
	return data;
}

/// Where the user puts the images, most specific first.  On Windows ProgramData comes
/// first: Cubase runs outside the MSIX sandbox some tools live in, and %LOCALAPPDATA% is
/// virtualised there -- a folder made by such a tool is invisible to the host.
std::vector<fs::path> romDirs()
{
	std::vector<fs::path> dirs;
	auto env = [&](const char *name, const char *tail) {
		if (const char *v = std::getenv(name))
			if (*v) dirs.push_back(fs::u8path(v) / fs::u8path(tail));
	};
	// "Ostrich 220", and the "Ostrich" of the test builds before the first release.
	env("OSTRICH_ROMS", "");
	for (const char *name : { "Ostrich 220", "Ostrich" })
	{
		const std::string roms = std::string(name) + "/roms";
#ifdef _WIN32
		env("PROGRAMDATA", roms.c_str());
		env("LOCALAPPDATA", roms.c_str());
		env("USERPROFILE", ("Documents/" + roms).c_str());
#else
		env("XDG_DATA_HOME", roms.c_str());
		env("HOME", (".local/share/" + roms).c_str());
		dirs.push_back("/usr/share/" + roms);
#endif
	}
	return dirs;
}

/// The factory dump: complete Roland DT1 messages with good checksums, each address
/// once.  The dump that circulates repeats its last packets, the very last one cut off
/// (tools/syx_to_mid.py has the story); this drops exactly that.
std::vector<std::vector<uint8_t>> parseDump(const std::vector<uint8_t> &d)
{
	std::vector<std::vector<uint8_t>> out;
	std::vector<std::array<uint8_t, 7>> seen;
	for (size_t i = 0; i < d.size(); )
	{
		if (d[i] != 0xF0) { i ++; continue; }
		size_t j = i + 1;
		while (j < d.size() && d[j] != 0xF7 && d[j] != 0xF0)
			j ++;
		if (j >= d.size() || d[j] != 0xF7) { i = j; continue; }
		std::vector<uint8_t> msg(d.begin() + long(i), d.begin() + long(j) + 1);
		i = j + 1;
		if (msg.size() < 11 || msg[1] != 0x41)
			continue;
		unsigned sum = 0;
		for (size_t k = 5; k < msg.size() - 1; k ++)
			sum += msg[k];
		if (sum % 128)
			continue;
		std::array<uint8_t, 7> key;
		std::copy(msg.begin() + 1, msg.begin() + 8, key.begin());
		if (std::find(seen.begin(), seen.end(), key) != seen.end())
			continue;
		seen.push_back(key);
		out.push_back(std::move(msg));
	}
	return out;
}

} // anonymous namespace


class OstrichPlugin : public Plugin
{
public:
	OstrichPlugin()
		: Plugin(kParamCount, 0, kStateCount)
	{
		for (auto &k : m_keySeq) k.store(0);
		m_core.setEffects(&m_fx);
		loadRoms();
		setupRate(getSampleRate());
	}

	~OstrichPlugin() override
	{
		if (m_sysexThread.joinable())
			m_sysexThread.join();
	}

protected:
	const char *getLabel() const override { return "Ostrich220"; }
	const char *getMaker() const override { return "Ostrich 220 project"; }
	const char *getLicense() const override { return "GPL-3.0-or-later"; }
	const char *getHomePage() const override { return "https://example.invalid/ostrich"; }
	uint32_t getVersion() const override { return d_version(0, 1, 0); }
	int64_t getUniqueId() const override { return d_cconst('O', '2', '2', '0'); }
	const char *getDescription() const override
	{
		return "An emulation of the Roland U-220 RS-PCM sound module, running its original "
		       "firmware. Requires the user's own ROM images.";
	}

	// ---- parameters ------------------------------------------------------------------

	void initParameter(uint32_t index, Parameter &p) override
	{
		if (index == kParamVolume)
		{
			p.hints = kParameterIsAutomatable;
			p.name = "Volume";
			p.symbol = "volume";
			p.unit = "dB";
			p.ranges.def = 0.0f;
			p.ranges.min = -40.0f;
			p.ranges.max = 12.0f;
			return;
		}
		if (index == kParamOutputStage)
		{
			p.hints = kParameterIsBoolean | kParameterIsAutomatable;
			p.name = "U-220 output stage";
			p.shortName = "HW-EQ";
			p.symbol = "output_stage";
			p.ranges.def = 1.0f;
			p.ranges.min = 0.0f;
			p.ranges.max = 1.0f;
			return;
		}
		if (index == kParamDirectOuts)
		{
			p.hints = kParameterIsBoolean;
			p.name = "DIRECT OUT 1/2 on own outputs";
			p.shortName = "Direct outs";
			p.symbol = "direct_outs";
			p.ranges.def = 1.0f;
			p.ranges.min = 0.0f;
			p.ranges.max = 1.0f;
			return;
		}
		const uint32_t k = index - kParamKeyFirst;
		p.hints = kParameterIsBoolean | kParameterIsAutomatable;
		p.name = kKeyNames[k];
		p.symbol = kKeySymbols[k];
		p.ranges.def = 0.0f;
		p.ranges.min = 0.0f;
		p.ranges.max = 1.0f;
	}

	float getParameterValue(uint32_t index) const override
	{
		if (index == kParamVolume)
			return m_volumeDb;
		if (index == kParamOutputStage)
			return m_outputStage ? 1.0f : 0.0f;
		if (index == kParamDirectOuts)
			return m_directOuts ? 1.0f : 0.0f;
		return m_keyDown[index - kParamKeyFirst].load() ? 1.0f : 0.0f;
	}

	// A click can press and release before run() comes round.  So a press also bumps a
	// counter, and run() presses for every new count, holding long enough in EMULATED time
	// for the firmware's key scan to see it.
	void setParameterValue(uint32_t index, float value) override
	{
		if (index == kParamVolume)
		{
			m_volumeDb = value;
			m_gainTarget = std::pow(10.0f, value / 20.0f);
			return;
		}
		if (index == kParamOutputStage)
		{
			// A flag the core reads per block; no lock needed for a bool.
			m_outputStage = value > 0.5f;
			m_core.setOutputFilter(m_outputStage);
			// The DAC's images belong to the same analogue output (a bool, read per sample).
			for (DacStage &d : m_dac)
				d.setImages(m_outputStage);
			return;
		}
		if (index == kParamDirectOuts)
		{
			m_directOuts = value > 0.5f;     // read per block
			return;
		}
		const uint32_t k = index - kParamKeyFirst;
		if (k < kNumKeys)
			setKey(k, value > 0.5f);
	}

	// The unit's three stereo pairs.  DIRECT OUT 1 and 2 are extra outputs, off until the
	// user switches them on in the host (patches/0011): a plug in the jack.
	void initAudioPort(bool input, uint32_t index, AudioPort &port) override
	{
		if (input)
			return Plugin::initAudioPort(input, index, port);
		static const char *const names[6] = {
			"MIX OUT L", "MIX OUT R", "DIRECT OUT 1 L", "DIRECT OUT 1 R", "DIRECT OUT 2 L", "DIRECT OUT 2 R" };
		static const char *const symbols[6] = { "mix_l", "mix_r", "dir1_l", "dir1_r", "dir2_l", "dir2_r" };
		port.name = names[index];
		port.symbol = symbols[index];
		port.groupId = index < 2 ? kPortGroupStereo : index < 4 ? kGroupDirect1 : kGroupDirect2;
	}

	void initPortGroup(uint32_t groupId, PortGroup &group) override
	{
		if (groupId == kGroupDirect1)
		{
			group.name = "DIRECT OUT 1";
			group.symbol = "direct_out_1";
		}
		else if (groupId == kGroupDirect2)
		{
			group.name = "DIRECT OUT 2";
			group.symbol = "direct_out_2";
		}
		else
			Plugin::initPortGroup(groupId, group);
	}

	void setKey(uint32_t k, bool down)
	{
		if (down && !m_keyDown[k].load())
			m_keySeq[k].fetch_add(1);
		m_keyDown[k].store(down);
	}

	// ---- state ---------------------------------------------------------------------------

	void initState(uint32_t index, State &state) override
	{
		switch (index)
		{
		case kStatePanel:
			state.key = "panel";
			state.label = "Panel";
			state.hints = kStateIsOnlyForUI;
			break;
		case kStateStatus:
			state.key = "status";
			state.label = "Status";
			state.hints = kStateIsOnlyForUI;
			break;
		case kStateNvram:
			state.key = "nvram";
			state.label = "Battery-backed memory";
			state.hints = kStateIsHostReadable;
			break;
		case kStateKey:
			// The panel's own keys come this way and not as parameter edits: under VST3 a
			// parameter edit from the UI reaches the DSP only through the host's
			// automation path, and in Cubase the clicks never arrived.  A state message
			// goes from the UI to the DSP directly.  The key parameters stay, for
			// automation and generic host UIs.
			state.key = "key";
			state.label = "Panel key";
			state.hints = kStateIsOnlyForDSP;
			break;
		case kStateUiSize:
			// Kept here only so that the host saves it with the session: the panel sets it
			// when the user resizes it, and gets it back when it opens (OstrichUI.cpp).
			state.key = "uisize";
			state.label = "Panel size";
			state.hints = 0;
			break;
		case kStateCard1:
		case kStateCard2:
			// The file's path, not its bytes: a card is Roland's data and stays on the
			// user's disk; a session moved to another machine says which file is missing.
			state.key = kCardStateKeys[index - kStateCard1];
			state.label = index == kStateCard1 ? "PCM card slot 1" : "PCM card slot 2";
			state.hints = 0;
			break;
		case kStateCardInfo:
			state.key = "cardinfo";
			state.label = "PCM cards";
			state.hints = kStateIsOnlyForUI;
			break;
		case kStateSysex:
			// "<n> save|load <path>": a .syx file, see requestSysex.
			state.key = "sysex";
			state.label = "SysEx file";
			state.hints = kStateIsOnlyForDSP;
			break;
		}
		state.defaultValue = "";
	}

	String getState(const char *key) const override
	{
		if (std::strcmp(key, "status") == 0)
		{
			std::lock_guard<std::mutex> lock(m_statusLock);
			return String(m_status.c_str());
		}
		if (std::strcmp(key, "uisize") == 0)
		{
			std::lock_guard<std::mutex> lock(m_uiSizeLock);
			return String(m_uiSize.c_str());
		}
		for (unsigned s = 0; s < kCardSlots; s ++)
			if (std::strcmp(key, kCardStateKeys[s]) == 0)
			{
				std::lock_guard<std::mutex> lock(m_cardLock);
				return String(m_cardPath[s].c_str());
			}
		if (std::strcmp(key, "cardinfo") == 0)
		{
			std::lock_guard<std::mutex> lock(m_cardLock);
			return String(cardInfoText().c_str());
		}
		if (std::strcmp(key, "nvram") != 0 || !m_romsLoaded)
			return String();
		std::lock_guard<std::mutex> lock(m_machine);
		std::vector<uint8_t> raw(m_core.saveState(nullptr, 0));
		m_core.saveState(raw.data(), raw.size());
		std::vector<char> hex(raw.size() * 2 + 1);
		encodeHex(raw.data(), raw.size(), hex.data());
		return String(hex.data());
	}

	void setState(const char *key, const char *value) override
	{
		if (value == nullptr)
			return;
		if (std::strcmp(key, "key") == 0)
		{
			// A restored session hands every key back, empty: ignore anything malformed.
			unsigned k = 0, down = 0;
			if (std::sscanf(value, "%u %u", &k, &down) == 2 && k < kNumKeys)
			{
				if (s_debug)
					d_stdout("Ostrich 220: key %u %s (state)", k, down ? "down" : "up");
				setKey(k, down != 0);
			}
			return;
		}
		if (std::strcmp(key, "uisize") == 0)
		{
			std::lock_guard<std::mutex> lock(m_uiSizeLock);
			m_uiSize = value;
			return;
		}
		for (unsigned s = 0; s < kCardSlots; s ++)
			if (std::strcmp(key, kCardStateKeys[s]) == 0)
			{
				setCard(s, value);
				return;
			}
		if (std::strcmp(key, "sysex") == 0)
		{
			if (value[0] != '\0')
				requestSysex(value);
			return;
		}
		if (std::strcmp(key, "nvram") != 0 || value[0] == '\0')
			return;
		std::vector<uint8_t> raw(std::strlen(value) / 2);
		if (!decodeHex(value, raw.data(), raw.size()))
			return;
		std::lock_guard<std::mutex> lock(m_machine);
		if (m_romsLoaded && m_core.loadState(raw.data(), raw.size()))
		{
			m_needFirstBoot = false;
			d_stdout("Ostrich 220: restored the battery-backed memory from the session.");
		}
	}

	// ---- audio ---------------------------------------------------------------------------

	void sampleRateChanged(double newRate) override { setupRate(newRate); }

	void activate() override
	{
		for (DacStage &d : m_dac)
			d.reset();
		m_gain = m_gainTarget;
		// A new instance with no session behind it: the factory-fresh machine.  Done here
		// and not in run(): it is a few seconds of emulated time, a fraction of one in CPU.
		std::lock_guard<std::mutex> lock(m_machine);
		if (m_romsLoaded && m_needFirstBoot)
			firstBoot();
	}

	void run(const float **, float **outputs, uint32_t frames,
	         const MidiEvent *midiEvents, uint32_t midiEventCount) override
	{
		float *const outL = outputs[0];
		float *const outR = outputs[1];

		if (m_statusPending.load())
		{
			// Set on other threads too (the SysEx work): never wait for it here.
			std::unique_lock<std::mutex> sl(m_statusLock, std::try_to_lock);
			if (sl.owns_lock())
			{
				m_statusPending.store(false);
				updateStateValue("status", m_status.c_str());
			}
		}
		if (m_cardInfoPending.load())
		{
			std::unique_lock<std::mutex> cl(m_cardLock, std::try_to_lock);
			if (cl.owns_lock())
			{
				m_cardInfoPending.store(false);
				updateStateValue("cardinfo", m_cardInfoSent.c_str());
			}
		}

		// setState() reboots the machine on another thread.  Never wait for it here.
		std::unique_lock<std::mutex> lock(m_machine, std::try_to_lock);
		if (!lock.owns_lock() || !m_romsLoaded || m_needFirstBoot)
		{
			for (uint32_t c = 0; c < DISTRHO_PLUGIN_NUM_OUTPUTS; c ++)
				std::memset(outputs[c], 0, sizeof(float) * frames);
			if (!m_romsLoaded)
				publishPanel(frames);
			return;
		}

		const uint32_t coreFrames = m_dac[0].inputsFor(frames);
		tickKeys(coreFrames);
		tickCards(coreFrames);

		for (uint32_t i = 0; i < midiEventCount; i ++)
		{
			const MidiEvent &ev = midiEvents[i];
			const uint8_t *data = ev.size > MidiEvent::kDataSize ? ev.dataExt : ev.data;
			if (ev.size == 0)
				continue;
			uint32_t off = uint32_t(double(ev.frame) * kCoreRate / m_hostRate);
			if (coreFrames && off >= coreFrames)
				off = coreFrames - 1;
			m_core.midiIn(data, ev.size, off);
		}

		if (m_coreBuf[0].size() < coreFrames)
		{
			// Only on a host block larger than any seen so far, so practically never.
			for (auto &b : m_coreBuf)
				b.resize(coreFrames);
		}

		// A DIRECT OUT pair has a plug in its jack when the host gave it buffers of its own:
		// DPF hands every output switched off in the host one and the same dummy buffer.
		const bool plugged[2] = {
			m_directOuts && outputs[2] != outputs[3],
			m_directOuts && outputs[4] != outputs[5] };
		float *core[6];
		for (int c = 0; c < 6; c ++)
			core[c] = m_coreBuf[c].data();
		m_core.render(&core[0], plugged[0] ? &core[2] : nullptr, plugged[1] ? &core[4] : nullptr, coreFrames);
		sendMidiOut(frames);
		for (int c = 0; c < 6; c ++)
			if (c < 2 || plugged[(c - 2) / 2])
				m_dac[c].process(core[c], coreFrames, outputs[c], frames);
			else
				std::memset(outputs[c], 0, sizeof(float) * frames);

		// The Volume knob acts on MIX OUT only; the DIRECT OUT jacks are at a fixed level.
		for (uint32_t i = 0; i < frames; i ++)
		{
			m_gain += (m_gainTarget - m_gain) * 0.001f;
			outL[i] *= m_gain;
			outR[i] *= m_gain;
		}
		publishPanel(frames);
	}

private:
	static constexpr double kCoreRate = double(kCoreSampleRate);
	static inline const bool s_debug = std::getenv("OSTRICH_DEBUG") != nullptr;
	static constexpr double kPanelRefreshHz = 25.0;
	static constexpr uint32_t kKeyHoldFrames = kCoreSampleRate / 8;    // 125 ms emulated

	void setupRate(double rate)
	{
		m_hostRate = rate > 0.0 ? rate : 48000.0;
		for (DacStage &d : m_dac)
		{
			d.setup(kCoreRate, m_hostRate, 8192);
			d.setImages(m_outputStage);
		}
		m_publishPeriod = uint32_t(m_hostRate / kPanelRefreshHz);
		const uint32_t worst = m_dac[0].inputsFor(8192) + 16;
		for (auto &b : m_coreBuf)
			b.assign(worst, 0.0f);
		setLatency(m_dac[0].latency());
	}

	void tickKeys(uint32_t coreFrames)
	{
		for (unsigned k = 0; k < kNumKeys; k ++)
		{
			const uint32_t seq = m_keySeq[k].load();
			if (!m_keyHeld[k] && seq != m_keySeen[k])
			{
				m_keySeen[k] = seq;
				m_keyHeld[k] = true;
				m_keyHeldFrames[k] = 0;
				m_core.setButton(Button(kKeyBits[k]), true);
				if (s_debug)
					d_stdout("Ostrich 220: machine key %u pressed", k);
				continue;
			}
			if (m_keyHeld[k])
			{
				m_keyHeldFrames[k] += coreFrames;
				if (!m_keyDown[k].load() && m_keyHeldFrames[k] >= kKeyHoldFrames)
				{
					m_keyHeld[k] = false;
					m_core.setButton(Button(kKeyBits[k]), false);
					if (s_debug)
					{
						char r0[kLcdColumns + 1], r1[kLcdColumns + 1];
						m_core.lcdText(r0, r1);
						d_stdout("Ostrich 220: machine key %u released, LCD [%s][%s]", k, r0, r1);
					}
				}
			}
		}
	}

	// ---- SysEx files -------------------------------------------------------------------

	// From the panel: "<n> save <path>" or "<n> load <path>"; the counter makes the same
	// file twice two requests.  The work runs on a thread of its own, on a copy of the
	// machine and as fast as the emulation goes, so the one playing is not held up.
	void requestSysex(const char *value)
	{
		unsigned n = 0;
		char verb[8] = {};
		int used = 0;
		if (std::sscanf(value, "%u %7s %n", &n, verb, &used) < 2 || used == 0 || value[used] == '\0')
			return;
		const std::string path = value + used;
		const bool save = std::strcmp(verb, "save") == 0;
		if (!save && std::strcmp(verb, "load") != 0)
			return;
		if (!m_romsLoaded)
		{
			setStatus("SysEx: no machine to work with -- the ROM images are missing.");
			return;
		}
		if (m_sysexBusy.exchange(true))
		{
			setStatus("SysEx: still busy with the last file.");
			return;
		}
		if (m_sysexThread.joinable())
			m_sysexThread.join();
		m_sysexThread = std::thread([this, save, path] {
			if (save)
				saveSysex(path);
			else
				loadSysex(path);
			m_sysexBusy.store(false);
		});
	}

	// The machine as it is now -- its memory -- in a second core, booted.
	std::unique_ptr<U220Core> cloneMachine()
	{
		auto c = std::make_unique<U220Core>();
		c->loadProgramRom(m_romImages[kSlotProgram].data(), m_romImages[kSlotProgram].size());
		for (unsigned w = 0; w < kNumWaveRoms; w ++)
			c->loadWaveRom(w, m_romImages[w].data(), m_romImages[w].size());
		std::vector<uint8_t> st;
		{
			std::lock_guard<std::mutex> lock(m_machine);
			st.resize(m_core.saveState(nullptr, 0));
			m_core.saveState(st.data(), st.size());
		}
		c->loadState(st.data(), st.size());
		return c;
	}

	// What the unit sends for Data > Bulk > All, keys pressed on the copy and its MIDI OUT
	// written to the file as it came: the dump is the firmware's own.
	void saveSysex(const std::string &path)
	{
		setStatus("SysEx: making a bulk dump (Data > Bulk > All) ...");
		auto c = cloneMachine();
		std::vector<uint8_t> out;
		auto run = [&](double seconds) {
			c->runUntilIdle(uint32_t(seconds * 1000.0));
			uint8_t buf[4096];
			while (const size_t got = c->midiOut(buf, sizeof(buf), nullptr))
				out.insert(out.end(), buf, buf + got);
		};
		auto press = [&](Button b) {
			c->setButton(b, true);
			run(0.2);
			c->setButton(b, false);
			run(0.3);
			if (s_debug)
			{
				char r0[kLcdColumns + 1], r1[kLcdColumns + 1];
				c->lcdText(r0, r1);
				d_stdout("Ostrich 220: SysEx copy, key %d, LCD [%s][%s]", int(b), r0, r1);
			}
		};
		auto title = [&]() {
			char r0[kLcdColumns + 1], r1[kLcdColumns + 1];
			c->lcdText(r0, r1);
			std::string t(r0);
			while (!t.empty() && t.back() == ' ')
				t.pop_back();
			return t;
		};
		// Just after power-on the firmware takes no keys yet: wait, and press DATA until the
		// display says so.  The menus remember their last choice: to the leftmost first,
		// then counted.
		run(1.0);
		for (int i = 0; i < 4 && title() != "Data"; i ++)
			press(kButtonData);
		for (int i = 0; i < 3; i ++)
			press(kButtonCursorLeft);
		press(kButtonCursorRight);
		press(kButtonEnter);
		for (int i = 0; i < 3; i ++)
			press(kButtonCursorLeft);
		press(kButtonEnter);
		if (title() != "Data/Bulk/All")
		{
			setStatus("SysEx: the bulk dump menu did not open as expected (display: " + title() + "), nothing saved.");
			return;
		}
		out.clear();
		press(kButtonEnter);
		// Until the line has been quiet for two seconds: the dump has pauses of its own.
		size_t last = 0;
		int quiet = 0;
		for (int i = 0; i < 240 && quiet < 4; i ++)
		{
			run(0.5);
			quiet = (!out.empty() && out.size() == last) ? quiet + 1 : 0;
			last = out.size();
		}
		const size_t packets = parseDump(out).size();
		if (packets == 0)
		{
			setStatus("SysEx: the machine sent no bulk dump, nothing saved.");
			return;
		}
		FILE *f = nullptr;
#ifdef _WIN32
		f = _wfopen(fs::u8path(path).c_str(), L"wb");
#else
		f = std::fopen(path.c_str(), "wb");
#endif
		if (f == nullptr || std::fwrite(out.data(), 1, out.size(), f) != out.size())
		{
			if (f)
				std::fclose(f);
			setStatus("SysEx: cannot write " + path);
			return;
		}
		std::fclose(f);
		setStatus("SysEx: saved the bulk dump, " + std::to_string(packets) + " packets, "
				+ std::to_string(out.size()) + " bytes, to " + path);
	}

	// A .syx into the copy's MIDI IN, at the wire's pace with a gap after each message
	// as a sequencer would leave, then the copy's memory into the machine (a reboot, as
	// after a session load).  What the unit takes from MIDI it takes here; what it does
	// not -- another device ID, Rx SysEx off -- changes nothing, and the status says so.
	void loadSysex(const std::string &path)
	{
		const auto msgs = parseDump(readFile(fs::u8path(path), 0x200000));
		if (msgs.empty())
		{
			setStatus("SysEx: " + path + " holds no Roland SysEx with a good checksum.");
			return;
		}
		setStatus("SysEx: sending " + std::to_string(msgs.size()) + " packets ...");
		auto c = cloneMachine();
		double t = c->emulatedSeconds() + 1.0;                  // just booted: let it settle
		for (const auto &m : msgs)
		{
			c->midiInAtTime(m.data(), m.size(), t);
			t += double(m.size()) * 10.0 / 31250.0 + 0.020;     // wire time + 20 ms
		}
		// The unit says "Receiving SysEx." while it takes data: that is the sign it did.
		bool received = false;
		while (c->emulatedSeconds() < t + 1.0)
		{
			c->runUntilIdle(100);
			char r0[kLcdColumns + 1], r1[kLcdColumns + 1];
			c->lcdText(r0, r1);
			received = received || std::strstr(r0, "Receiving") || std::strstr(r1, "Receiving");
		}
		if (!received)
		{
			setStatus("SysEx: the machine took nothing from " + path + " (Rx SysEx off, or another device ID?).");
			return;
		}
		std::vector<uint8_t> after(c->saveState(nullptr, 0));
		c->saveState(after.data(), after.size());
		{
			std::lock_guard<std::mutex> lock(m_machine);
			m_core.loadState(after.data(), after.size());
			m_needFirstBoot = false;
		}
		setStatus("SysEx: loaded " + std::to_string(msgs.size()) + " packets from " + path);
	}

	// ---- PCM cards ---------------------------------------------------------------------

	// A card file for a slot, from the panel or the session; "" takes the card out.  Not
	// the audio thread: it reads the file.  A slot that holds a card is emptied first and
	// the new one goes in a moment later (tickCards), so that the firmware sees the change.
	void setCard(unsigned slot, const char *value)
	{
		const std::string path = value;
		{
			std::lock_guard<std::mutex> lock(m_cardLock);
			if (path == m_cardPath[slot] && m_cardSettled[slot])
				return;                     // the session or the panel saying it again
			m_cardPath[slot] = path;
			m_cardSettled[slot] = true;
		}

		std::vector<uint8_t> prepared;
		std::string info;
		if (!path.empty())
		{
			const fs::path p = fs::u8path(path);
			const std::string name = pstr(p.filename());
			const std::vector<uint8_t> raw = readFile(p);
			std::error_code ec;
			if (raw.empty() && !fs::exists(p, ec))
				info = std::string("err") + kCardInfoFieldSep + "File not found" + kCardInfoFieldSep + name;
			else if (raw.empty() || raw.size() > kCardBytes || U220Core::prepareCard(raw.data(), raw.size(), prepared) != LoadResult::Ok)
			{
				info = std::string("err") + kCardInfoFieldSep + "Not a PCM card" + kCardInfoFieldSep + name;
				prepared.clear();
			}
			else
			{
				// The header is plain text in a dump: "RolandU-110 N", the card's name at
				// 0x10, its ID at 0x20 -- what the firmware reads, through its tables.
				std::string title(reinterpret_cast<const char *>(raw.data()) + 0x10, 16);
				while (!title.empty() && (title.back() == ' ' || title.back() == '\0'))
					title.pop_back();
				const char *sub = raw.size() > 0x20 ? cardTitle(raw[0x20]) : nullptr;
				info = std::string("ok") + kCardInfoFieldSep + title + kCardInfoFieldSep + (sub ? sub : name.c_str());
			}
		}

		{
			std::lock_guard<std::mutex> lock(m_machine);
			m_cardInsertPending[slot] = false;
			if (m_core.cardPresent(slot))
			{
				m_core.ejectCard(slot);
				m_cardInsertWait[slot] = kCardSwapFrames;
			}
			else
				m_cardInsertWait[slot] = 0;
			if (!prepared.empty())
			{
				m_cardImage[slot].swap(prepared);
				if (m_cardInsertWait[slot] == 0)
					m_core.installCard(slot, m_cardImage[slot].data());
				else
					m_cardInsertPending[slot] = true;
			}
		}

		std::lock_guard<std::mutex> lock(m_cardLock);
		m_cardInfo[slot] = info;
		m_cardInfoSent = cardInfoText();
		m_cardInfoPending.store(true);
		d_stdout("Ostrich 220: card slot %u: %s", slot + 1, path.empty() ? "empty" : path.c_str());
	}

	// The audio thread, under m_machine: a card waiting for its slot to have been seen empty.
	void tickCards(uint32_t coreFrames)
	{
		for (unsigned s = 0; s < kCardSlots; s ++)
		{
			if (!m_cardInsertPending[s])
				continue;
			if (m_cardInsertWait[s] > coreFrames)
			{
				m_cardInsertWait[s] -= coreFrames;
				continue;
			}
			m_core.installCard(s, m_cardImage[s].data());       // one memcpy of 512 KB
			m_cardInsertPending[s] = false;
		}
	}

	// Under m_cardLock.
	std::string cardInfoText() const
	{
		std::string t;
		for (unsigned s = 0; s < kCardSlots; s ++)
			t += (s ? std::string(1, kCardInfoSlotSep) : std::string()) + m_cardInfo[s];
		return t;
	}

	// ---- MIDI OUT ----------------------------------------------------------------------

	// What the firmware sent during this block, as the host's MIDI events: the byte stream
	// of the jack (running status, SysEx, real-time bytes anywhere) cut into messages.  A
	// SysEx message spans blocks as it does the wire; it goes out in the block it ends in.
	void sendMidiOut(uint32_t frames)
	{
		m_sysexUsed = 0;
		for (;;)
		{
			const size_t n = m_core.midiOut(m_outBytes, sizeof(m_outBytes), m_outAt);
			for (size_t i = 0; i < n; i ++)
			{
				uint32_t frame = uint32_t(double(m_outAt[i]) * m_hostRate / kCoreRate);
				if (frame >= frames)
					frame = frames ? frames - 1 : 0;
				midiOutByte(m_outBytes[i], frame);
			}
			if (n < sizeof(m_outBytes))
				break;
		}
	}

	void midiOutByte(uint8_t b, uint32_t frame)
	{
		if (b >= 0xF8)                              // real-time: on its own, anywhere
		{
			emitMidi(&b, 1, frame);
			return;
		}
		if (b == 0xF0)
		{
			m_sysex[0] = b;
			m_sysexLen = 1;
			m_running = 0;
			m_msgLen = 0;
			return;
		}
		if (m_sysexLen)
		{
			if (b & 0x80)                           // F7, or a status byte cutting it short
			{
				if (b == 0xF7 && m_sysexLen < sizeof(m_sysex))
				{
					m_sysex[m_sysexLen ++] = b;
					emitMidi(m_sysex, m_sysexLen, frame);
				}
				m_sysexLen = 0;                     // otherwise cut short or too long: dropped
				if (b == 0xF7)
					return;
			}
			else
			{
				if (m_sysexLen < sizeof(m_sysex))
					m_sysex[m_sysexLen] = b;
				m_sysexLen ++;                      // past the buffer: dropped at the end
				return;
			}
		}
		if (b == 0xF7)                              // an end with no start
			return;
		if (b & 0x80)
		{
			m_msg[0] = b;
			m_msgLen = 1;
			m_msgNeed = midiMessageLength(b);
			m_running = b < 0xF0 ? b : 0;           // system common cancels running status
		}
		else if (m_msgLen == 0)
		{
			if (!m_running)
				return;                             // data with no status: noise on the line
			m_msg[0] = m_running;
			m_msg[1] = b;
			m_msgLen = 2;
			m_msgNeed = midiMessageLength(m_running);
		}
		else if (m_msgLen < 3)
			m_msg[m_msgLen ++] = b;
		if (m_msgLen == m_msgNeed)
		{
			emitMidi(m_msg, m_msgLen, frame);
			m_msgLen = 0;
		}
	}

	static uint8_t midiMessageLength(uint8_t status)
	{
		switch (status & 0xF0)
		{
		case 0xC0: case 0xD0: return 2;
		case 0xF0:
			return status == 0xF2 ? 3 : (status == 0xF1 || status == 0xF3) ? 2 : 1;
		default: return 3;
		}
	}

	void emitMidi(const uint8_t *data, uint32_t size, uint32_t frame)
	{
		MidiEvent ev;
		ev.frame = frame;
		ev.size = size;
		if (size <= MidiEvent::kDataSize)
		{
			std::memcpy(ev.data, data, size);
			ev.dataExt = nullptr;
		}
		else
		{
			// The host reads the bytes when the block is done: one copy each, kept till then.
			if (m_sysexUsed + size > sizeof(m_sysexArena))
				return;
			std::memcpy(m_sysexArena + m_sysexUsed, data, size);
			ev.dataExt = m_sysexArena + m_sysexUsed;
			m_sysexUsed += size;
		}
		writeMidiEvent(ev);
	}

	void publishPanel(uint32_t frames)
	{
		m_publishAccum += frames;
		if (m_publishAccum < m_publishPeriod)
			return;
		m_publishAccum = 0;

		PanelBlob blob {};
		if (m_romsLoaded)
		{
			PanelState st;
			m_core.snapshot(st);
			std::memcpy(blob.lcd, st.lcd, sizeof(blob.lcd));
			std::memcpy(blob.cgram, st.cgram, sizeof(blob.cgram));
			blob.cursor_pos = st.cursor_pos;
			blob.cursor_flags = st.cursor_flags;
			blob.flags = 1;
		}
		else
		{
			const char *l0 = "  ROM images missing    ";
			const char *l1 = "  see status line below ";
			std::memcpy(blob.lcd, l0, 24);
			std::memcpy(blob.lcd + 24, l1, 24);
			blob.cursor_pos = 0xff;
		}
		// Sent when it changed, and once a second regardless: the queue to the UI drops a
		// push when it is full (the host not idling the view for a while), and a display
		// that then stood still would show the dropped state's predecessor for good.
		if (std::memcmp(&blob, &m_lastBlob, sizeof(blob)) != 0 || ++m_publishesSame >= kPanelRefreshHz)
		{
			encodeHex(reinterpret_cast<const uint8_t *>(&blob), sizeof(blob), m_blobHex);
			if (updateStateValue("panel", m_blobHex))
			{
				m_lastBlob = blob;
				m_publishesSame = 0;
			}
		}
	}

	void setStatus(const std::string &s)
	{
		{
			std::lock_guard<std::mutex> lock(m_statusLock);
			m_status = s;
			m_statusPending.store(true);
		}
		d_stdout("Ostrich 220: %s", s.c_str());
	}

	// ---- ROMs ------------------------------------------------------------------------

	void loadRoms()
	{
		std::vector<uint8_t> images[7];
		std::string where;
		for (const fs::path &dir : romDirs())
		{
			std::error_code ec;
			if (!fs::is_directory(dir, ec))
				continue;
			for (const auto &e : fs::directory_iterator(dir, ec))
			{
				std::error_code ec2;
				const auto sz = e.is_regular_file(ec2) ? e.file_size(ec2) : 0;
				if (ec2 || (sz != kProgramRomBytes && sz != kWaveRomBytes))
					continue;
				std::vector<uint8_t> d = readFile(e.path());
				const std::string h = sha1(d);
				for (const KnownImage &k : kKnown)
					if (h == k.sha1 && images[k.slot].empty())
					{
						images[k.slot] = std::move(d);
						if (where.empty())
							where = pstr(dir);
						break;
					}
			}
			// The factory dump may sit next to the ROMs, or in a sysex folder beside them.
			for (const fs::path &sd : { dir, dir.parent_path() / "sysex" })
				for (const auto &e : fs::directory_iterator(sd, ec))
				{
					std::string n = pstr(e.path().filename());
					for (char &c : n) c = char(std::tolower(static_cast<unsigned char>(c)));
					if (m_factoryPath.empty() && n.rfind("u220factory", 0) == 0
					    && pstr(e.path().extension()).size() == 4
					    && (n.size() > 4 && n.compare(n.size() - 4, 4, ".syx") == 0))
						m_factoryPath = e.path();
				}
		}

		std::string missing;
		for (int s = 0; s < 7; s ++)
			if (images[s].empty())
				missing += std::string(missing.empty() ? "" : ", ") + kSlotNames[s];
		if (!missing.empty())
		{
			std::string dirs;
			for (const fs::path &d : romDirs())
				dirs += (dirs.empty() ? "" : "  |  ") + pstr(d);
			setStatus("Missing: " + missing + ".  Put the U-220 ROM images (any file names) into: " + dirs);
			return;
		}

		m_core.loadProgramRom(images[kSlotProgram].data(), images[kSlotProgram].size());
		for (unsigned w = 0; w < kNumWaveRoms; w ++)
			m_core.loadWaveRom(w, images[w].data(), images[w].size());
		m_core.reset();
		for (int s = 0; s < 7; s ++)
			m_romImages[s] = std::move(images[s]);
		m_romsLoaded = true;
		setStatus("ROMs from " + where + (m_factoryPath.empty()
				? ".  No U220Factory.syx found: a new instance starts with empty memory."
				: ".  Factory data: " + pstr(m_factoryPath)));
	}

	/// Power on a machine with no memory, as the factory would have: answer "Memory
	/// Initialize" with ENTER, then send the factory dump and select P-01.  All in emulated
	/// time, run flat out, so the user sees a ready machine and not the procedure.
	void firstBoot()
	{
		m_needFirstBoot = false;
		m_core.reset();
		m_core.runUntilIdle(3000);
		if (!m_core.nvramBlank() && m_factoryPath.empty())
			return;

		auto press = [&](Button b) {
			m_core.setButton(b, true);
			m_core.runUntilIdle(200);
			m_core.setButton(b, false);
			m_core.runUntilIdle(800);
		};
		press(kButtonEnter);
		m_core.runUntilIdle(2000);

		if (m_factoryPath.empty())
			return;
		const auto msgs = parseDump(readFile(m_factoryPath, 0x100000));
		if (msgs.empty())
		{
			setStatus("Factory data not loaded: " + pstr(m_factoryPath) + " holds no valid U-220 SysEx.");
			return;
		}
		double t = m_core.emulatedSeconds() + 0.2;
		for (const auto &m : msgs)
		{
			m_core.midiInAtTime(m.data(), m.size(), t);
			t += double(m.size()) * 10.0 / 31250.0 + 0.020;     // wire time + 20 ms
		}
		const uint8_t selectP01[] = { 0xCF, 0x00 };
		m_core.midiInAtTime(selectP01, 2, t + 1.0);
		const double until = t + 2.5 - m_core.emulatedSeconds();
		m_core.runUntilIdle(uint32_t(until * 1000.0));
		setStatus("Factory data loaded (" + std::to_string(msgs.size()) + " packets) from " + pstr(m_factoryPath));
	}

	// ---- members ---------------------------------------------------------------------

	mutable std::mutex m_machine;
	U220Core m_core;
	U220Fx m_fx;                    // reverb/delay and chorus, tuned to Helge's U-220 (u220_fx.h)
	bool m_romsLoaded = false;
	bool m_needFirstBoot = true;
	fs::path m_factoryPath;

	std::string m_status;
	mutable std::mutex m_statusLock;
	std::atomic<bool> m_statusPending { false };
	std::string m_uiSize;           // "<w> <h>", the panel's, saved with the session
	mutable std::mutex m_uiSizeLock;

	double m_hostRate = 48000.0;
	// MIX OUT L/R, DIRECT OUT 1 L/R, DIRECT OUT 2 L/R, as the outputs.
	DacStage m_dac[6];              // 32 kHz -> host, with the DAC's images (DacStage.hpp)
	std::vector<float> m_coreBuf[6];
	static constexpr uint32_t kGroupDirect1 = 0, kGroupDirect2 = 1;

	float m_volumeDb = 0.0f, m_gainTarget = 1.0f, m_gain = 1.0f;
	bool m_outputStage = true;
	bool m_directOuts = true;

	std::atomic<bool> m_keyDown[kNumKeys] {};
	std::atomic<uint32_t> m_keySeq[kNumKeys];
	uint32_t m_keySeen[kNumKeys] = {};
	bool m_keyHeld[kNumKeys] = {};
	uint32_t m_keyHeldFrames[kNumKeys] = {};

	uint32_t m_publishPeriod = 1920, m_publishAccum = 0, m_publishesSame = 0;
	PanelBlob m_lastBlob {};
	char m_blobHex[sizeof(PanelBlob) * 2 + 1] = {};

	// SysEx files (requestSysex).  The ROM images stay for the machine's copies.
	std::vector<uint8_t> m_romImages[7];
	std::thread m_sysexThread;
	std::atomic<bool> m_sysexBusy { false };

	// PCM cards.  Paths and what the panel shows under m_cardLock; the images and the
	// insertion waiting for its moment under m_machine.
	static constexpr uint32_t kCardSwapFrames = kCoreSampleRate / 5;   // 200 ms emulated
	mutable std::mutex m_cardLock;
	std::string m_cardPath[kCardSlots], m_cardInfo[kCardSlots], m_cardInfoSent;
	bool m_cardSettled[kCardSlots] = {};
	std::atomic<bool> m_cardInfoPending { false };
	std::vector<uint8_t> m_cardImage[kCardSlots];
	bool m_cardInsertPending[kCardSlots] = {};
	uint32_t m_cardInsertWait[kCardSlots] = {};

	// MIDI OUT.  A U-220 packet is at most 256 data bytes plus 10 of frame (Roland DT1); at
	// 31250 baud a block of 8192 frames at 44.1 kHz carries ~580 bytes.
	uint8_t m_outBytes[512];
	uint32_t m_outAt[512];
	uint8_t m_msg[3] = {}, m_msgLen = 0, m_msgNeed = 0, m_running = 0;
	uint8_t m_sysex[1024];
	size_t m_sysexLen = 0;
	uint8_t m_sysexArena[8192];
	size_t m_sysexUsed = 0;


	DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OstrichPlugin)
};

Plugin *createPlugin()
{
	return new OstrichPlugin();
}

END_NAMESPACE_DISTRHO
