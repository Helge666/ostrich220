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
	env("OSTRICH_ROMS", "");
#ifdef _WIN32
	env("PROGRAMDATA", "Ostrich/roms");
	env("LOCALAPPDATA", "Ostrich/roms");
	env("USERPROFILE", "Documents/Ostrich/roms");
#else
	env("XDG_DATA_HOME", "Ostrich/roms");
	env("HOME", ".local/share/Ostrich/roms");
	dirs.push_back("/usr/share/Ostrich/roms");
#endif
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
		m_dbgThread = std::thread([this] { debugLoop(); });
	}

	~OstrichPlugin() override
	{
		m_dbgStop.store(true);
		if (m_dbgThread.joinable())
			m_dbgThread.join();
	}

protected:
	const char *getLabel() const override { return "Ostrich"; }
	const char *getMaker() const override { return "Ostrich project"; }
	const char *getLicense() const override { return "GPL-3.0-or-later"; }
	const char *getHomePage() const override { return "https://example.invalid/ostrich"; }
	uint32_t getVersion() const override { return d_version(0, 1, 0); }
	int64_t getUniqueId() const override { return d_cconst('U', '2', '2', '0'); }
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
			m_dac[0].setImages(m_outputStage);
			m_dac[1].setImages(m_outputStage);
			return;
		}
		const uint32_t k = index - kParamKeyFirst;
		if (k < kNumKeys)
			setKey(k, value > 0.5f);
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
		}
		state.defaultValue = "";
	}

	String getState(const char *key) const override
	{
		if (std::strcmp(key, "status") == 0)
			return String(m_status.c_str());
		if (std::strcmp(key, "uisize") == 0)
		{
			std::lock_guard<std::mutex> lock(m_uiSizeLock);
			return String(m_uiSize.c_str());
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
				m_dbgKeyMsgs.fetch_add(1);
				if (s_debug)
					d_stdout("Ostrich: key %u %s (state)", k, down ? "down" : "up");
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
		if (std::strcmp(key, "nvram") != 0 || value[0] == '\0')
			return;
		std::vector<uint8_t> raw(std::strlen(value) / 2);
		if (!decodeHex(value, raw.data(), raw.size()))
			return;
		std::lock_guard<std::mutex> lock(m_machine);
		if (m_romsLoaded && m_core.loadState(raw.data(), raw.size()))
		{
			m_needFirstBoot = false;
			d_stdout("Ostrich: restored the battery-backed memory from the session.");
		}
	}

	// ---- audio ---------------------------------------------------------------------------

	void sampleRateChanged(double newRate) override { setupRate(newRate); }

	void activate() override
	{
		m_dac[0].reset();
		m_dac[1].reset();
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

		if (m_statusPending.exchange(false))
			updateStateValue("status", m_status.c_str());

		// setState() reboots the machine on another thread.  Never wait for it here.
		std::unique_lock<std::mutex> lock(m_machine, std::try_to_lock);
		m_dbgRuns.fetch_add(1, std::memory_order_relaxed);
		if (!lock.owns_lock() || !m_romsLoaded || m_needFirstBoot)
		{
			m_dbgIdleRuns.fetch_add(1, std::memory_order_relaxed);
			std::memset(outL, 0, sizeof(float) * frames);
			std::memset(outR, 0, sizeof(float) * frames);
			if (!m_romsLoaded)
				publishPanel(frames);
			return;
		}

		const uint32_t coreFrames = m_dac[0].inputsFor(frames);
		tickKeys(coreFrames);

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

		if (m_coreL.size() < coreFrames)
		{
			// Only on a host block larger than any seen so far, so practically never.
			m_coreL.resize(coreFrames);
			m_coreR.resize(coreFrames);
		}
		m_dbgMidi.fetch_add(midiEventCount, std::memory_order_relaxed);
		m_core.renderStereo(m_coreL.data(), m_coreR.data(), coreFrames);
		m_dbgEmu.store(m_core.emulatedSeconds(), std::memory_order_relaxed);
		m_dac[0].process(m_coreL.data(), coreFrames, outL, frames);
		m_dac[1].process(m_coreR.data(), coreFrames, outR, frames);

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
		m_dac[0].setup(kCoreRate, m_hostRate, 8192);
		m_dac[1].setup(kCoreRate, m_hostRate, 8192);
		m_dac[0].setImages(m_outputStage);
		m_dac[1].setImages(m_outputStage);
		m_publishPeriod = uint32_t(m_hostRate / kPanelRefreshHz);
		const uint32_t worst = m_dac[0].inputsFor(8192) + 16;
		m_coreL.assign(worst, 0.0f);
		m_coreR.assign(worst, 0.0f);
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
				m_dbgPresses.fetch_add(1, std::memory_order_relaxed);
				if (s_debug)
					d_stdout("Ostrich: machine key %u pressed", k);
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
						d_stdout("Ostrich: machine key %u released, LCD [%s][%s]", k, r0, r1);
					}
				}
			}
		}
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
				m_dbgPanelOk.fetch_add(1, std::memory_order_relaxed);
			}
			else
				m_dbgPanelFail.fetch_add(1, std::memory_order_relaxed);
			std::memcpy(m_dbgLcd, blob.lcd, sizeof(m_dbgLcd));
		}
	}

	void setStatus(const std::string &s)
	{
		m_status = s;
		m_statusPending.store(true);
		d_stdout("Ostrich: %s", s.c_str());
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

	// ---- diagnostics (the panel freeze, ROADMAP.md Phase 4) ---------------------------

	// Once a second: what the machine shows and how far each link of DSP -> UI got.
	// Counters only on the audio side; the file is written here.
	void debugLoop()
	{
		char line[400], lcd[56];
		uint8_t raw[48];
		uint64_t lastRuns = 0;
		while (!m_dbgStop.load())
		{
			for (int i = 0; i < 10 && !m_dbgStop.load(); i ++)
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			std::memcpy(raw, m_dbgLcd, sizeof(raw));
			lcdText(raw, lcd);
			const uint64_t runs = m_dbgRuns.load();
			std::snprintf(line, sizeof(line),
					"dsp %p runs %llu (+%llu, idle %llu) emu %.1f s midi %llu keymsgs %llu presses %llu panel ok %llu fail %llu lcd %s",
					(void *)this, (unsigned long long)runs, (unsigned long long)(runs - lastRuns),
					(unsigned long long)m_dbgIdleRuns.load(), m_dbgEmu.load(),
					(unsigned long long)m_dbgMidi.load(), (unsigned long long)m_dbgKeyMsgs.load(),
					(unsigned long long)m_dbgPresses.load(), (unsigned long long)m_dbgPanelOk.load(),
					(unsigned long long)m_dbgPanelFail.load(), lcd);
			lastRuns = runs;
			debugLog("ostrich-dsp", line);
		}
	}

	// ---- members ---------------------------------------------------------------------

	mutable std::mutex m_machine;
	U220Core m_core;
	U220Fx m_fx;                    // reverb/delay and chorus, tuned to Helge's U-220 (u220_fx.h)
	bool m_romsLoaded = false;
	bool m_needFirstBoot = true;
	fs::path m_factoryPath;

	std::string m_status;
	std::atomic<bool> m_statusPending { false };
	std::string m_uiSize;           // "<w> <h>", the panel's, saved with the session
	mutable std::mutex m_uiSizeLock;

	double m_hostRate = 48000.0;
	DacStage m_dac[2];              // 32 kHz -> host, with the DAC's images (DacStage.hpp)
	std::vector<float> m_coreL, m_coreR;

	float m_volumeDb = 0.0f, m_gainTarget = 1.0f, m_gain = 1.0f;
	bool m_outputStage = true;

	std::atomic<bool> m_keyDown[kNumKeys] {};
	std::atomic<uint32_t> m_keySeq[kNumKeys];
	uint32_t m_keySeen[kNumKeys] = {};
	bool m_keyHeld[kNumKeys] = {};
	uint32_t m_keyHeldFrames[kNumKeys] = {};

	uint32_t m_publishPeriod = 1920, m_publishAccum = 0, m_publishesSame = 0;
	PanelBlob m_lastBlob {};
	char m_blobHex[sizeof(PanelBlob) * 2 + 1] = {};

	std::thread m_dbgThread;
	std::atomic<bool> m_dbgStop { false };
	std::atomic<uint64_t> m_dbgRuns { 0 }, m_dbgIdleRuns { 0 }, m_dbgMidi { 0 }, m_dbgKeyMsgs { 0 },
			m_dbgPresses { 0 }, m_dbgPanelOk { 0 }, m_dbgPanelFail { 0 };
	std::atomic<double> m_dbgEmu { 0.0 };
	uint8_t m_dbgLcd[48] = {};

	DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OstrichPlugin)
};

Plugin *createPlugin()
{
	return new OstrichPlugin();
}

END_NAMESPACE_DISTRHO
