// license:BSD-3-Clause
// copyright-holders:Helge Thorsten Kautz
/***************************************************************************************

    U220Core -- the machine, assembled from MAME's devices with no MAME around them

    The machine wiring of roland_u220.cpp, written a second time on purpose (u110_core.cpp
    says why).  Everything that touches the emulation must behave exactly like the driver
    -- the null test against MAME renders proves it -- so where a line looks arbitrary,
    check the driver before changing it.  The scheduling (run_block, the MIDI queue, the
    50 Hz stream flush) is U110Core's, which is already null-tested.

***************************************************************************************/

#include "u220_core.h"
#include "u220_effects.h"

#include "emu.h"

#include "sound/roland_lp.h"
#include "cpu/mcs96/i8x9x.h"
#include <cmath>

#include "roland_u110_data.h"
#include "roland_u220_ic25.h"

#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cstdlib>

namespace ostrich {

using roland_u110::CARD_SIZE;
using roland_u110::PCM_READ_PREFETCH;
using roland_u110::descramble_pcm;

namespace {

#define TRACE_FLAG(NAME, ENV) \
    inline bool NAME() { static const bool on = std::getenv(ENV) != nullptr; return on; }
TRACE_FLAG(trace_tg,   "U220_TGTRACE")
TRACE_FLAG(trace_midi, "U220_MIDITRACE")
TRACE_FLAG(trace_lcd,  "U220_LCDTRACE")
TRACE_FLAG(trace_fx,   "U220_FXTRACE")
TRACE_FLAG(trace_gain, "U220_GAINTRACE")
#undef TRACE_FLAG

constexpr u32 CPU_CLOCK = 12000000;         // 12 MHz XTAL
constexpr u32 PCM_CLOCK = 32768000;         // X2, the tone generator's own crystal
constexpr u32 PCM_DIV   = 1024;             // 32.768 MHz / 1024 = 32000 Hz exactly

constexpr size_t PCM_SPACE_SIZE = 0x400000;
constexpr size_t LOW_SIZE       = 0x800;    // 0x0100-0x07FF; the driver keeps it as RAM

// Where each dumped wave ROM sits in the chip's 4 MB space: A-D on the U-110's internal
// banks, E and F (the SN-U110-08/09 contents) on its first two card slots.  The driver's
// PLACE[] table; the timbre and drum A/B against the hardware found nothing misplaced.
constexpr u32 PLACE[kNumWaveRoms] = { 0x000000, 0x100000, 0x200000, 0x300000, 0x080000, 0x180000 };

using roland_u110::MIDI_BAUD;

// The output stage, MEASURED rather than taken from a schematic: Helge's U-220 against the
// unfiltered core, all 127 sounding timbres averaged (ROADMAP.md, Phase 4).  What separates
// them below 15 kHz is a gentle high shelf, about -3 dB above 5 kHz; an RBJ shelf at
// 5.2 kHz, -3.3 dB, Q 0.5 fits it within 0.8 dB from 1 to 14 kHz.  The U-110's Sallen-Key
// values that stood here first cut far too steeply: the hardware has real content up to
// 16 kHz and beyond (DAC images, which no filter here can add back).
constexpr double SHELF_HZ = 5200.0, SHELF_DB = -3.3, SHELF_Q = 0.5;

} // anonymous namespace


/***************************************************************************************
    Impl
***************************************************************************************/

struct U220Core::Impl
{
	Impl()
		: mconfig(machine)
		, cpu(mconfig, "maincpu", nullptr, CPU_CLOCK)
		, pcm(mconfig, "pcm", nullptr, PCM_CLOCK)
		, program(0x10000, 8)
	{ }

	// --- the machine ---------------------------------------------------------------
	running_machine machine;
	machine_config mconfig;

	p8098_device              cpu;
	mb87419_mb87420_device    pcm;
	// The output stage: one biquad per channel, direct form I.
	double shelf_b[3] = {}, shelf_a[3] = {};
	double shelf_x[6][2] = {}, shelf_y[6][2] = {};   // MIX, DIRECT 1, DIRECT 2; left, right
	void shelf_setup();
	void shelf_process(float *buf, int ch, uint32_t n);

	address_space program;
	std::unique_ptr<address_map> regs_map;
	std::unique_ptr<address_space> regs;

