// SPDX-License-Identifier: GPL-3.0-or-later
/*
    Load Ostrich.clap without a GUI, play one note through it and write the output, so
    the PLUGIN (ROM search, first boot into factory data, MIDI timing, resampler, state)
    is tested and not only the core.

        ostrich_clap_render Ostrich.clap out.wav [note] [rate] [presses] [channel] [params]
                            [seconds] [note-on]

    Plays `note` (default 60) at velocity 100 on `channel` (1-16, default 1) from `note-on`
    (default 1.0 s) for 2 s, renders `seconds` (default 6) at `rate` (default 48000), then
    saves the plugin state and prints its size.  `presses`: parameters to press for 0.1 s,
    "id@sec,id@sec,..." (a bare id presses at 2.0 s) -- the panel keys.  `params`: values
    to set at the start, "id=value,...".  The WAV has all six outputs: MIX OUT, DIRECT OUT
    1, DIRECT OUT 2, each given buffers of its own (plugged in).
*/

#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "clap/entry.h"
#include "clap/plugin-factory.h"
#include "clap/ext/state.h"

#define FRAMES 256

static const void *host_get_extension(const clap_host_t *h, const char *id) { (void)h; (void)id; return NULL; }
static void host_noop(const clap_host_t *h) { (void)h; }
static const clap_host_t host = {
	CLAP_VERSION, NULL, "ostrich_clap_render", "Ostrich", "", "0",
	host_get_extension, host_noop, host_noop, host_noop
};

typedef union { clap_event_header_t header; clap_event_note_t note; clap_event_param_value_t param; } any_event_t;
typedef struct { any_event_t ev[64]; uint32_t n; } events_t;
static uint32_t in_size(const clap_input_events_t *l) { return ((const events_t *)l->ctx)->n; }
static const clap_event_header_t *in_get(const clap_input_events_t *l, uint32_t i)
{ return &((const events_t *)l->ctx)->ev[i].header; }
static bool out_push(const clap_output_events_t *l, const clap_event_header_t *e) { (void)l; (void)e; return true; }

typedef struct { uint8_t *p; size_t n, cap; } buf_t;
static int64_t ostream_write(const clap_ostream_t *s, const void *d, uint64_t n)
{
	buf_t *b = (buf_t *)s->ctx;
	if (b->n + n > b->cap) { b->cap = (b->n + n) * 2; b->p = realloc(b->p, b->cap); }
	memcpy(b->p + b->n, d, n); b->n += n; return (int64_t)n;
}
static int64_t istream_read(const clap_istream_t *s, void *d, uint64_t n)
{
	buf_t *b = (buf_t *)s->ctx;
	const uint64_t left = b->cap - b->n, k = n < left ? n : left;
	memcpy(d, b->p + b->n, k); b->n += k; return (int64_t)k;
}

