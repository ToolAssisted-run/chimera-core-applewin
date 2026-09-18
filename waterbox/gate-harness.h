/* gate-harness.h - the shared half of run-native.c and run-wbx.c.
 *
 * One replay-and-digest loop over an abstract core interface, so the native
 * reference and the sandboxed core run EXACTLY the same schedule - the same
 * typed text, the same pseudo-random key and joystick exercise - digesting
 * video, audio, lag, the 6502's cycle count and every memory domain per
 * frame. The two drivers differ only in how the exports are reached.
 *
 * Wire format: waterbox.config "input.buttons" order = AwButton in
 * applewin-driver.h (the IIe keyboard key by key, then the joystick buttons,
 * then the disk swaps); axes = AwAxis (two joysticks, 0..255).
 *
 * Descended from chimera-core-stella's harness.
 */
#ifndef GATE_HARNESS_H
#define GATE_HARNESS_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "applewin-driver.h"

#define GATE_BTN_COUNT AW_BTN_COUNT
#define GATE_AXIS_COUNT AW_AXIS_COUNT

struct gate_core
{
	int (*init)(void);
	const char *(*load_error)(void);
	void (*set_button)(int32_t index, int32_t state);
	void (*frame)(void);
	const uint32_t *(*video)(int *w, int *h);
	const int16_t *(*audio)(int *n);
	int (*input_was_read)(void);
	int (*domain_count)(void);
	const char *(*domain_name)(int i);
	const uint8_t *(*domain_ptr)(int i);
	int64_t (*domain_size)(int i);
	void (*set_axis)(int32_t index, int32_t value); /* NULL if the core has no axes */
	int (*vsync_numerator)(void);
	int (*vsync_denominator)(void);
	/* optional per-frame hook (the rerecord leg); may be NULL */
	void (*pre_frame)(void);
	/* optional: turn the core's drawing on and off (the turbo leg); may be NULL */
	void (*set_rendering)(int on);
	/* the machine's own cycle counter, so a run is compared as a machine and not only as a picture */
	uint64_t (*cycles)(void);
};

struct gate_opts
{
	long frames;          /* run length */
	const char *typeText; /* NULL, or text typed at the keyboard (\n = Return) */
	long typeAt;          /* the frame the typing starts on */
	long typeEvery;       /* frames per keystroke (held for half of them) */
	const char *screenshotPath; /* optional final-frame .tga */
	int exercise;         /* nonzero: random keys and a wandering joystick */
	const char *dumpDomain;     /* optional: memory domain to dump after the run... */
	const char *dumpPath;       /* ...into this file (the frontend gate compares it) */
	const char *textOut;        /* optional: the 40-column text page as text, after the run */
	long swapDiskAt;            /* >0: press Next Disk 1 on this frame */
	long resetAt;               /* >0: press Reset on this frame (a IIe with no disk boots forever) */
	int turbo;            /* nonzero: draw nothing for the first half of the run */
	long turboSettle;     /* frames to let the picture settle before hashing it */
};

static uint64_t gate_fnv(uint64_t h, const void *p, size_t n)
{
	const uint8_t *b = (const uint8_t *)p;
	if (!h) h = 1469598103934665603ULL;
	for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ULL; }
	return h;
}

/* A deterministic exercise, identical in both drivers, so the input path is
 * part of the comparison: every few frames one of a handful of harmless keys
 * (letters, digits, space, Return, the arrows) goes down for a couple of
 * frames, and the joysticks wander. Ctrl, Reset and the disk swaps stay up -
 * resetting the machine every few frames proves nothing. */
static void gate_exercise(long frame, uint8_t *buttons, int32_t *axes)
{
	static const int keys[] = {
		AW_BTN_A, AW_BTN_B, AW_BTN_C, AW_BTN_1, AW_BTN_2, AW_BTN_SPACE, AW_BTN_RETURN,
		AW_BTN_LEFT, AW_BTN_RIGHT, AW_BTN_UP, AW_BTN_DOWN, AW_BTN_P, AW_BTN_R, AW_BTN_SHIFT,
		AW_BTN_P1_BUTTON1, AW_BTN_P1_BUTTON2, AW_BTN_OPEN_APPLE, AW_BTN_ESCAPE,
	};
	const long slot = frame / 6;
	uint64_t x = (uint64_t)slot * 6364136223846793005ULL + 1442695040888963407ULL;
	x ^= x >> 33;
	const int key = keys[x % (sizeof keys / sizeof keys[0])];
	if (frame % 6 < 2) buttons[key] = 1;
	/* a second key, sometimes, so modifiers combine */
	if ((x >> 8) % 3 == 0 && frame % 6 < 3) buttons[keys[(x >> 16) % (sizeof keys / sizeof keys[0])]] = 1;

	uint64_t w = (uint64_t)frame * 2862933555777941757ULL + 3037000493ULL;
	w ^= w >> 29;
	axes[AW_AXIS_P1_X] = (int32_t)(w % 256);
	axes[AW_AXIS_P1_Y] = (int32_t)((w >> 8) % 256);
	axes[AW_AXIS_P2_X] = (int32_t)((w >> 16) % 256);
	axes[AW_AXIS_P2_Y] = (int32_t)((w >> 24) % 256);
}