	// --- memory --------------------------------------------------------------------
	std::vector<u8> progrom = std::vector<u8>(kProgramRomBytes, 0xff);
	std::vector<u8> pcmrom  = std::vector<u8>(PCM_SPACE_SIZE, 0xff);
	std::vector<u8> ram     = std::vector<u8>(kNvramBytes, 0);     // IC10
	std::vector<u8> low     = std::vector<u8>(LOW_SIZE, 0);
	bool have_program = false;

	// --- driver state, mirroring roland_u220.cpp ------------------------------------
	u8   bank = 0;                  // IC12 0x1000: what the window at 0x0800 shows
	u8   reg1001 = 0;
	u8   pcmreg[0x20] = {};         // what the CPU last wrote to each chip register
	u8   pend07 = 0;                // envelope target held back until its rate
	bool pend07_valid = false;
	u16  keys = 0xffff;             // active low
	u8   port2 = 0;

	roland_u220::ic25_mixer ic25;      // IC25's voice mixer: pan and effect sends per voice
	void apply_voice_gains(int voice);
	EffectParams effect_params() const;

	BypassEffects bypass;
	EffectsModule *fx = &bypass;
	bool output_filter = true;

	// --- LCD: the driver's text shadow, plus the CGRAM the UI needs -----------------
	u8   ddram[0x80] = {};
	u8   lcd_addr = 0;
	bool lcd_to_cgram = false;
	u8   cgram[64] = {};
	u8   cgram_addr = 0;
	bool cgram_dirty = false;
	// What the UI needs for the controller's own cursor, which the driver's shadow does
	// not model: display control (bit 1 underline, bit 0 blinking block) and where the
	// cursor is.  Kept apart from lcd_addr so nothing the CPU reads back can change.
	u8   display_ctrl = 0;
	u8   cursor_addr = 0;
	// The firmware switches the cursor off around every refresh of the text and back on
	// after it, so a snapshot taken at a random moment would flicker.  The last place the
	// cursor was SHOWN, and when, lets the snapshot ride over those gaps as the glass does.
	u8   shown_addr = 0, shown_flags = 0;
	attotime shown_at = attotime::never();
	void note_cursor();

	emu_timer *lcd_timer = nullptr;
	emu_timer *stream_timer = nullptr;
	emu_timer *slice_timer = nullptr;

	// --- MIDI, U110Core's queue ----------------------------------------------------
	static constexpr double MIDI_BYTE_SECONDS = 10.0 / double(MIDI_BAUD);
	std::vector<u8> midi_in_queue;
	std::vector<attotime> midi_in_at;
	size_t midi_in_pos = 0;
	attotime midi_next_deliverable;
	std::vector<u8> midi_out_bytes;
	std::vector<u32> midi_out_at;

	// --- rendering -------------------------------------------------------------------
	u64 frames_done = 0;
	attotime block_start;
	bool started = false;

	void build();
	void install_map();
	void run_block(u32 frames);

	u8   window_r(offs_t offset);
	void window_w(offs_t offset, u8 data);
	u8   pcm_r(offs_t offset);
	void pcm_w(offs_t offset, u8 data);
	void pcm_flush();
	void lcd_cmd(u8 data);
	void lcd_data(u8 data);

