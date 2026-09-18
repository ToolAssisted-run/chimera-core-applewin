/* applewin-driver.h - the Apple II as a frame-stepped machine.
 *
 * The driver owns upstream AppleWin's globals the way its Win32 WinMain does:
 * it fills the registry from the chimera settings, builds the machine, and
 * runs it exactly one video frame of 6502 cycles per FrameAdvance. It is the
 * FrameBase (the "window") upstream talks to, the sound buffers upstream
 * writes into, and the registry upstream reads.
 *
 * wbx-entry.cpp is the guest ABI over this; the same driver runs in the native
 * reference build.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

/* ---- the wire: waterbox.config "input.buttons" order ---------------------
 * The Apple IIe keyboard, key by key, then the joystick buttons. See
 * applewin-driver.cpp's kKeys for the map. */
enum AwButton
{
	AW_BTN_RESET = 0,
	AW_BTN_ESCAPE,
	AW_BTN_1, AW_BTN_2, AW_BTN_3, AW_BTN_4, AW_BTN_5, AW_BTN_6, AW_BTN_7, AW_BTN_8, AW_BTN_9, AW_BTN_0,
	AW_BTN_MINUS, AW_BTN_EQUALS, AW_BTN_DELETE,
	AW_BTN_TAB,
	AW_BTN_Q, AW_BTN_W, AW_BTN_E, AW_BTN_R, AW_BTN_T, AW_BTN_Y, AW_BTN_U, AW_BTN_I, AW_BTN_O, AW_BTN_P,
	AW_BTN_LBRACKET, AW_BTN_RBRACKET, AW_BTN_BACKSLASH,
	AW_BTN_CONTROL,
	AW_BTN_A, AW_BTN_S, AW_BTN_D, AW_BTN_F, AW_BTN_G, AW_BTN_H, AW_BTN_J, AW_BTN_K, AW_BTN_L,
	AW_BTN_SEMICOLON, AW_BTN_QUOTE, AW_BTN_RETURN,
	AW_BTN_SHIFT,
	AW_BTN_Z, AW_BTN_X, AW_BTN_C, AW_BTN_V, AW_BTN_B, AW_BTN_N, AW_BTN_M,
	AW_BTN_COMMA, AW_BTN_PERIOD, AW_BTN_SLASH,
	AW_BTN_CAPSLOCK,
	AW_BTN_OPEN_APPLE, AW_BTN_SOLID_APPLE,
	AW_BTN_SPACE,
	AW_BTN_GRAVE,
	AW_BTN_LEFT, AW_BTN_RIGHT, AW_BTN_UP, AW_BTN_DOWN,
	AW_BTN_P1_BUTTON1, AW_BTN_P1_BUTTON2,
	AW_BTN_P2_BUTTON1,
	AW_BTN_NEXT_DISK1, AW_BTN_NEXT_DISK2,
	AW_BTN_COUNT
};

/* axes: waterbox.config "input.axes" order (0..255, 127 centred) */
enum AwAxis
{
	AW_AXIS_P1_X = 0,
	AW_AXIS_P1_Y,
	AW_AXIS_P2_X,
	AW_AXIS_P2_Y,
	AW_AXIS_COUNT
};

/* the picture: the Apple's 560x384 without AppleWin's window border */
#define AW_VIDEO_WIDTH 560
#define AW_VIDEO_HEIGHT 384

/* the most samples one frame can hand back (a PAL frame at 44100 is 882) */
#define AW_AUDIO_MAX_SAMPLES 4096

/* builds the machine from the mounted settings and slots; 0 with a message on failure */
int awdrv_init(char *error, size_t errorLen);

/* the frame's input, then one frame of cycles; render=0 draws nothing (turbo) */
void awdrv_set_button(int index, int down);
void awdrv_set_axis(int index, int value);
void awdrv_frame(int render);

const uint32_t *awdrv_video(void);
const int16_t *awdrv_audio(int *sampleCount);
int awdrv_input_was_read(void);
void awdrv_vsync(int *numerator, int *denominator);

/* memory domains */
int awdrv_domain_count(void);
const char *awdrv_domain_name(int i);
uint8_t *awdrv_domain_ptr(int i);
int64_t awdrv_domain_size(int i);

/* drive lights: one per drive the machine has; the media names are the slot's */
int awdrv_drive_count(void);
const char *awdrv_drive_name(int i);
int awdrv_drive_light(int i);
int awdrv_drive_media_count(int i);
const char *awdrv_drive_media_name(int i, int n);
int awdrv_drive_media_selected(int i);
int awdrv_drive_media_inserted(int i);

/* the 6502's own cycle counter (the gate compares machines, not just pictures) */
uint64_t awdrv_cycles(void);