/* typed text: one keystroke per typeEvery frames, held for half of them,
 * with Shift down for the glyphs that need it on the IIe's keyboard */
static int gate_typed_key(char c, int *shift)
{
	static const struct { char plain, shifted; int key; } table[] = {
		{ 'a', 'A', AW_BTN_A }, { 'b', 'B', AW_BTN_B }, { 'c', 'C', AW_BTN_C }, { 'd', 'D', AW_BTN_D },
		{ 'e', 'E', AW_BTN_E }, { 'f', 'F', AW_BTN_F }, { 'g', 'G', AW_BTN_G }, { 'h', 'H', AW_BTN_H },
		{ 'i', 'I', AW_BTN_I }, { 'j', 'J', AW_BTN_J }, { 'k', 'K', AW_BTN_K }, { 'l', 'L', AW_BTN_L },
		{ 'm', 'M', AW_BTN_M }, { 'n', 'N', AW_BTN_N }, { 'o', 'O', AW_BTN_O }, { 'p', 'P', AW_BTN_P },
		{ 'q', 'Q', AW_BTN_Q }, { 'r', 'R', AW_BTN_R }, { 's', 'S', AW_BTN_S }, { 't', 'T', AW_BTN_T },
		{ 'u', 'U', AW_BTN_U }, { 'v', 'V', AW_BTN_V }, { 'w', 'W', AW_BTN_W }, { 'x', 'X', AW_BTN_X },
		{ 'y', 'Y', AW_BTN_Y }, { 'z', 'Z', AW_BTN_Z },
		{ '1', '!', AW_BTN_1 }, { '2', '@', AW_BTN_2 }, { '3', '#', AW_BTN_3 }, { '4', '$', AW_BTN_4 },
		{ '5', '%', AW_BTN_5 }, { '6', '^', AW_BTN_6 }, { '7', '&', AW_BTN_7 }, { '8', '*', AW_BTN_8 },
		{ '9', '(', AW_BTN_9 }, { '0', ')', AW_BTN_0 }, { '-', '_', AW_BTN_MINUS }, { '=', '+', AW_BTN_EQUALS },
		{ '[', '{', AW_BTN_LBRACKET }, { ']', '}', AW_BTN_RBRACKET }, { '\\', '|', AW_BTN_BACKSLASH },
		{ ';', ':', AW_BTN_SEMICOLON }, { '\'', '"', AW_BTN_QUOTE }, { ',', '<', AW_BTN_COMMA },
		{ '.', '>', AW_BTN_PERIOD }, { '/', '?', AW_BTN_SLASH }, { '`', '~', AW_BTN_GRAVE },
		{ ' ', ' ', AW_BTN_SPACE }, { '\n', '\n', AW_BTN_RETURN }, { '\t', '\t', AW_BTN_TAB }, { 27, 27, AW_BTN_ESCAPE },
	};
	for (size_t i = 0; i < sizeof table / sizeof table[0]; i++)
	{
		if (table[i].plain == c) { *shift = 0; return table[i].key; }
		if (table[i].shifted == c && table[i].shifted != table[i].plain) { *shift = 1; return table[i].key; }
	}
	return -1;
}

static void gate_type(const struct gate_opts *o, long frame, uint8_t *buttons)
{
	if (!o->typeText || frame < o->typeAt) return;
	const long every = o->typeEvery > 0 ? o->typeEvery : 4;
	const long index = (frame - o->typeAt) / every;
	if (index >= (long)strlen(o->typeText)) return;
	if ((frame - o->typeAt) % every >= (every + 1) / 2) return;
	int shift = 0;
	const int key = gate_typed_key(o->typeText[index], &shift);
	if (key < 0) return;
	buttons[key] = 1;
	if (shift) buttons[AW_BTN_SHIFT] = 1;
}

