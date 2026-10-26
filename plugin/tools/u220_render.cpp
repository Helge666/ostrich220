// SPDX-License-Identifier: GPL-3.0-or-later
//
// Render through U220Core, for the null test against MAME and for seeing whether it boots.
//
//   u220_render --roms DIR [--nvram FILE] [--seconds N] [--out FILE.wav] [--lcd]
//               [--midi-at FILE] [--block N] [--no-filter] [--press BIT@SEC[:HOLD],...]
//               [--save-nvram FILE]
//
// --midi-at takes "t,byte" lines (hex byte, absolute emulated seconds), e.g. extracted
// from the MAME driver's "MIDI IN <t> <byte>" log, so both see the same arrival times.
// --no-filter leaves the analogue output stage out, which is what MAME's driver produces.
// --press uses the key bits of u220_core.h, like tools/u220_keys.lua.

#include "u220_core.h"
#include "u220_effects.h"
#include "u220_fx.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

std::vector<uint8_t> read_file(const std::string &path)
{
	std::vector<uint8_t> data;
	FILE *f = std::fopen(path.c_str(), "rb");
	if (!f) return data;
	std::fseek(f, 0, SEEK_END);
	long n = std::ftell(f);
	std::fseek(f, 0, SEEK_SET);
	data.resize(size_t(n < 0 ? 0 : n));
	if (!data.empty() && std::fread(data.data(), 1, data.size(), f) != data.size())
		data.clear();
	std::fclose(f);
	return data;
}

void write_wav(const std::string &path, const std::vector<int16_t> &interleaved, uint32_t rate)
{
	FILE *f = std::fopen(path.c_str(), "wb");
	if (!f) { std::fprintf(stderr, "cannot write %s\n", path.c_str()); return; }
	const uint32_t bytes = uint32_t(interleaved.size() * 2);
	auto u32w = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
	auto u16w = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
	std::fwrite("RIFF", 1, 4, f); u32w(36 + bytes); std::fwrite("WAVE", 1, 4, f);
	std::fwrite("fmt ", 1, 4, f); u32w(16); u16w(1); u16w(2);
	u32w(rate); u32w(rate * 4); u16w(4); u16w(16);
	std::fwrite("data", 1, 4, f); u32w(bytes);
	std::fwrite(interleaved.data(), 1, bytes, f);
	std::fclose(f);
}

// MAME's quantiser with dither off (u110_render.cpp has the details).
int16_t quantise(float v)
{
	int i = int(std::floor(v * 32768.0f + 0.5f));
	return int16_t(i > 32767 ? 32767 : i < -32768 ? -32768 : i);
}

const char *const kProgramNames[] = {
	"u-220_roland_1-0-2.ic8", "Roland_U-220_v1.02_IC8.BIN",
	"roland_u220_pgm_v1-02_(15209245_LH530847).bin",
};
const char *const kWaveNames[6][2] = {
	{ "roland-a_r15179892f_mb834000a-20_226-aa.bin", "roland_t110_u110_u220_waverom0.bin" },
	{ "roland-b_r15179893f_mb834000a-20_227-aa.bin", "roland_t110_u110_u220_waverom1.bin" },
	{ "roland-c_r15179894f_mb834000a-20_228-aa.bin", "roland_t110_u110_u220_waverom2.bin" },
	{ "roland-d_r15179895f_mb834000a-20_229-aa.bin", "roland_t110_u110_u220_waverom3.bin" },
	{ "roland-e_r15179947_mb834000a-20_3a1-aa.bin",  "roland_u220_waverom4_(sn-u110-08).bin" },
	{ "roland-f_r15179948_mb834000a-20_3a2-aa.bin",  "roland_u220_waverom5_(sn-u110-09).bin" },
};

