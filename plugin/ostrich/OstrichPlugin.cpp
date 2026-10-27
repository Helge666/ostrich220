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
#include "ZipReader.hpp"

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

// The CRC32s are what a zip lists for each entry (mame/src/mame/roland/roland_u220.cpp has
// both): a zip is searched by them without unpacking anything else.
struct KnownImage { const char *sha1; uint32_t crc; int slot; const char *what; };
constexpr int kSlotProgram = 6;             // 0..5 = wave ROMs A..F
const KnownImage kKnown[] = {
	{ "9d72b1688a173505b5e1c5ddac01efa78ee76c7f", 0xd5492e9b, kSlotProgram, "firmware v1.02" },
	{ "d0b66c0ea0e3af284a1806226aed79d8da2f3dd4", 0x893afa9c, kSlotProgram, "firmware v1.01" },
	{ "e1c6f1ee29cebe8663cb0c6989b8faebb3d841fd", 0x82abd055, kSlotProgram, "firmware v1.00" },
	{ "55d36199ea489969a729782afe4cbff5b1cede0e", 0xd5475560, 0, "wave ROM A (IC19)" },
	{ "8b8067fbc6fa7b2e8694a3785f123627ea458d1f", 0x43a5b26c, 1, "wave ROM B (IC20)" },
	{ "8dd9826d7d3e67b9a568eadeaaf7856a05f3e068", 0xe79ee88a, 2, "wave ROM C (IC21)" },
	{ "35b98a0fb5cf643fb7b761a74581486eee3bd8c3", 0xda9fbf6d, 3, "wave ROM D (IC22)" },
	{ "71b6f97df6dd4e573db5743c10c77e86d67965a3", 0x104f3974, 4, "wave ROM E (IC23)" },
	{ "aea715a0a554c071a2e912894805a8d819802e1d", 0xaf1c2778, 5, "wave ROM F (IC24)" },
};
// Card dumps known to be good: MAME's software list (mame/hash/u110_card.xml).  A dump
// that matches shows its card's number and name; any other still goes in, by its header.
struct KnownCard { const char *sha1; uint32_t crc; const char *id; const char *title; };
const KnownCard kKnownCards[] = {
	{ "6d3f17c44f42b79401416910be624b65b4db8e33", 0x12a18917, "Musitronics 1", "Akkordeon (U-25)" },
	{ "9f2dced30b9bcc9c4ae841661ee334f1daa6dd68", 0x90ea2ea8, "SN-MV30-01", "Rhythm Section (U-31)" },
	{ "bbe0b2aa7badcc5061b648745f1bd957771f134b", 0x57c740c9, "SN-MV30-02", "Orchestral (U-30)" },
	{ "c248ba8fcfe79c83339132a855dfe047bfa79983", 0xe51284a1, "SN-SPLA-01", "Sound Elements Vol. 1 (U-01)" },
	{ "b57b215828d292505c4d5c3e85aefb3e34086514", 0x8a99cfce, "SN-U110-01", "Pipe Organ & Harpsichord" },
	{ "5e4edc717adcf7e078ea4bc1ce3bd04eacece672", 0x7f347263, "SN-U110-02", "Latin & F.X. Percussions" },
	{ "1cb93d7bfeb499f22cba316a5732af97d7311a4e", 0x7f14c801, "SN-U110-03", "Ethnic" },
	{ "a369fbb1874c222a0c9e1066275e3174917b3725", 0x39e0e70b, "SN-U110-04", "Electric Grand & Clavi" },
	{ "b1a59971153e0cc5c4c2631a9bdb57433f47ca0e", 0x844b2456, "SN-U110-05", "Orchestral Strings" },
	{ "02d8dfb9bd45721e34b0eec3db69b3f5e182e7bc", 0xc715c9aa, "SN-U110-06", "Orchestral Winds" },
	{ "1e16c98cf776598b726d4f48ca3f3d677cd32c54", 0x6fb3a4f4, "SN-U110-07", "Electric Guitar" },
	{ "71b6f97df6dd4e573db5743c10c77e86d67965a3", 0x104f3974, "SN-U110-08", "Synthesizer" },
	{ "aea715a0a554c071a2e912894805a8d819802e1d", 0xaf1c2778, "SN-U110-09", "Guitar & Keyboards" },
	{ "3249ff037a165e57173bd45c543a665810d2c57b", 0xc219c27e, "SN-U110-10", "Rock Drums" },
	{ "f5cbe2272d03e21403bfca029476d015f6a34d3a", 0x0ee5274d, "SN-U110-11", "Sound Effects" },
	{ "bac35550bdd719da4e5dc703d3829523cfee9b8a", 0x39c78747, "SN-U110-12", "Sax & Trombone" },
	{ "4a6ca2b45c83991e62023dfc0114b40baed0e0fd", 0x4cbf140a, "SN-U110-13", "Super Strings" },
	{ "dfe87d385068201d3a31a55466e171fda614e412", 0xcf5e6371, "SN-U110-14", "Super Ac Guitar" },
	{ "56918595d39b480a6a15b7ecc8d1c836cca842a3", 0x2df93109, "SN-U110-15", "Super Brass" },
};