static int gate_write_tga(const char *path, const uint32_t *bgra, int w, int h)
{
	FILE *f = fopen(path, "wb");
	if (!f) return 0;
	uint8_t hdr[18] = { 0 };
	hdr[2] = 2;
	hdr[12] = w & 0xff; hdr[13] = (w >> 8) & 0xff;
	hdr[14] = h & 0xff; hdr[15] = (h >> 8) & 0xff;
	hdr[16] = 32;
	hdr[17] = 0x20;
	fwrite(hdr, 1, 18, f);
	fwrite(bgra, 4, (size_t)w * h, f);
	fclose(f);
	return 1;
}

/* the loop both drivers share; prints the digest block to stdout */
static int gate_run(const struct gate_core *c, const struct gate_opts *o)
{
	if (c->init() != 1)
	{
		fprintf(stderr, "Init failed: %s\n", c->load_error ? c->load_error() : "?");
		return 1;
	}

	const long frames = o->frames;
	uint64_t vh = 0, ah = 0;
	/* the second half of the run, hashed separately: see the turbo hook */
	const long tail = frames / 2;
	const long hashFrom = tail + o->turboSettle;
	uint64_t th = 0;
	long lag = 0;
	uint8_t buttons[GATE_BTN_COUNT];
	uint8_t prev[GATE_BTN_COUNT];
	int32_t axes[GATE_AXIS_COUNT];
	memset(prev, 0, sizeof prev);

	for (long f = 0; f < frames; f++)
	{
		memset(buttons, 0, sizeof buttons);
		for (int a = 0; a < GATE_AXIS_COUNT; a++) axes[a] = 127;
		if (o->exercise) gate_exercise(f, buttons, axes);
		gate_type(o, f, buttons);
		if (o->swapDiskAt > 0 && f == o->swapDiskAt) buttons[AW_BTN_NEXT_DISK1] = 1;
		if (o->resetAt > 0 && f == o->resetAt) buttons[AW_BTN_RESET] = 1;

		/* turbo: draw nothing for the first half of the run, then draw the
		 * second half normally; the second half's pictures are what the
		 * turbo leg compares */
		if (o->turbo && c->set_rendering)
			c->set_rendering(f >= tail);

		if (c->pre_frame)
			c->pre_frame();

		for (int i = 0; i < GATE_BTN_COUNT; i++)
		{
			if (buttons[i] != prev[i])
				c->set_button(i, buttons[i]);
			prev[i] = buttons[i];
		}
		if (c->set_axis)
			for (int a = 0; a < GATE_AXIS_COUNT; a++) c->set_axis(a, axes[a]);

		c->frame();

		int w = 0, h = 0, n = 0;
		const uint32_t *video = c->video(&w, &h);
		const int16_t *audio = c->audio(&n);
		vh = gate_fnv(vh, &w, sizeof w);
		vh = gate_fnv(vh, &h, sizeof h);
		vh = gate_fnv(vh, video, (size_t)w * h * 4);
		if (f >= hashFrom)
		{
			th = gate_fnv(th, &w, sizeof w);
			th = gate_fnv(th, &h, sizeof h);
			th = gate_fnv(th, video, (size_t)w * h * 4);
		}
		ah = gate_fnv(ah, audio, (size_t)n * 2 * sizeof(int16_t));
		if (!c->input_was_read())
			lag++;
		if (o->screenshotPath && f == frames - 1)
			gate_write_tga(o->screenshotPath, video, w, h);
	}

	printf("frames=%ld\n", frames);
	printf("vsync=%d/%d\n", c->vsync_numerator(), c->vsync_denominator());
	printf("videoHash=%016llx\n", (unsigned long long)vh);
	printf("tailVideoHash=%016llx\n", (unsigned long long)th);
	printf("audioHash=%016llx\n", (unsigned long long)ah);
	printf("lagFrames=%ld\n", lag);
	if (c->cycles)
		printf("cycles=%llu\n", (unsigned long long)c->cycles());
	int nd = c->domain_count();
	for (int i = 0; i < nd; i++)
	{
		uint64_t dh = gate_fnv(0, c->domain_ptr(i), (size_t)c->domain_size(i));
		printf("domain[%s]=%016llx\n", c->domain_name(i), (unsigned long long)dh);
	}
	if (o->textOut)
	{
		/* the 40-column text page ($400-$7FF) as text: what the machine
		 * shows, readable by a person and comparable by a script */
		for (int i = 0; i < nd; i++)
		{
			if (strcmp(c->domain_name(i), "Main RAM") != 0) continue;
			const uint8_t *ram = c->domain_ptr(i);
			FILE *tf = fopen(o->textOut, "w");
			if (!tf) { perror(o->textOut); return 1; }
			for (int row = 0; row < 24; row++)
			{
				/* the text page's interleave: row r lives at $400 + (r%8)*128 + (r/8)*40 */
				const int base = 0x400 + (row % 8) * 128 + (row / 8) * 40;
				for (int col = 0; col < 40; col++)
				{
					/* $80-$FF normal, $00-$3F inverse, $40-$7F flash */
					const int b = ram[base + col];
					int ch = b & 0x7f;
					if (ch < 0x20) ch += 0x40;
					else if (b < 0x80 && ch >= 0x60) ch -= 0x40;
					if (ch < 0x20 || ch > 0x7e) ch = '?';
					fputc(ch, tf);
				}
				fputc('\n', tf);
			}
			fclose(tf);
		}
	}

	if (o->dumpDomain && o->dumpPath)
	{
		int found = 0;
		for (int i = 0; i < nd; i++)
		{
			if (strcmp(c->domain_name(i), o->dumpDomain) != 0)
				continue;
			FILE *f = fopen(o->dumpPath, "wb");
			if (!f) { perror(o->dumpPath); return 1; }
			fwrite(c->domain_ptr(i), 1, (size_t)c->domain_size(i), f);
			fclose(f);
			found = 1;
			break;
		}
		if (!found)
		{
			fprintf(stderr, "no such domain to dump: %s\n", o->dumpDomain);
			return 1;
		}
	}
	return 0;
}