// Keeps the reverb and chorus send buses, for measuring the effects against hardware, and
// every change of the effect settings with the frame it took effect at, so
// u220_reverb_run can replay the effects alone (float, not quantised: the fit wants the
// low tails).
struct BusTap final : ostrich::EffectsModule
{
	std::vector<int16_t> rev, cho;
	std::vector<float> revf, chof;
	std::string params;
	uint64_t frame = 0;
	ostrich::EffectParams last;
	bool any = false;
	void setParams(const ostrich::EffectParams &p) override
	{
		if (any && p == last)
			return;
		any = true;
		last = p;
		char buf[160];
		std::snprintf(buf, sizeof(buf), "%llu %d %d %d %d %d %d %d %d %d %d %d %d %d\n",
				(unsigned long long)frame, p.reverb_on, p.chorus_on, p.reverb_type, p.reverb_time,
				p.reverb_level, p.delay_feedback, p.chorus_type, p.chorus_post_reverb,
				p.chorus_level, p.chorus_delay, p.chorus_rate, p.chorus_depth, p.chorus_feedback);
		params += buf;
	}
	void write(uint16_t, uint8_t, double) override { }
	void reset() override { }
	void process(const float *rl, const float *rr, const float *cl, const float *cr,
			float *, float *, uint32_t n) override
	{
		for (uint32_t i = 0; i < n; i ++)
		{
			rev.push_back(quantise(rl[i])); rev.push_back(quantise(rr[i]));
			cho.push_back(quantise(cl[i])); cho.push_back(quantise(cr[i]));
			revf.push_back(rl[i]); revf.push_back(rr[i]);
			chof.push_back(cl[i]); chof.push_back(cr[i]);
		}
		frame += n;
	}
};

void write_f32(const std::string &path, const std::vector<float> &v)
{
	if (FILE *f = std::fopen(path.c_str(), "wb"))
	{
		std::fwrite(v.data(), sizeof(float), v.size(), f);
		std::fclose(f);
	}
}

} // namespace

