// SPDX-License-Identifier: GPL-3.0-or-later
//
// Run U220Reverb alone over send buses recorded by `u220_render --send-out`, replaying the
// effect settings at the frames they changed -- the inner loop of tools/reverb_fit.py,
// which needs the reverb's output for a new tuning in seconds, not a full emulation run.
//
//   u220_reverb_run SEND [--tuning FILE] [--out FILE.f32]
//
// SEND is the --send-out path: SEND.rev.f32, SEND.cho.f32 and SEND.params.txt are read.
// The output is the reverb return alone (no dry, no output stage), interleaved stereo
// float32 at the core rate.  Without --tuning the built-in tuning is used.

#include "u220_reverb.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

std::vector<float> read_f32(const std::string &path)
{
	std::vector<float> v;
	FILE *f = std::fopen(path.c_str(), "rb");
	if (!f) return v;
	std::fseek(f, 0, SEEK_END);
	const long n = std::ftell(f);
	std::fseek(f, 0, SEEK_SET);
	v.resize(size_t(n < 0 ? 0 : n) / sizeof(float));
	if (std::fread(v.data(), sizeof(float), v.size(), f) != v.size())
		v.clear();
	std::fclose(f);
	return v;
}

std::string read_text(const std::string &path)
{
	std::string s;
	if (FILE *f = std::fopen(path.c_str(), "r"))
	{
		char buf[4096];
		size_t n;
		while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0)
			s.append(buf, n);
		std::fclose(f);
	}
	return s;
}

} // namespace

int main(int argc, char **argv)
{
	if (argc < 2)
	{
		std::fprintf(stderr, "usage: u220_reverb_run SEND [--tuning FILE] [--out FILE.f32]\n");
		return 2;
	}
	const std::string send = argv[1];
	std::string tuning, out = "reverb_out.f32";
	for (int i = 2; i < argc; i ++)
	{
		const std::string a = argv[i];
		auto next = [&]() { return (i + 1 < argc) ? argv[++i] : ""; };
		if      (a == "--tuning") tuning = next();
		else if (a == "--out")    out = next();
		else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); return 2; }
	}

	const std::vector<float> rev = read_f32(send + ".rev.f32"), cho = read_f32(send + ".cho.f32");
	if (rev.empty() || cho.size() != rev.size())
	{
		std::fprintf(stderr, "cannot read %s.rev.f32 / .cho.f32\n", send.c_str());
		return 1;
	}
	struct Change { unsigned long long frame; ostrich::EffectParams p; };
	std::vector<Change> changes;
	{
		FILE *f = std::fopen((send + ".params.txt").c_str(), "r");
		if (!f) { std::fprintf(stderr, "cannot read %s.params.txt\n", send.c_str()); return 1; }
		Change c;
		int v[13];
		while (std::fscanf(f, "%llu %d %d %d %d %d %d %d %d %d %d %d %d %d", &c.frame, &v[0], &v[1],
				&v[2], &v[3], &v[4], &v[5], &v[6], &v[7], &v[8], &v[9], &v[10], &v[11], &v[12]) == 14)
		{
			c.p.reverb_on = v[0]; c.p.chorus_on = v[1]; c.p.reverb_type = v[2];
			c.p.reverb_time = v[3]; c.p.reverb_level = v[4]; c.p.delay_feedback = v[5];
			c.p.chorus_type = v[6]; c.p.chorus_post_reverb = v[7]; c.p.chorus_level = v[8];
			c.p.chorus_delay = v[9]; c.p.chorus_rate = v[10]; c.p.chorus_depth = v[11];
			c.p.chorus_feedback = v[12];
			changes.push_back(c);
		}
		std::fclose(f);
	}

	ostrich::U220Reverb reverb;
	if (!tuning.empty())
	{
		ostrich::ReverbTuning t = ostrich::ReverbTuning::builtin();
		std::string err;
		if (!t.parse(read_text(tuning), &err))
		{
			std::fprintf(stderr, "%s: %s\n", tuning.c_str(), err.c_str());
			return 1;
		}
		reverb.setTuning(t);
	}

	// Same block structure as the core: settings change on block boundaries.
	const size_t frames = rev.size() / 2;
	constexpr size_t BLOCK = 512;
	std::vector<float> rl(BLOCK), rr(BLOCK), cl(BLOCK), cr(BLOCK), ol(BLOCK), orr(BLOCK);
	std::vector<float> result(frames * 2);
	size_t next = 0;
	for (size_t done = 0; done < frames; )
	{
		while (next < changes.size() && changes[next].frame <= done)
			reverb.setParams(changes[next ++].p);
		size_t n = std::min(BLOCK, frames - done);
		if (next < changes.size() && changes[next].frame > done && changes[next].frame < done + n)
			n = size_t(changes[next].frame - done);
		for (size_t i = 0; i < n; i ++)
		{
			rl[i] = rev[(done + i) * 2]; rr[i] = rev[(done + i) * 2 + 1];
			cl[i] = cho[(done + i) * 2]; cr[i] = cho[(done + i) * 2 + 1];
			ol[i] = orr[i] = 0.0f;
		}
		reverb.process(rl.data(), rr.data(), cl.data(), cr.data(), ol.data(), orr.data(), uint32_t(n));
		for (size_t i = 0; i < n; i ++)
		{
			result[(done + i) * 2] = ol[i];
			result[(done + i) * 2 + 1] = orr[i];
		}
		done += n;
	}

	FILE *f = std::fopen(out.c_str(), "wb");
	if (!f) { std::fprintf(stderr, "cannot write %s\n", out.c_str()); return 1; }
	std::fwrite(result.data(), sizeof(float), result.size(), f);
	std::fclose(f);
	std::printf("%zu frames, %zu setting changes -> %s\n", frames, changes.size(), out.c_str());
	return 0;
}