/* shared CLI parsing; 0 on a bad argument */
static int gate_parse_opts(int argc, char **argv, int first, struct gate_opts *o)
{
	o->frames = 600;
	o->typeText = NULL;
	o->typeAt = 120;
	o->typeEvery = 4;
	o->screenshotPath = NULL;
	o->exercise = 0;
	o->dumpDomain = NULL;
	o->dumpPath = NULL;
	o->textOut = NULL;
	o->swapDiskAt = 0;
	o->resetAt = 0;
	o->turbo = 0;
	o->turboSettle = 0;
	for (int i = first; i < argc; i++)
	{
		if (!strcmp(argv[i], "--frames") && i + 1 < argc) o->frames = strtol(argv[++i], 0, 0);
		else if (!strcmp(argv[i], "--type") && i + 1 < argc) o->typeText = argv[++i];
		else if (!strcmp(argv[i], "--type-at") && i + 1 < argc) o->typeAt = strtol(argv[++i], 0, 0);
		else if (!strcmp(argv[i], "--type-every") && i + 1 < argc) o->typeEvery = strtol(argv[++i], 0, 0);
		else if (!strcmp(argv[i], "--screenshot") && i + 1 < argc) o->screenshotPath = argv[++i];
		else if (!strcmp(argv[i], "--exercise")) o->exercise = 1;
		else if (!strcmp(argv[i], "--dump-domain") && i + 2 < argc) { o->dumpDomain = argv[++i]; o->dumpPath = argv[++i]; }
		else if (!strcmp(argv[i], "--text") && i + 1 < argc) o->textOut = argv[++i];
		else if (!strcmp(argv[i], "--swap-disk-at") && i + 1 < argc) o->swapDiskAt = strtol(argv[++i], 0, 0);
		else if (!strcmp(argv[i], "--reset-at") && i + 1 < argc) o->resetAt = strtol(argv[++i], 0, 0);
		else if (!strcmp(argv[i], "--turbo")) o->turbo = 1;
		else if (!strcmp(argv[i], "--turbo-settle") && i + 1 < argc) o->turboSettle = strtol(argv[++i], 0, 0);
		else if (!strcmp(argv[i], "--rerecord")) ; /* run-wbx's; ignored here */
		else if (!strcmp(argv[i], "--session")) ; /* run-wbx's; ignored here */
		else { fprintf(stderr, "unknown argument %s\n", argv[i]); return 0; }
	}
	return 1;
}

#endif /* GATE_HARNESS_H */