	attotime midi_due() const;
	void midi_deliver_due();
	void midi_tx_w(u8 data);
	void stream_tick(s32);
};


/***************************************************************************************
    Construction
***************************************************************************************/

void U220Core::Impl::build()
{
	if (started)
		return;

	device_sound_interface::s_default_rate = kCoreSampleRate;

	// The tone generator, set up exactly as the driver does.
	pcm.set_rate_divider(PCM_DIV);
	pcm.set_pcm_mode(mb87419_mb87420_device::PCM_FLOAT8_DELTA);
	pcm.set_delta_dc_hz(0.0);
	pcm.set_env_engine(true);
	pcm.set_env_max_int_hz(kCoreSampleRate);
	pcm.set_rom(pcmrom.data(), pcmrom.size());
	// One output per mixer bus: dry, reverb and chorus, each L/R (roland_u220_ic25.h).
	pcm.set_output_count(roland_u220::NUM_BUSES);
	pcm.set_gain_matrix(true);
	ic25.reset(progrom.data());
	pcm.int_callback().set([this](int state) {
		cpu.set_input_line(i8x9x_device::EXTINT_LINE, state);
	});

	// The firmware reads word-sized per-voice state at odd addresses; see mcs96.h.
	cpu.set_unaligned_ext_words(true);
	cpu.ach7_cb().set([]() -> u16 { return 0x2a0; });      // battery sense
	cpu.serial_tx_cb().set([this](u8 data) { midi_tx_w(data); });
	cpu.out_p2_cb().set([this](u8 data) { port2 = data; });

	shelf_setup();

	install_map();

	{
		const auto configs = cpu.space_configs();
		cpu.set_space(AS_PROGRAM, &program);
		for (const auto &c : configs)
		{
			if (c.first != AS_DATA || !c.second->m_internal_map)
				continue;
			regs_map.reset(new address_map(cpu, nullptr));
			c.second->m_internal_map(*regs_map);
			regs_map->allocate();
			regs.reset(new address_space(0x100, c.second->m_databus_width));
			regs->install_map(0x00, 0xff, regs_map.get());
			cpu.set_space(AS_DATA, regs.get());
		}
		cpu.set_share_provider([this](const char *tag) -> std::pair<void *, size_t> {
			auto found = regs_map ? regs_map->find_share(tag)
								  : std::pair<u8 *, size_t>{ nullptr, 0 };
			return { found.first, found.second };
		});
	}

	(void)trace_tg(); (void)trace_midi();

	midi_in_queue.reserve(1024);
	midi_in_at.reserve(1024);
	midi_out_bytes.reserve(1024);
	midi_out_at.reserve(1024);

	lcd_timer = machine.scheduler().alloc([this](s32) {
		// IC12's INT, the LCD-ready pulse on HSI0: the firmware's 0x2130 feeds the next byte.
		cpu.set_input_line(i8x9x_device::HSI0_LINE, ASSERT_LINE);
		cpu.set_input_line(i8x9x_device::HSI0_LINE, CLEAR_LINE);
	});
	stream_timer = machine.scheduler().alloc([this](s32 p) { stream_tick(p); });
	// The driver's 100 ms LCD logger does nothing to the machine, but it is a timer, so it
	// ends CPU slices where MAME's does.  Kept as a no-op for the same slicing.
	slice_timer = machine.scheduler().alloc([](s32) { });

	std::fill(std::begin(ddram), std::end(ddram), u8(' '));

	pcm.start();
	cpu.start();

	const attotime streams_update = attotime::from_hz(u32(50));
	stream_timer->adjust(streams_update, 0, streams_update);
	slice_timer->adjust(attotime::from_msec(100), 0, attotime::from_msec(100));

	started = true;
}

// The firmware has rewritten a word in a voice's mixer block: hand the new gains to the
// tone generator, from the current sample on.
void U220Core::Impl::apply_voice_gains(int voice)
{
	float g[roland_u220::NUM_BUSES];
	ic25.voice_gains(unsigned(voice), g);
	pcm.stream()->update();
	for (int b = 0; b < roland_u220::NUM_BUSES; b ++)
		pcm.set_voice_gain(u8(voice), b, g[b]);
	if (trace_gain())
		std::fprintf(stderr, "GAIN %f v%02d dry %.3f %.3f rev %.3f %.3f cho %.3f %.3f dir1 %.3f %.3f dir2 %.3f %.3f"
				" words %06x %06x\n", machine.time().as_double(), voice, g[0], g[1], g[2], g[3], g[4], g[5],
				g[6], g[7], g[8], g[9], ic25.voice_word(unsigned(voice), 0), ic25.voice_word(unsigned(voice), 1));
}

// The effect fields of the temp patch, bytes 12-19 at 0x1430 (IC10 0x743C), read as one
// little-endian word -- the layout tools/make_reverb_probe.py documents -- and the two
// global switches in the setup byte at 0x1401 (bit 4 reverb, bit 5 chorus).
EffectParams U220Core::Impl::effect_params() const
{
	u64 w = 0;
	for (int i = 7; i >= 0; i --)
		w = (w << 8) | ram[0x743c + i];
	auto f = [w](int pos, int width) { return int((w >> pos) & ((u64(1) << width) - 1)); };
	const u8 setup = ram[0x7401];
	EffectParams p;
	p.reverb_on = BIT(setup, 4);
	p.chorus_on = BIT(setup, 5);
	p.reverb_type = f(60, 3);
	p.reverb_time = f(16, 5);
	p.reverb_level = f(48, 5);
	p.delay_feedback = f(43, 5);
	p.chorus_type = f(29, 3);
	p.chorus_post_reverb = f(63, 1) != 0;
	p.chorus_level = f(6, 5);
	p.chorus_delay = f(32, 5);
	p.chorus_rate = f(0, 5);
	p.chorus_depth = f(11, 5);
	p.chorus_feedback = f(23, 6) - 32;
	return p;
}

// RBJ high shelf (Audio EQ Cookbook) at the core's own rate.
void U220Core::Impl::shelf_setup()
{
	const double A = std::pow(10.0, SHELF_DB / 40.0);
	const double w0 = 2.0 * M_PI * SHELF_HZ / kCoreSampleRate;
	const double c = std::cos(w0), al = std::sin(w0) / (2.0 * SHELF_Q), sa = 2.0 * std::sqrt(A) * al;
	const double a0 = (A + 1) - (A - 1) * c + sa;
	shelf_b[0] = A * ((A + 1) + (A - 1) * c + sa) / a0;
	shelf_b[1] = -2 * A * ((A - 1) + (A + 1) * c) / a0;
	shelf_b[2] = A * ((A + 1) + (A - 1) * c - sa) / a0;
	shelf_a[1] = 2 * ((A - 1) - (A + 1) * c) / a0;
	shelf_a[2] = ((A + 1) - (A - 1) * c - sa) / a0;
}

void U220Core::Impl::shelf_process(float *buf, int ch, uint32_t n)
{
	double *x = shelf_x[ch], *y = shelf_y[ch];
	for (uint32_t i = 0; i < n; i ++)
	{
		const double in = buf[i];
		const double out = shelf_b[0] * in + shelf_b[1] * x[0] + shelf_b[2] * x[1]
				- shelf_a[1] * y[0] - shelf_a[2] * y[1];
		x[1] = x[0]; x[0] = in;
		y[1] = y[0]; y[0] = out;
		buf[i] = float(out);
	}
}

void U220Core::Impl::install_map()
{
	// The driver's mem_map.  0x0000-0x00FF is the CPU's own register file.
	program.install_ram(0x0100, 0x07ff, low.data());
	program.install_handler(0x0800, 0x0fff,
			[this](offs_t o) -> u8 { return window_r(o); },
			[this](offs_t o, u8 d) { window_w(o, d); });
	program.install_handler(0x1000, 0x1001,
			[this](offs_t o) -> u8 { return o ? reg1001 : bank; },
			[this](offs_t o, u8 d) { if (o) reg1001 = d; else bank = d; });
	// IC10's last 4 KB, fixed (pages 0E/0F), under IC12's two registers.
	program.install_ram(0x1002, 0x1fff, ram.data() + 0x7002);
	program.install_rom(0x2000, 0xffff, progrom.data() + 0x2000);
}


/***************************************************************************************
    Handlers
***************************************************************************************/

u8 U220Core::Impl::window_r(offs_t offset)
{
	switch (bank >> 4)
	{
	case 0x0:                                           // SRAM pages
		return ram[((bank & 0x0f) << 11) | offset];
	case 0x1:                                           // LCD
		return (offset & 1) ? ddram[lcd_addr & 0x7f] : (lcd_addr & 0x7f);
	case 0x3:                                           // key matrix, active low
		if (offset == 0x0e) return u8(keys & 0xff);
		if (offset == 0x0d) return u8(keys >> 8) | 0xc0;
		return 0xff;
	case 0x4:                                           // effect processor
		return ic25.read(offset);
	case 0x6:                                           // tone generator
		return pcm_r(offset & 0x1f);
	case 0x8: case 0x9:                                 // EPROM upper half
		return progrom[0x10000 | ((bank & 0x1f) << 11) | offset];
	default:
		return 0xff;
	}
}

void U220Core::Impl::window_w(offs_t offset, u8 data)
{
	switch (bank >> 4)
	{
	case 0x0:
		ram[((bank & 0x0f) << 11) | offset] = data;
		break;
	case 0x1:
		if (offset == 0)      lcd_cmd(data);
		else if (offset == 1) lcd_data(data);
		lcd_timer->adjust(attotime::from_usec(40));
		break;
	case 0x4:
		if (trace_fx())
			std::fprintf(stderr, "FX %f %03X = %02X\n", machine.time().as_double(), unsigned(offset), data);
		fx->write(u16(offset), data, machine.time().as_double());
		if (const int v = ic25.write(offset, data); v >= 0)
			apply_voice_gains(v);
		break;
	case 0x6:
		pcm_w(offset & 0x1f, data);
		break;
	default:
		break;
	}
}

u8 U220Core::Impl::pcm_r(offs_t offset)
{
	pcm_flush();
	switch (offset)
	{
	case 0x00:
	case 0x02:
	case 0x03:
		return pcm.read(offset);
	case 0x01:
	{
		// The CPU's wave-ROM read port: voice 0's phase accumulator is the address.
		const u32 phase = pcmreg[0x08] | (pcmreg[0x09] << 8)
				| (pcmreg[0x0a] << 16) | (u32(pcmreg[0x0b]) << 24);
		const u16 bnk = pcmreg[0x02] | (pcmreg[0x03] << 8);
		const offs_t addr = (((phase >> 14) + PCM_READ_PREFETCH) & 0x3ffff) | ((bnk & 0x3c00) << 8);
		return pcmrom[addr & 0x3fffff];
	}
	default:
		return pcmreg[offset];
	}
}

void U220Core::Impl::pcm_flush()
{
	if (pend07_valid)
	{
		pend07_valid = false;
		pcm.write(0x07, pend07);
	}
}

// The firmware writes an envelope segment as 07 (target) THEN 06 (rate); the device
// starts the segment on 07.  Hold 07 back until its 06 -- the driver explains.
void U220Core::Impl::pcm_w(offs_t offset, u8 data)
{
	pcmreg[offset] = data;
	if (offset == 0x07)
	{
		pend07 = data;
		pend07_valid = true;
		return;
	}
	if (offset == 0x06)
	{
		pcm.write(0x06, data);
		pcm_flush();
		return;
	}
	pcm_flush();
	pcm.write(offset, data);
	if (trace_tg())
		std::fprintf(stderr, "TG %f v%02X reg %02X = %02X\n", machine.time().as_double(),
				pcmreg[0x1f] & 0x1f, unsigned(offset), data);
}

// DDRAM exactly as the driver's shadow tracks it (the CPU reads it back through the
// window, so it must agree).  CGRAM is tracked separately, for the UI only.
void U220Core::Impl::lcd_cmd(u8 data)
{
	if (trace_lcd())
		std::fprintf(stderr, "LCD %f cmd %02X\n", machine.time().as_double(), data);
	if ((data & 0xf8) == 0x08)
		display_ctrl = data & 0x07;
	else if ((data & 0xf8) == 0x10)                 // cursor move, S/C = 0
		cursor_addr = (data & 0x04) ? u8((cursor_addr + 1) & 0x7f) : u8((cursor_addr - 1) & 0x7f);

	if (data == 0x01)
	{
		std::fill(std::begin(ddram), std::end(ddram), u8(' '));
		lcd_addr = 0;
		lcd_to_cgram = false;
	}
	else if ((data & 0xfe) == 0x02)
	{
		lcd_addr = 0;
		lcd_to_cgram = false;
	}
	else if (data & 0x80)
	{
		lcd_addr = data & 0x7f;
		lcd_to_cgram = false;
	}
	else if (data & 0x40)
	{
		lcd_to_cgram = true;
		cgram_addr = data & 0x3f;
	}
	if (data == 0x01 || (data & 0xfe) == 0x02 || (data & 0x80))
		cursor_addr = lcd_addr;
	note_cursor();
}

void U220Core::Impl::note_cursor()
{
	if ((display_ctrl & 0x04) && (display_ctrl & 0x03))
	{
		shown_addr = cursor_addr;
		shown_flags = display_ctrl & 0x03;
		shown_at = machine.time();
	}
}

void U220Core::Impl::lcd_data(u8 data)
{
	if (lcd_to_cgram)
	{
		cgram[cgram_addr] = data;
		cgram_addr = (cgram_addr + 1) & 0x3f;
		cgram_dirty = true;
		return;
	}
	ddram[lcd_addr & 0x7f] = data;
	lcd_addr = (lcd_addr + 1) & 0x7f;
	cursor_addr = lcd_addr;
	note_cursor();
}

attotime U220Core::Impl::midi_due() const
{
	if (midi_in_pos >= midi_in_queue.size())
		return attotime::never();
	attotime const arrival = midi_in_at[midi_in_pos] + attotime::from_double(MIDI_BYTE_SECONDS);
	return (midi_next_deliverable > arrival) ? midi_next_deliverable : arrival;
}

void U220Core::Impl::midi_deliver_due()
{
	while (midi_in_pos < midi_in_queue.size() && machine.time() >= midi_due())
	{
		const attotime due = midi_due();
		const u8 byte = midi_in_queue[midi_in_pos ++];
		if (trace_midi())
			std::fprintf(stderr, "MIDI IN %f %02X   (due %f)\n",
					machine.time().as_double(), byte, due.as_double());
		cpu.serial_w(byte);
		midi_next_deliverable = due + attotime::from_double(MIDI_BYTE_SECONDS);
	}
}

void U220Core::Impl::midi_tx_w(u8 data)
{
	midi_out_bytes.push_back(data);
	const double into_block = (machine.time() - block_start).as_double();
	midi_out_at.push_back(u32(into_block > 0.0 ? into_block * kCoreSampleRate : 0.0));
}


/***************************************************************************************
    Rendering -- U110Core::Impl::run_block, which explains every line
***************************************************************************************/

void U220Core::Impl::stream_tick(s32)
{
	pcm.stream()->update();
}

void U220Core::Impl::run_block(u32 frames)
{
	block_start = attotime::from_ticks(frames_done, kCoreSampleRate);
	const attotime block_end = attotime::from_ticks(frames_done + frames, kCoreSampleRate);

	sound_stream *streams[] = { pcm.stream() };
	for (sound_stream *s : streams)
		s->begin_block(frames, block_start);

	while (machine.time() < block_end)
	{
		midi_deliver_due();
		const attotime now = machine.time();

		attotime target = machine.scheduler().next_event();
		const attotime midi = midi_due();
		if (midi < target)
			target = midi;
		if (block_end < target)
			target = block_end;
		if (target <= now)
		{
			machine.scheduler().advance_to(target);
			continue;
		}

		u64 want = cpu.attotime_to_cycles(target - now);
		if (want == 0)
			want = 1;
		const int used = cpu.run_cycles(int(want));
		machine.scheduler().advance_to(now + cpu.cycles_to_attotime(used > 0 ? u64(used) : 0));
	}
	midi_deliver_due();
	frames_done += frames;

	if (midi_in_pos)
	{
		midi_in_queue.erase(midi_in_queue.begin(), midi_in_queue.begin() + midi_in_pos);
		midi_in_at.erase(midi_in_at.begin(), midi_in_at.begin() + midi_in_pos);
		midi_in_pos = 0;
	}

	for (sound_stream *s : streams)
		s->finish_block();
}


/***************************************************************************************
    Public interface
***************************************************************************************/

U220Core::U220Core() : m_impl(new Impl) { }
U220Core::~U220Core() = default;

LoadResult U220Core::loadProgramRom(const uint8_t *data, size_t len)
{
	if (!data || len != kProgramRomBytes)
		return LoadResult::WrongSize;
	std::memcpy(m_impl->progrom.data(), data, len);
	m_impl->have_program = true;
	m_impl->build();
	return LoadResult::Ok;
}

LoadResult U220Core::loadWaveRom(unsigned index, const uint8_t *data, size_t len)
{
	if (index >= kNumWaveRoms)
		return LoadResult::NoSuchSlot;
	if (!data || len != kWaveRomBytes)
		return LoadResult::WrongSize;
	descramble_pcm(&m_impl->pcmrom[PLACE[index]], data, CARD_SIZE);
	return LoadResult::Ok;
}

void U220Core::reset()
{
	Impl &m = *m_impl;
	if (!m.started)
		return;
	m.bank = 0;
	m.reg1001 = 0;
	m.pend07_valid = false;
	std::fill(std::begin(m.pcmreg), std::end(m.pcmreg), u8(0));
	m.fx->reset();
	m.ic25.reset(m.progrom.data());
	for (unsigned v = 0; v < roland_u220::IC25_NUM_VOICES; v ++)
		for (int b = 0; b < roland_u220::NUM_BUSES; b ++)
			m.pcm.set_voice_gain(u8(v), b, 0.0f);
	m.pcm.reset();
	m.cpu.reset();
}

void U220Core::runUntilIdle(uint32_t timeoutMs)
{
	std::vector<float> l(1024), r(1024);
	const uint32_t blocks = (timeoutMs * kCoreSampleRate / 1000) / 1024;
	for (uint32_t i = 0; i < blocks; i ++)
		renderStereo(l.data(), r.data(), 1024);
}

void U220Core::setEffects(EffectsModule *fx)
{
	m_impl->fx = fx ? fx : &m_impl->bypass;
}

void U220Core::setOutputFilter(bool on)
{
	m_impl->output_filter = on;
}

void U220Core::midiIn(const uint8_t *bytes, size_t n, uint32_t sampleOffset)
{
	const attotime when = attotime::from_ticks(m_impl->frames_done + sampleOffset, kCoreSampleRate);
	for (size_t i = 0; i < n; i ++)
	{
		m_impl->midi_in_queue.push_back(bytes[i]);
		m_impl->midi_in_at.push_back(when);
	}
}

void U220Core::midiInAtTime(const uint8_t *bytes, size_t n, double seconds)
{
	const attotime when = attotime::from_double(seconds);
	for (size_t i = 0; i < n; i ++)
	{
		m_impl->midi_in_queue.push_back(bytes[i]);
		m_impl->midi_in_at.push_back(when);
	}
}

size_t U220Core::midiOut(uint8_t *buf, size_t cap, uint32_t *offsets)
{
	Impl &m = *m_impl;
	const size_t n = std::min(cap, m.midi_out_bytes.size());
	std::copy_n(m.midi_out_bytes.begin(), n, buf);
	if (offsets)
		std::copy_n(m.midi_out_at.begin(), n, offsets);
	m.midi_out_bytes.erase(m.midi_out_bytes.begin(), m.midi_out_bytes.begin() + n);
	m.midi_out_at.erase(m.midi_out_at.begin(), m.midi_out_at.begin() + n);
	return n;
}

void U220Core::renderStereo(float *left, float *right, uint32_t nframes)
{
	float *const mix[2] = { left, right };
	render(mix, nullptr, nullptr, nframes);
}

void U220Core::render(float *const mix[2], float *const dir1[2], float *const dir2[2], uint32_t nframes)
{
	Impl &m = *m_impl;
	float *const *const outs[3] = { mix, dir1, dir2 };
	if (!m.started || !m.have_program || nframes == 0)
	{
		for (float *const *o : outs)
			if (o)
				for (int c = 0; c < 2; c ++)
					std::fill_n(o[c], nframes, 0.0f);
		return;
	}
	m.run_block(nframes);
	using namespace roland_u220;
	sound_stream &st = *m.pcm.stream();
	const auto &dl = st.output(BUS_DRY_L), &dr = st.output(BUS_DRY_R);
	const auto &rl = st.output(BUS_REV_L), &rr = st.output(BUS_REV_R);
	const auto &cl = st.output(BUS_CHO_L), &cr = st.output(BUS_CHO_R);
	const auto &d1l = st.output(BUS_DIR1_L), &d1r = st.output(BUS_DIR1_R);
	const auto &d2l = st.output(BUS_DIR2_L), &d2r = st.output(BUS_DIR2_R);
	// Every bus reaches the output dry; the reverb and chorus buses also feed the effects,
	// which add their returns.  A direct pair with nowhere of its own to go (nullptr: no
	// plug in its jack) joins MIX, without effect.  Then the analogue stage, which follows
	// the DAC, on every pair.
	float *const left = mix[0], *const right = mix[1];
	for (uint32_t i = 0; i < nframes; i ++)
	{
		left[i]  = dl[i] + rl[i] + cl[i];
		right[i] = dr[i] + rr[i] + cr[i];
		if (!dir1) { left[i] += d1l[i]; right[i] += d1r[i]; }
		if (!dir2) { left[i] += d2l[i]; right[i] += d2r[i]; }
	}
	m.fx->setParams(m.effect_params());
	m.fx->process(&rl[0], &rr[0], &cl[0], &cr[0], left, right, nframes);
	if (dir1)
		for (uint32_t i = 0; i < nframes; i ++)
		{
			dir1[0][i] = d1l[i];
			dir1[1][i] = d1r[i];
		}
	if (dir2)
		for (uint32_t i = 0; i < nframes; i ++)
		{
			dir2[0][i] = d2l[i];
			dir2[1][i] = d2r[i];
		}
	if (m.output_filter)
		for (int k = 0; k < 3; k ++)
			if (outs[k])
			{
				m.shelf_process(outs[k][0], 2 * k, nframes);
				m.shelf_process(outs[k][1], 2 * k + 1, nframes);
			}
}

void U220Core::setButton(Button sw, bool down)
{
	if (sw < 0 || sw >= kButtonCount)
		return;
	if (down) m_impl->keys &= u16(~(1u << sw));
	else      m_impl->keys |=  u16(1u << sw);
}

void U220Core::snapshot(PanelState &out) const
{
	const Impl &m = *m_impl;
	std::memset(&out, 0, sizeof(out));
	out.version = PanelState::kVersion;
	std::memcpy(out.lcd, m.ddram, kLcdColumns);
	std::memcpy(out.lcd + kLcdColumns, m.ddram + 0x40, kLcdColumns);
	std::memcpy(out.cgram, m.cgram, sizeof(out.cgram));
	out.cgram_dirty = m.cgram_dirty;
	// DDRAM 0x00-0x17 is row 0, 0x40-0x57 row 1; anywhere else the cursor is off screen.
	// Shown now, or shown within the last 150 ms: see shown_at.
	const bool recent = !m.shown_at.is_never()
			&& (m.machine.time() - m.shown_at) < attotime::from_msec(150);
	const u8 a = m.shown_addr;
	out.cursor_pos = (a < kLcdColumns) ? a : (a >= 0x40 && a < 0x40 + kLcdColumns) ? u8(a - 0x40 + kLcdColumns) : 0xff;
	out.cursor_flags = recent ? m.shown_flags : 0;
}

uint8_t U220Core::readMem(uint16_t addr) const
{ return m_impl->program.read_byte(addr); }

void U220Core::writeMem(uint16_t addr, uint8_t value)
{ m_impl->program.write_byte(addr, value); }

double U220Core::emulatedSeconds() const
{ return double(m_impl->frames_done) / double(kCoreSampleRate); }

uint8_t U220Core::readNvram(uint32_t addr) const
{ return addr < kNvramBytes ? m_impl->ram[addr] : 0xff; }

void U220Core::lcdText(char *row0, char *row1) const
{
	char *rows[2] = { row0, row1 };
	for (unsigned r = 0; r < kLcdRows; r ++)
	{
		for (unsigned c = 0; c < kLcdColumns; c ++)
		{
			const u8 ch = m_impl->ddram[r * 0x40 + c];
			rows[r][c] = (ch >= 0x20 && ch < 0x7f) ? char(ch) : (ch < 0x10 ? '#' : '.');
		}
		rows[r][kLcdColumns] = 0;
	}
}

namespace {
constexpr u32 kStateMagic = 0x55323230;      // "U220"
constexpr u32 kStateVersion = 1;
struct StateHeader { u32 magic, version, nvram_size; };
}

size_t U220Core::saveState(uint8_t *buf, size_t cap) const
{
	const size_t need = sizeof(StateHeader) + kNvramBytes;
	if (buf == nullptr || cap < need)
		return need;
	StateHeader h { kStateMagic, kStateVersion, u32(kNvramBytes) };
	std::memcpy(buf, &h, sizeof(h));
	std::memcpy(buf + sizeof(h), m_impl->ram.data(), kNvramBytes);
	return need;
}

bool U220Core::loadState(const uint8_t *buf, size_t n)
{
	if (buf == nullptr || n < sizeof(StateHeader))
		return false;
	StateHeader h;
	std::memcpy(&h, buf, sizeof(h));
	if (h.magic != kStateMagic || h.version != kStateVersion || h.nvram_size != kNvramBytes)
		return false;
	if (n < sizeof(h) + kNvramBytes)
		return false;
	std::memcpy(m_impl->ram.data(), buf + sizeof(h), kNvramBytes);
	if (m_impl->started)
	{
		reset();
		runUntilIdle(3000);
	}
	return true;
}

bool U220Core::setNvram(const uint8_t *data, size_t len)
{
	if (data == nullptr || len != kNvramBytes)
		return false;
	std::memcpy(m_impl->ram.data(), data, kNvramBytes);
	return true;
}

bool U220Core::nvramBlank() const
{
	return std::all_of(m_impl->ram.begin(), m_impl->ram.end(), [](u8 b) { return b == 0; });
}

} // namespace ostrich
