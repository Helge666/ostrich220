// SPDX-License-Identifier: GPL-3.0-or-later
/*
    Load Ostrich.clap without a GUI, play one note through it and write the output, so
    the PLUGIN (ROM search, first boot into factory data, MIDI timing, resampler, state)
    is tested and not only the core.

        ostrich_clap_render Ostrich.clap out.wav [note] [rate]

    Plays `note` (default 60) at velocity 100 from 1.0 s to 3.0 s, renders 6 s at `rate`
    (default 48000), then saves the plugin state and prints its size.
*/

#include <dlfcn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

typedef struct { clap_event_note_t ev[2]; uint32_t n; } events_t;
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

int main(int argc, char **argv)
{
	if (argc < 3) { fprintf(stderr, "usage: %s plugin.clap out.wav [note] [rate]\n", argv[0]); return 2; }
	const int note = argc > 3 ? atoi(argv[3]) : 60;
	const double rate = argc > 4 ? atof(argv[4]) : 48000.0;

	void *lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
	if (!lib) { fprintf(stderr, "%s\n", dlerror()); return 1; }
	const clap_plugin_entry_t *entry = dlsym(lib, "clap_entry");
	entry->init(argv[1]);
	const clap_plugin_factory_t *f = entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
	const clap_plugin_descriptor_t *d = f->get_plugin_descriptor(f, 0);
	printf("plugin: %s (%s)\n", d->name, d->id);
	const clap_plugin_t *p = f->create_plugin(f, &host, d->id);
	p->init(p);
	if (!p->activate(p, rate, 1, FRAMES)) { fprintf(stderr, "activate failed\n"); return 1; }
	p->start_processing(p);

	const uint32_t total = (uint32_t)(rate * 6.0);
	float *L = calloc(total + FRAMES, sizeof(float)), *R = calloc(total + FRAMES, sizeof(float));
	for (uint32_t pos = 0; pos < total; pos += FRAMES)
	{
		events_t evs; memset(&evs, 0, sizeof(evs));
		const uint32_t on = (uint32_t)(rate * 1.0), off = (uint32_t)(rate * 3.0);
		for (int k = 0; k < 2; k ++)
		{
			const uint32_t at = k ? off : on;
			if (at >= pos && at < pos + FRAMES)
			{
				clap_event_note_t *e = &evs.ev[evs.n ++];
				e->header.size = sizeof(*e);
				e->header.time = at - pos;
				e->header.space_id = CLAP_CORE_EVENT_SPACE_ID;
				e->header.type = k ? CLAP_EVENT_NOTE_OFF : CLAP_EVENT_NOTE_ON;
				e->note_id = -1; e->port_index = 0; e->channel = 0; e->key = (int16_t)note;
				e->velocity = 100.0 / 127.0;
			}
		}
		clap_input_events_t in = { &evs, in_size, in_get };
		clap_output_events_t out = { NULL, out_push };
		float *chans[2] = { L + pos, R + pos };
		clap_audio_buffer_t ob = { chans, NULL, 2, 0, 0 };
		clap_process_t proc = { (int64_t)pos, FRAMES, NULL, NULL, &ob, 0, 1, &in, &out };
		p->process(p, &proc);
	}

	const clap_plugin_state_t *st = p->get_extension(p, CLAP_EXT_STATE);
	if (st)
	{
		buf_t b = { 0 };
		clap_ostream_t os = { &b, ostream_write };
		printf("state: %s, %zu bytes\n", st->save(p, &os) ? "saved" : "FAILED", b.n);
		free(b.p);
	}
	p->stop_processing(p);
	p->deactivate(p);
	p->destroy(p);
	entry->deinit();

	FILE *w = fopen(argv[2], "wb");
	const uint32_t bytes = total * 4;
	uint32_t v; uint16_t s;
	fwrite("RIFF", 1, 4, w); v = 36 + bytes; fwrite(&v, 4, 1, w); fwrite("WAVEfmt ", 1, 8, w);
	v = 16; fwrite(&v, 4, 1, w); s = 1; fwrite(&s, 2, 1, w); s = 2; fwrite(&s, 2, 1, w);
	v = (uint32_t)rate; fwrite(&v, 4, 1, w); v = (uint32_t)rate * 4; fwrite(&v, 4, 1, w);
	s = 4; fwrite(&s, 2, 1, w); s = 16; fwrite(&s, 2, 1, w); fwrite("data", 1, 4, w); fwrite(&bytes, 4, 1, w);
	float peak = 0;
	for (uint32_t i = 0; i < total; i ++)
		for (int c = 0; c < 2; c ++)
		{
			float x = c ? R[i] : L[i];
			if (x > peak) peak = x; if (-x > peak) peak = -x;
			int q = (int)(x * 32767.0f); if (q > 32767) q = 32767; if (q < -32768) q = -32768;
			int16_t q16 = (int16_t)q; fwrite(&q16, 2, 1, w);
		}
	fclose(w);
	printf("wrote %s, peak %.3f\n", argv[2], peak);
	return 0;
}