int main(int argc, char **argv)
{
	if (argc < 3) { fprintf(stderr, "usage: %s plugin.clap out.wav [note] [rate]\n", argv[0]); return 2; }
	const int note = argc > 3 ? atoi(argv[3]) : 60;
	const double rate = argc > 4 ? atof(argv[4]) : 48000.0;
	const int channel = argc > 6 ? atoi(argv[6]) - 1 : 0;    // 1-based on the command line
	const double seconds = argc > 8 ? atof(argv[8]) : 6.0;
	const double note_on = argc > 9 ? atof(argv[9]) : 1.0;
	const bool realtime = getenv("OSTRICH_RENDER_REALTIME") != NULL;

	// Presses ("id@sec,...", a bare id at 2.0 s) and starting values ("id=value,...").
	struct { int id; double at; } presses[32]; int npress = 0;
	if (argc > 5 && atoi(argv[5]) >= 0)
		for (char *t = strtok(strdup(argv[5]), ","); t && npress < 32; t = strtok(NULL, ","))
		{
			char *at = strchr(t, '@');
			presses[npress].id = atoi(t);
			presses[npress ++].at = at ? atof(at + 1) : 2.0;
		}
	struct { int id; double value; } params[16]; int nparam = 0;
	if (argc > 7)
		for (char *t = strtok(strdup(argv[7]), ","); t && nparam < 16; t = strtok(NULL, ","))
		{
			char *eq = strchr(t, '=');
			if (!eq) continue;
			params[nparam].id = atoi(t);
			params[nparam ++].value = atof(eq + 1);
		}

	void *lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
	if (!lib) { fprintf(stderr, "%s\n", dlerror()); return 1; }
	const clap_plugin_entry_t *entry = dlsym(lib, "clap_entry");
	entry->init(argv[1]);
	const clap_plugin_factory_t *f = entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
	const clap_plugin_descriptor_t *d = f->get_plugin_descriptor(f, 0);
	printf("plugin: %s (%s)\n", d->name, d->id);
	const clap_plugin_t *p = f->create_plugin(f, &host, d->id);
	p->init(p);
	// OSTRICH_RENDER_LOAD: a state saved by OSTRICH_RENDER_SAVE (perhaps with another
	// machine memory put in), loaded before the first note -- a session reopened.
	if (getenv("OSTRICH_RENDER_LOAD"))
	{
		const clap_plugin_state_t *st = p->get_extension(p, CLAP_EXT_STATE);
		FILE *sf = fopen(getenv("OSTRICH_RENDER_LOAD"), "rb");
		if (!st || !sf) { fprintf(stderr, "cannot load state\n"); return 1; }
		buf_t b = { 0 };
		fseek(sf, 0, SEEK_END); b.cap = (size_t)ftell(sf); fseek(sf, 0, SEEK_SET);
		b.p = malloc(b.cap);
		if (fread(b.p, 1, b.cap, sf) != b.cap) { fprintf(stderr, "short state file\n"); return 1; }
		fclose(sf);
		clap_istream_t is = { &b, istream_read };
		printf("state: %s\n", st->load(p, &is) ? "loaded" : "LOAD FAILED");
		free(b.p);
	}
	if (!p->activate(p, rate, 1, FRAMES)) { fprintf(stderr, "activate failed\n"); return 1; }
	p->start_processing(p);

	const uint32_t total = (uint32_t)(rate * seconds);
	float *out[6];
	for (int c = 0; c < 6; c ++)
		out[c] = calloc(total + FRAMES, sizeof(float));
	for (uint32_t pos = 0; pos < total; pos += FRAMES)
	{
		events_t evs; memset(&evs, 0, sizeof(evs));
		const uint32_t on = (uint32_t)(rate * note_on), off = (uint32_t)(rate * (note_on + 2.0));
		if (pos == 0)
			for (int k = 0; k < nparam; k ++)
			{
				clap_event_param_value_t *e = &evs.ev[evs.n ++].param;
				memset(e, 0, sizeof(*e));
				e->header.size = sizeof(*e);
				e->header.space_id = CLAP_CORE_EVENT_SPACE_ID;
				e->header.type = CLAP_EVENT_PARAM_VALUE;
				e->param_id = (clap_id)params[k].id;
				e->note_id = -1; e->port_index = -1; e->channel = -1; e->key = -1;
				e->value = params[k].value;
			}
		for (int k = 0; k < 2; k ++)
		{
			const uint32_t at = k ? off : on;
			if (at >= pos && at < pos + FRAMES)
			{
				clap_event_note_t *e = &evs.ev[evs.n ++].note;
				e->header.size = sizeof(*e);
				e->header.time = at - pos;
				e->header.space_id = CLAP_CORE_EVENT_SPACE_ID;
				e->header.type = k ? CLAP_EVENT_NOTE_OFF : CLAP_EVENT_NOTE_ON;
				e->note_id = -1; e->port_index = 0; e->channel = (int16_t)channel; e->key = (int16_t)note;
				e->velocity = 100.0 / 127.0;
			}
		}
		// The panel keys: pressed at their time, released 0.1 s later.
		for (int j = 0; j < npress; j ++)
			for (int k = 0; k < 2; k ++)
			{
				const uint32_t at = (uint32_t)(rate * (presses[j].at + (k ? 0.1 : 0.0)));
				if (at >= pos && at < pos + FRAMES && evs.n < 64)
				{
					clap_event_param_value_t *e = &evs.ev[evs.n ++].param;
					memset(e, 0, sizeof(*e));
					e->header.size = sizeof(*e);
					e->header.time = at - pos;
					e->header.space_id = CLAP_CORE_EVENT_SPACE_ID;
					e->header.type = CLAP_EVENT_PARAM_VALUE;
					e->param_id = (clap_id)presses[j].id;
					e->note_id = -1; e->port_index = -1; e->channel = -1; e->key = -1;
					e->value = k ? 0.0 : 1.0;
				}
			}
		clap_input_events_t in = { &evs, in_size, in_get };
		clap_output_events_t oev = { NULL, out_push };
		float *chans[3][2];
		clap_audio_buffer_t ob[3];
		for (int b = 0; b < 3; b ++)
		{
			chans[b][0] = out[2 * b] + pos;
			chans[b][1] = out[2 * b + 1] + pos;
			ob[b] = (clap_audio_buffer_t){ chans[b], NULL, 2, 0, 0 };
		}
		clap_process_t proc = { (int64_t)pos, FRAMES, NULL, NULL, ob, 0, 3, &in, &oev };
		p->process(p, &proc);
		// OSTRICH_RENDER_REALTIME: as slow as playback, so that the plugin's once-a-second
		// debug log (PROGRAMDATA/Ostrich/ostrich-dsp.log) shows the display step by step.
		if (realtime)
			usleep((useconds_t)(1e6 * FRAMES / rate));
	}

	const clap_plugin_state_t *st = p->get_extension(p, CLAP_EXT_STATE);
	if (st)
	{
		buf_t b = { 0 };
		clap_ostream_t os = { &b, ostream_write };
		const bool ok = st->save(p, &os);
		printf("state: %s, %zu bytes, nvram %s\n", ok ? "saved" : "FAILED", b.n,
		       memmem(b.p, b.n, "nvram", 5) ? "present" : "MISSING");
		if (getenv("OSTRICH_RENDER_SAVE"))
		{
			FILE *sf = fopen(getenv("OSTRICH_RENDER_SAVE"), "wb");
			if (sf) { fwrite(b.p, 1, b.n, sf); fclose(sf); }
		}
		free(b.p);
	}
	p->stop_processing(p);
	p->deactivate(p);
	p->destroy(p);
	entry->deinit();

	FILE *w = fopen(argv[2], "wb");
	const uint32_t bytes = total * 12;
	uint32_t v; uint16_t s;
	fwrite("RIFF", 1, 4, w); v = 36 + bytes; fwrite(&v, 4, 1, w); fwrite("WAVEfmt ", 1, 8, w);
	v = 16; fwrite(&v, 4, 1, w); s = 1; fwrite(&s, 2, 1, w); s = 6; fwrite(&s, 2, 1, w);
	v = (uint32_t)rate; fwrite(&v, 4, 1, w); v = (uint32_t)rate * 12; fwrite(&v, 4, 1, w);
	s = 12; fwrite(&s, 2, 1, w); s = 16; fwrite(&s, 2, 1, w); fwrite("data", 1, 4, w); fwrite(&bytes, 4, 1, w);
	float peak[6] = { 0 };
	for (uint32_t i = 0; i < total; i ++)
		for (int c = 0; c < 6; c ++)
		{
			const float x = out[c][i];
			if (x > peak[c]) peak[c] = x;
			if (-x > peak[c]) peak[c] = -x;
			int q = (int)(x * 32767.0f); if (q > 32767) q = 32767; if (q < -32768) q = -32768;
			int16_t q16 = (int16_t)q; fwrite(&q16, 2, 1, w);
		}
	fclose(w);
	printf("wrote %s, peak MIX %.3f %.3f  DIRECT 1 %.3f %.3f  DIRECT 2 %.3f %.3f\n", argv[2],
	       peak[0], peak[1], peak[2], peak[3], peak[4], peak[5]);
	return 0;
}