const char *const kSlotNames[kRomSlots] ={ "A", "B", "C", "D", "E", "F", "firmware" };

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

bool writeFile(const fs::path &p, const std::vector<uint8_t> &data)
{
	FILE *f = nullptr;
#ifdef _WIN32
	f = _wfopen(p.c_str(), L"wb");
#else
	f = std::fopen(p.c_str(), "wb");
#endif
	if (!f)
		return false;
	const bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
	return std::fclose(f) == 0 && ok;
}

/// Where the user puts the images, most specific first.  On Windows ProgramData comes
/// first: Cubase runs outside the MSIX sandbox some tools live in, and %LOCALAPPDATA% is
/// virtualised there -- a folder made by such a tool is invisible to the host.
std::vector<fs::path> romDirs()
{
	std::vector<fs::path> dirs;
	auto env = [&](const char *name, const char *tail) {
		if (const char *v = std::getenv(name))
			if (*v) dirs.push_back((fs::u8path(v) / fs::u8path(tail)).make_preferred());
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

/// Where images the user picks in the panel are copied to: the first of these that takes
/// them.  romDirs() searches them all.
std::vector<fs::path> installDirs()
{
	std::vector<fs::path> dirs;
	auto env = [&](const char *name, const char *tail) {
		if (const char *v = std::getenv(name))
			if (*v) dirs.push_back((fs::u8path(v) / fs::u8path(tail)).make_preferred());
	};
#ifdef _WIN32
	env("PROGRAMDATA", "Ostrich 220/roms");
	env("LOCALAPPDATA", "Ostrich 220/roms");
	env("USERPROFILE", "Documents/Ostrich 220/roms");
#else
	env("XDG_DATA_HOME", "Ostrich 220/roms");
	env("HOME", ".local/share/Ostrich 220/roms");
#endif
	return dirs;
}

/// A card file's bytes: the file itself, or out of a zip (MAME's software list keeps a card
/// as sn_u110_02.zip) the entry that is a known card, else its only entry of a card's size.
std::vector<uint8_t> readCardFile(const fs::path &p)
{
	std::string ext = pstr(p.extension());
	for (char &c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
	if (ext != ".zip")
		return readFile(p);
	const auto entries = zip::list(p);
	const zip::Entry *pick = nullptr;
	for (const zip::Entry &e : entries)
		for (const KnownCard &k : kKnownCards)
			if (pick == nullptr && e.crc == k.crc)
				pick = &e;
	if (pick == nullptr)
	{
		int sized = 0;
		for (const zip::Entry &e : entries)
			if (e.size > 0 && e.size <= kCardBytes && ++ sized == 1)
				pick = &e;
		if (sized != 1)
			pick = nullptr;
	}
	std::vector<uint8_t> d;
	if (pick == nullptr || !zip::extract(p, *pick, d))
		d.clear();
	return d;
}

/// Whether a file name is a factory dump's, "U220Factory*.syx" in any case.
bool isFactoryName(std::string n)
{
	for (char &c : n) c = char(std::tolower(static_cast<unsigned char>(c)));
	return n.rfind("u220factory", 0) == 0 && n.size() > 4 && n.compare(n.size() - 4, 4, ".syx") == 0;
}

/// What one or more folders hold: the images by slot, the first of each found, and a
/// factory dump.  An image or dump out of a zip has the zip as @c from and its own file
/// name as @c member.
struct RomScan
{
	std::vector<uint8_t> images[kRomSlots];
	fs::path from[kRomSlots];
	std::string member[kRomSlots];
	std::string what[kRomSlots];
	fs::path factory;
	std::string factoryMember;
	std::vector<uint8_t> factoryData;   ///< only for a dump out of a zip
	fs::path where;             ///< the folder the first image came from

	bool complete() const
	{
		for (const auto &i : images)
			if (i.empty()) return false;
		return true;
	}
	bool any() const
	{
		for (const auto &i : images)
			if (!i.empty()) return true;
		return false;
	}
	std::string missing() const
	{
		std::string m;
		for (unsigned s = 0; s < kRomSlots; s ++)
			if (images[s].empty())
				m += std::string(m.empty() ? "" : ", ") + kSlotNames[s];
		return m;
	}

	/// Adds what @p dir holds, by SHA-1 whatever the names: loose files first, then what
	/// zips in it hold (a MAME set).  The factory dump may sit next to the ROMs, in a sysex
	/// folder in or beside them, or in one of the zips.
	void scan(const fs::path &dir)
	{
		std::error_code ec;
		if (!fs::is_directory(dir, ec))
			return;
		std::vector<fs::path> zips;
		for (const auto &e : fs::directory_iterator(dir, ec))
		{
			std::error_code ec2;
			const auto sz = e.is_regular_file(ec2) ? e.file_size(ec2) : 0;
			if (ec2)
				continue;
			std::string ext = pstr(e.path().extension());
			for (char &c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
			if (ext == ".zip")
				zips.push_back(e.path());
			if (sz == kProgramRomBytes || sz == kWaveRomBytes)
				take(readFile(e.path()), e.path(), std::string(), dir);
		}
		for (const fs::path &sd : { dir, dir / "sysex", dir.parent_path() / "sysex" })
			for (const auto &e : fs::directory_iterator(sd, ec))
				if (factory.empty() && isFactoryName(pstr(e.path().filename())))
					factory = e.path();

		std::sort(zips.begin(), zips.end());
		for (const fs::path &z : zips)
		{
			if (complete() && !factory.empty())
				break;
			for (const zip::Entry &e : zip::list(z))
			{
				// Windows PowerShell 5's Compress-Archive writes folders with '\'.
				const size_t cut = e.name.find_last_of("/\\");
				const std::string name = cut == std::string::npos ? e.name : e.name.substr(cut + 1);
				std::vector<uint8_t> d;
				if (factory.empty() && e.size <= 0x100000 && isFactoryName(name) && zip::extract(z, e, d))
				{
					factory = z;
					factoryMember = name;
					factoryData = std::move(d);
					continue;
				}
				// Only an entry a known image could be is unpacked; the SHA-1 decides.
				const bool candidate = std::any_of(std::begin(kKnown), std::end(kKnown), [&](const KnownImage &k) {
					return k.crc == e.crc && images[k.slot].empty()
					    && e.size == (k.slot == kSlotProgram ? kProgramRomBytes : kWaveRomBytes);
				});
				if (candidate && zip::extract(z, e, d))
					take(std::move(d), z, name, dir);
			}
		}
	}

private:
	void take(std::vector<uint8_t> d, const fs::path &path, const std::string &inZip, const fs::path &dir)
	{
		const std::string h = sha1(d);
		for (const KnownImage &k : kKnown)
			if (h == k.sha1 && images[k.slot].empty())
			{
				images[k.slot] = std::move(d);
				from[k.slot] = path;
				member[k.slot] = inZip;
				what[k.slot] = k.slot == kSlotProgram ? std::string(k.what + 9) : "found";
				if (where.empty())
					where = dir;
				return;
			}
	}
};

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
		for (auto &f : m_keyFreeFrames) f = kKeyMaxWaitFrames;     // the first press goes at once
		m_core.setEffects(&m_fx);
		loadRoms();
		setupRate(getSampleRate());
	}

	~OstrichPlugin() override
	{
		if (m_sysexThread.joinable())
			m_sysexThread.join();
		if (m_romThread.joinable())
			m_romThread.join();
	}

protected:
	const char *getLabel() const override { return "Ostrich220"; }
	const char *getMaker() const override { return "Ostrich 220 project"; }
	const char *getLicense() const override { return "GPL-3.0-or-later"; }
	const char *getHomePage() const override { return "https://github.com/Helge666/ostrich220"; }
	uint32_t getVersion() const override { return d_version(0, 1, 1); }
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
		case kStateRomSet:
			state.key = "romset";
			state.label = "ROM images";
			state.hints = kStateIsOnlyForUI;
			break;
		case kStateRomFile:
			// "<n> <path>": see requestRomFile.
			state.key = "romfile";
			state.label = "ROM image file";
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
		if (std::strcmp(key, "romset") == 0)
		{
			std::lock_guard<std::mutex> lock(m_statusLock);
			return String(m_romSet.c_str());
		}
		if (std::strcmp(key, "nvram") != 0)
			return String();
		std::lock_guard<std::mutex> lock(m_machine);
		std::vector<uint8_t> raw;
		if (m_romsLoaded)
		{
			raw.resize(m_core.saveState(nullptr, 0));
			m_core.saveState(raw.data(), raw.size());
		}
		else
			raw = m_pendingNvram;           // saved again as it came, not lost
		if (raw.empty())
			return String();
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
		if (std::strcmp(key, "romfile") == 0)
		{
			if (value[0] != '\0')
				requestRomFile(value);
			return;
		}
		if (std::strcmp(key, "nvram") != 0 || value[0] == '\0')
			return;
		std::vector<uint8_t> raw(std::strlen(value) / 2);
		if (!decodeHex(value, raw.data(), raw.size()))
			return;
		std::lock_guard<std::mutex> lock(m_machine);
		if (!m_romsLoaded)
			m_pendingNvram = std::move(raw);
		else if (m_core.loadState(raw.data(), raw.size()))
		{
			m_needFirstBoot = false;
			d_stdout("Ostrich 220: restored the battery-backed memory from the session.");
		}
	}

	// ---- audio ---------------------------------------------------------------------------

	void sampleRateChanged(double newRate) override { setupRate(newRate); }

	void activate() override
	{
		m_activated = true;
		for (DacStage &d : m_dac)
			d.reset();
		m_gain = m_gainTarget;
		// A new instance with no session behind it: the factory-fresh machine.  Done here
		// and not in run(): it is a few seconds of emulated time, a fraction of one in CPU.
		std::lock_guard<std::mutex> lock(m_machine);
		if (m_romsLoaded && m_needFirstBoot)
			firstBoot();
	}

	void deactivate() override { m_activated = false; }

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
		if (m_romSetPending.load())
		{
			std::unique_lock<std::mutex> sl(m_statusLock, std::try_to_lock);
			if (sl.owns_lock())
			{
				m_romSetPending.store(false);
				updateStateValue("romset", m_romSet.c_str());
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
			m_midiSeen = true;
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
	// tickKeys: between queued presses at least the gap, then the display still for the
	// settle time, or the longest wait.  At most this many presses wait.
	static constexpr uint32_t kKeyGapFrames = kCoreSampleRate / 25;        // 40 ms
	static constexpr uint32_t kKeySettleFrames = kCoreSampleRate * 3 / 25; // 120 ms
	static constexpr uint32_t kKeyMaxWaitFrames = kCoreSampleRate * 3 / 5; // 600 ms
	static constexpr uint32_t kKeyMaxQueued = 16;

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

	// Presses queue up (the wheel over the display, a fast hand on the keyboard), and each
	// is played as a press of its own.  The next waits for the firmware: after a patch change
	// it takes ~0.4 s before it reads the keys again, in a menu much less, so the cue is the
	// display having stood still a moment, not a fixed time (measured with u220_render:
	// ten VALUE presses 125 ms long arrive all only 300 ms apart).
	void tickKeys(uint32_t coreFrames)
	{
		char r0[kLcdColumns + 1], r1[kLcdColumns + 1];
		m_core.lcdText(r0, r1);
		if (std::memcmp(r0, m_keyLcd, kLcdColumns) != 0 || std::memcmp(r1, m_keyLcd + kLcdColumns, kLcdColumns) != 0)
		{
			std::memcpy(m_keyLcd, r0, kLcdColumns);
			std::memcpy(m_keyLcd + kLcdColumns, r1, kLcdColumns);
			m_lcdStillFrames = 0;
		}
		else if (m_lcdStillFrames < kKeySettleFrames)
			m_lcdStillFrames += coreFrames;

		for (unsigned k = 0; k < kNumKeys; k ++)
		{
			const uint32_t seq = m_keySeq[k].load();
			if (!m_keyHeld[k] && m_keyFreeFrames[k] < kKeyMaxWaitFrames)
				m_keyFreeFrames[k] += coreFrames;
			if (!m_keyHeld[k] && seq != m_keySeen[k])
			{
				const bool settled = m_keyFreeFrames[k] >= kKeyGapFrames
					&& (m_lcdStillFrames >= kKeySettleFrames || m_keyFreeFrames[k] >= kKeyMaxWaitFrames);
				if (!settled)
					continue;
				if (seq - m_keySeen[k] > kKeyMaxQueued)
					m_keySeen[k] = seq - kKeyMaxQueued;     // a spun wheel: not minutes of it
				m_keySeen[k] ++;
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
				// Held while the key is down, or the press is not over, or more are queued.
				if ((!m_keyDown[k].load() || m_keySeq[k].load() != m_keySeen[k]) && m_keyHeldFrames[k] >= kKeyHoldFrames)
				{
					m_keyHeld[k] = false;
					m_keyFreeFrames[k] = 0;
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
		if (!m_activated)
			return;                         // not the instance that plays: see m_activated
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
			const std::vector<uint8_t> raw = readCardFile(p);
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
				// A known dump: its card's number, checked.
				const std::string h = sha1(raw);
				for (const KnownCard &k : kKnownCards)
					if (h == k.sha1)
						info = std::string("ok") + kCardInfoFieldSep + k.id + " \xe2\x9c\x93" + kCardInfoFieldSep + k.title;
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
			// MIDI MESSAGE: the firmware has no part in it (nothing it writes follows MIDI
			// IN), so the lamp is the line's.  Lit for one refresh after any MIDI in.
			blob.flags = 1 | (m_midiSeen ? 2 : 0);
			m_midiSeen = false;
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

	/// Looks in the ROM folders and starts the machine when everything is there.  From the
	/// constructor, and again once the panel has copied images in (installRoms).
	bool loadRoms(std::string *missing = nullptr)
	{
		RomScan r;
		for (const fs::path &dir : romDirs())
			r.scan(dir);
		publishRomSet(r);
		if (!r.complete())
		{
			if (missing)
				*missing = r.missing();
			std::string dirs;
			for (const fs::path &d : romDirs())
				dirs += (dirs.empty() ? "" : "  |  ") + pstr(d);
			setStatus("Missing: " + r.missing() + ".  Put the U-220 ROM images (any file names) into: " + dirs);
			return false;
		}

		std::lock_guard<std::mutex> lock(m_machine);
		m_factoryPath = r.factory;
		m_factoryData = std::move(r.factoryData);
		m_core.loadProgramRom(r.images[kSlotProgram].data(), r.images[kSlotProgram].size());
		for (unsigned w = 0; w < kNumWaveRoms; w ++)
			m_core.loadWaveRom(w, r.images[w].data(), r.images[w].size());
		m_core.reset();
		for (unsigned s = 0; s < kRomSlots; s ++)
			m_romImages[s] = std::move(r.images[s]);
		m_romsLoaded = true;
		// A session's memory that came while the images were missing.
		if (!m_pendingNvram.empty() && m_core.loadState(m_pendingNvram.data(), m_pendingNvram.size()))
			m_needFirstBoot = false;
		m_pendingNvram.clear();
		setStatus("ROMs from " + pstr(r.where) + (m_factoryPath.empty()
				? ".  No U220Factory.syx found: a new instance runs Memory Initialize instead."
				: ".  Factory data: " + pstr(m_factoryPath) + (r.factoryMember.empty() ? "" : " (" + r.factoryMember + ")")));
		return true;
	}

	/// What the panel's ROM list shows (OstrichShared.hpp, kStateRomSet).
	void publishRomSet(const RomScan &r)
	{
		std::string t;
		for (unsigned s = 0; s < kRomSlots; s ++)
			t += r.what[s] + kCardInfoFieldSep;
		t += (r.factoryMember.empty() ? pstr(r.factory.filename()) : r.factoryMember) + kCardInfoFieldSep;
		const auto dirs = installDirs();
		t += dirs.empty() ? std::string() : pstr(dirs.front());
		std::lock_guard<std::mutex> lock(m_statusLock);
		m_romSet = t;
		m_romSetPending.store(true);
	}

	// From the panel: "<n> <path>", a file the user picked.  Its folder is searched for all
	// the images, on a thread of its own: SHA-1 over a few MB, then the first boot.
	void requestRomFile(const char *value)
	{
		unsigned n = 0;
		int used = 0;
		if (std::sscanf(value, "%u %n", &n, &used) < 1 || used == 0 || value[used] == '\0' || m_romsLoaded)
			return;
		if (!m_activated)
			return;                         // not the instance that plays: see m_activated
		if (m_romBusy.exchange(true))
			return;
		if (m_romThread.joinable())
			m_romThread.join();
		const fs::path picked = fs::u8path(value + used);
		m_romThread = std::thread([this, picked] {
			installRoms(picked);
			m_romBusy.store(false);
		});
	}

	/// Copies what the picked file's folder holds and the ROM folders lack into the first
	/// install folder that takes it, then looks again.
	void installRoms(const fs::path &picked)
	{
		RomScan have, found;
		for (const fs::path &dir : romDirs())
			have.scan(dir);
		const fs::path dir = picked.parent_path();
		found.scan(dir);
		const std::string pickedName = pstr(picked.filename());

		// What goes into the install folder: a loose file is copied, one out of a zip is
		// written out under its own name -- the folder then holds plain files only.
		struct Copy { fs::path from; std::string name; const std::vector<uint8_t> *data; };
		std::vector<Copy> copy;
		std::string what;
		for (unsigned s = 0; s < kRomSlots; s ++)
			if (have.images[s].empty() && !found.images[s].empty())
			{
				if (found.member[s].empty())
					copy.push_back({ found.from[s], pstr(found.from[s].filename()), nullptr });
				else
					copy.push_back({ found.from[s], found.member[s], &found.images[s] });
				what += std::string(what.empty() ? "" : ", ") + (s == kSlotProgram ? "firmware " + found.what[s] : kSlotNames[s]);
			}
		if (have.factory.empty() && !found.factory.empty())
		{
			if (found.factoryMember.empty())
				copy.push_back({ found.factory, pstr(found.factory.filename()), nullptr });
			else
				copy.push_back({ found.factory, found.factoryMember, &found.factoryData });
			what += std::string(what.empty() ? "" : ", ") + "factory data";
		}
		if (!found.any())
		{
			setStatus(pickedName + " is not one of the U-220's ROM images, and its folder holds none of them either.");
			publishRomSet(have);
			return;
		}

		std::string target, error;
		for (const fs::path &to : installDirs())
		{
			if (copy.empty())
				break;
			std::error_code ec;
			fs::create_directories(to, ec);
			bool ok = !ec;
			for (const Copy &c : copy)
			{
				if (!ok)
					break;
				// Never over a file of the same name: the user's, whatever it is.
				const fs::path name = fs::u8path(c.name);
				fs::path dest = to / name;
				for (int i = 2; fs::exists(dest, ec); i ++)
					dest = to / fs::u8path(pstr(name.stem()) + " (" + std::to_string(i) + ")" + pstr(name.extension()));
				if (c.data != nullptr)
				{
					ok = writeFile(dest, *c.data);
					if (!ok)
						error = pstr(dest) + ": cannot write";
				}
				else
				{
					ok = fs::copy_file(c.from, dest, ec) && !ec;
					if (!ok)
						error = pstr(dest) + ": " + ec.message();
				}
			}
			if (ok)
			{
				target = pstr(to);
				break;
			}
		}
		if (!copy.empty() && target.empty())
		{
			setStatus("Could not copy the images: " + error);
			publishRomSet(have);
			return;
		}

		std::string missing;
		const bool running = loadRoms(&missing);
		// What matters first: the status line is cut at the panel's width.
		std::string msg = copy.empty() ? "Nothing new in " + pstr(dir) + "."
		                               : "Copied " + what + " from " + pstr(dir) + " to " + target + ".";
		if (std::none_of(std::begin(found.from), std::end(found.from),
				[&](const fs::path &p) { return p.filename() == picked.filename(); })
		    && found.factory.filename() != picked.filename())
			msg += "  (" + pickedName + " itself is not one of them.)";
		if (!running)
		{
			setStatus("Still missing: " + missing + ".  " + msg);
			return;
		}
		std::lock_guard<std::mutex> lock(m_machine);
		if (m_needFirstBoot)
			firstBoot();
		setStatus(std::string("The U-220 is running") + (m_factoryPath.empty()
				? " (no U220Factory.syx, so Memory Initialize).  " : ".  ") + msg);
	}

	/// Power on a machine with no memory, as the factory would have: answer "Memory
	/// Initialize" with ENTER, then send the factory dump and select P-01.  All in emulated
	/// time, run flat out, so the user sees a ready machine and not the procedure.
	void firstBoot()
	{
		m_needFirstBoot = false;
		// Asked before the firmware runs: after that its stack and variables are in the RAM.
		const bool blank = m_core.nvramBlank();
		m_core.reset();
		m_core.runUntilIdle(3000);
		if (!blank && m_factoryPath.empty())
			return;

		auto press = [&](Button b) {
			m_core.setButton(b, true);
			m_core.runUntilIdle(200);
			m_core.setButton(b, false);
			m_core.runUntilIdle(800);
		};
		// The first key after power-on can go unseen (as in saveSysex): press again while
		// the display still asks for it.
		for (int tries = 0; tries < 3; tries ++)
		{
			press(kButtonEnter);
			m_core.runUntilIdle(2000);
			char r0[kLcdColumns + 1], r1[kLcdColumns + 1];
			m_core.lcdText(r0, r1);
			if (std::strstr(r1, "[ENTER]") == nullptr)
				break;
		}

		if (m_factoryPath.empty())
			return;
		const auto msgs = parseDump(m_factoryData.empty() ? readFile(m_factoryPath, 0x100000) : m_factoryData);
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
	std::atomic<bool> m_romsLoaded { false };
	bool m_needFirstBoot = true;
	fs::path m_factoryPath;
	std::vector<uint8_t> m_factoryData;         // the dump, when it came out of a zip
	std::vector<uint8_t> m_pendingNvram;        // a session's memory, held till the ROMs come
	std::thread m_romThread;                    // installRoms
	std::atomic<bool> m_romBusy { false };
	// Whether the host has activated this instance.  Under VST3, DPF runs a second, whole
	// instance for the edit controller, and the panel's state messages reach both: two
	// installs of the same ROM files raced (the loser wrote to %LOCALAPPDATA%), two SysEx
	// saves to one file.  Files are written only by the instance that plays.
	std::atomic<bool> m_activated { false };

	std::string m_status, m_romSet;             // m_romSet: kStateRomSet
	mutable std::mutex m_statusLock;
	std::atomic<bool> m_statusPending { false }, m_romSetPending { false };
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
	uint32_t m_keyFreeFrames[kNumKeys] = {};    // since the key's last release
	char m_keyLcd[kLcdColumns * 2] = {};        // tickKeys: the display, to see it settle
	uint32_t m_lcdStillFrames = 0;

	uint32_t m_publishPeriod = 1920, m_publishAccum = 0, m_publishesSame = 0;
	PanelBlob m_lastBlob {};
	bool m_midiSeen = false;        // MIDI in since the last refresh: the MIDI MESSAGE lamp
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