int main(int argc, char **argv)
{
	std::string roms = "roms", out, nvram, save_nvram, midi_events, presses, midi_out, send_out;
	std::vector<std::string> cards;
	double seconds = 10.0;
	bool show_lcd = false, filter = true, reverb = false, no_reverb = false;
	uint32_t blocksize = 512;

	for (int i = 1; i < argc; i ++)
	{
		std::string a = argv[i];
		auto next = [&]() { return (i + 1 < argc) ? argv[++i] : ""; };
		if      (a == "--roms")       roms = next();
		else if (a == "--nvram")      nvram = next();
		else if (a == "--save-nvram") save_nvram = next();
		else if (a == "--out")        out = next();
		else if (a == "--seconds")    seconds = std::atof(next());
		else if (a == "--lcd")        show_lcd = true;
		else if (a == "--block")      blocksize = uint32_t(std::atoi(next()));
		else if (a == "--midi-at")    midi_events = next();
		else if (a == "--no-filter")  filter = false;
		else if (a == "--press")      presses = next();
		else if (a == "--card")       cards.push_back(next());   // SLOT:FILE[@SEC], FILE "-" = eject
		else if (a == "--midi-out")   midi_out = next();     // raw bytes the machine sends
		else if (a == "--send-out")   send_out = next();     // reverb send bus as WAV
		else if (a == "--reverb")     reverb = true;         // the effect module on
		else if (a == "--no-reverb")  no_reverb = true;      // ...but the chorus only
		else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); return 2; }
	}

	ostrich::U220Core core;

	bool loaded = false;
	for (const char *n : kProgramNames)
	{
		auto d = read_file(roms + "/" + n);
		if (d.size() == ostrich::kProgramRomBytes)
		{
			std::printf("program ROM: %s\n", n);
			loaded = core.loadProgramRom(d.data(), d.size()) == ostrich::LoadResult::Ok;
			break;
		}
	}
	if (!loaded) { std::fprintf(stderr, "no program ROM found under %s\n", roms.c_str()); return 1; }

	for (unsigned w = 0; w < ostrich::kNumWaveRoms; w ++)
	{
		std::vector<uint8_t> d;
		for (const char *n : kWaveNames[w])
			if ((d = read_file(roms + "/" + n)).size() == ostrich::kWaveRomBytes)
				break;
		if (d.size() != ostrich::kWaveRomBytes)
		{ std::fprintf(stderr, "wave ROM %c missing or wrong size\n", 'A' + w); return 1; }
		core.loadWaveRom(w, d.data(), d.size());
	}
	std::printf("wave ROMs: A-F loaded and descrambled\n");

	if (!nvram.empty())
	{
		// A raw 32 KB image, as MAME's nvram/u220/nvram holds it.
		auto d = read_file(nvram);
		if (d.size() != ostrich::kNvramBytes) { std::fprintf(stderr, "bad NVRAM %s\n", nvram.c_str()); return 1; }
		core.setNvram(d.data(), d.size());
	}
	// PCM cards: in the slot from power-on, or put in / pulled out at a time.
	struct CardChange { unsigned slot; double at; std::vector<uint8_t> prepared; bool eject, done = false; };
	std::vector<CardChange> card_changes;
	for (const std::string &c : cards)
	{
		const size_t colon = c.find(':'), at = c.rfind('@');
		const std::string file = c.substr(colon + 1, (at == std::string::npos || at < colon) ? std::string::npos : at - colon - 1);
		CardChange ch { unsigned(std::atoi(c.c_str())), (at == std::string::npos || at < colon) ? -1.0 : std::atof(c.c_str() + at + 1), {}, file == "-" };
		if (!ch.eject)
		{
			auto d = read_file(file);
			if (ostrich::U220Core::prepareCard(d.data(), d.size(), ch.prepared) != ostrich::LoadResult::Ok)
			{ std::fprintf(stderr, "bad card image %s\n", file.c_str()); return 1; }
		}
		if (ch.at < 0.0)
		{
			if (ch.eject) core.ejectCard(ch.slot); else core.installCard(ch.slot, ch.prepared.data());
			std::printf("card slot %u: %s at power-on\n", ch.slot, file.c_str());
			continue;
		}
		card_changes.push_back(std::move(ch));
	}

	core.setOutputFilter(filter);
	BusTap tap;
	ostrich::U220Fx rev;
	if (!send_out.empty())
		core.setEffects(&tap);
	else if (reverb)
	{
		rev.setReverbEnabled(!no_reverb);
		core.setEffects(&rev);
	}
	core.reset();

	struct TimedByte { double t; uint8_t b; };
	std::vector<TimedByte> injected;
	if (!midi_events.empty())
	{
		FILE *f = std::fopen(midi_events.c_str(), "r");
		if (!f) { std::fprintf(stderr, "cannot read %s\n", midi_events.c_str()); return 1; }
		double t; unsigned b;
		while (std::fscanf(f, "%lf,%x", &t, &b) == 2)
			injected.push_back({ t, uint8_t(b) });
		std::fclose(f);
		std::printf("injecting %zu MIDI bytes at given arrival times\n", injected.size());
	}

	struct Press { int bit; double at, hold; bool down = false, up = false; };
	std::vector<Press> press;
	for (size_t p = 0; p < presses.size(); )
	{
		size_t e = presses.find(',', p);
		std::string item = presses.substr(p, e == std::string::npos ? std::string::npos : e - p);
		Press k { 0, 0, 0.1 };
		std::sscanf(item.c_str(), "%d@%lf:%lf", &k.bit, &k.at, &k.hold);
		press.push_back(k);
		p = (e == std::string::npos) ? presses.size() : e + 1;
	}

	const uint32_t rate = ostrich::kCoreSampleRate;
	const uint32_t total = uint32_t(seconds * rate);
	std::vector<float> l(blocksize), r(blocksize);
	std::vector<int16_t> pcm;
	pcm.reserve(size_t(total) * 2);
	// The trace records a byte's COMPLETION; hand it in one byte time earlier.
	const double byte_time = 10.0 / 31250.0;
	size_t inject_pos = 0;
	std::string last_lcd;
	uint32_t done = 0;

	while (done < total)
	{
		const uint32_t n = std::min(blocksize, total - done);
		while (inject_pos < injected.size())
		{
			const double start = injected[inject_pos].t - byte_time;
			if (start >= double(done + n) / rate) break;
			core.midiInAtTime(&injected[inject_pos].b, 1, start);
			inject_pos ++;
		}
		const double now = double(done) / rate;
		for (auto &k : press)
		{
			if (!k.down && now >= k.at) { core.setButton(ostrich::Button(k.bit), true); k.down = true; }
			if (k.down && !k.up && now >= k.at + k.hold) { core.setButton(ostrich::Button(k.bit), false); k.up = true; }
		}
		for (auto &ch : card_changes)
			if (!ch.done && now >= ch.at)
			{
				if (ch.eject) core.ejectCard(ch.slot); else core.installCard(ch.slot, ch.prepared.data());
				std::printf("card slot %u: %s at %.2f s\n", ch.slot, ch.eject ? "pulled out" : "put in", now);
				ch.done = true;
			}

		core.renderStereo(l.data(), r.data(), n);
		if (!midi_out.empty())
		{
			uint8_t mo[4096];
			const size_t got = core.midiOut(mo, sizeof(mo), nullptr);
			if (got)
			{
				FILE *f = std::fopen(midi_out.c_str(), "ab");
				if (f) { std::fwrite(mo, 1, got, f); std::fclose(f); }
			}
		}
		for (uint32_t i = 0; i < n; i ++)
		{
			pcm.push_back(quantise(l[i]));
			pcm.push_back(quantise(r[i]));
		}
		done += n;

		if (show_lcd)
		{
			char r0[ostrich::kLcdColumns + 1], r1[ostrich::kLcdColumns + 1];
			core.lcdText(r0, r1);
			ostrich::PanelState st;
			core.snapshot(st);
			char cur[32];
			std::snprintf(cur, sizeof(cur), "  cursor %d flags %d", st.cursor_pos == 0xff ? -1 : int(st.cursor_pos), st.cursor_flags);
			std::string text = std::string("[") + r0 + "][" + r1 + "]" + cur;
			if (text != last_lcd)
			{
				std::printf("LCD %8.3f %s\n", double(done) / rate, text.c_str());
				last_lcd = text;
			}
		}
	}

	int16_t peak = 0;
	for (int16_t s : pcm) { int16_t a = s < 0 ? int16_t(-s) : s; if (a > peak) peak = a; }
	std::printf("rendered %.2f s, peak %d\n", seconds, peak);
	if (!out.empty())
	{
		write_wav(out, pcm, rate);
		std::printf("wrote %s\n", out.c_str());
	}
	if (!send_out.empty())
	{
		write_wav(send_out, tap.rev, rate);
		write_wav(send_out + ".cho.wav", tap.cho, rate);
		write_f32(send_out + ".rev.f32", tap.revf);
		write_f32(send_out + ".cho.f32", tap.chof);
		if (FILE *f = std::fopen((send_out + ".params.txt").c_str(), "w"))
		{
			std::fputs(tap.params.c_str(), f);
			std::fclose(f);
		}
		std::printf("wrote %s (reverb send), .cho.wav, .rev.f32/.cho.f32 and .params.txt\n", send_out.c_str());
	}
	if (!save_nvram.empty())
	{
		std::vector<uint8_t> st(core.saveState(nullptr, 0));
		core.saveState(st.data(), st.size());
		FILE *f = std::fopen(save_nvram.c_str(), "wb");
		if (f) { std::fwrite(st.data() + st.size() - ostrich::kNvramBytes, 1, ostrich::kNvramBytes, f); std::fclose(f); }
	}
	return 0;
}
