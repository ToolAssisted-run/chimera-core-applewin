/* wbx-entry.cpp - the chimera guest ABI over applewin-driver.
 *
 * Compiles identically for the guest (miniBox emulibc) and for the native
 * reference build (native-shim/emulibc.h), which is what makes the
 * equivalence gate a real proof: the same driver, the same exports, one in
 * the sandbox and one out of it.
 */
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <emulibc.h>

#include "applewin-driver.h"

static char g_loadError[512];
static int g_inited;
static int g_vsyncNum = 60, g_vsyncDen = 1;

/* Two channels reach a core: a controller of 64 buttons or fewer arrives from
 * the frontend as a PACKED mask in FrameAdvance, while a wide controller (this
 * one: a keyboard) and the gate harness drive SetButton. Each keeps its own
 * state and a frame is their union, so neither can leave a key stuck down for
 * the other. */
static uint8_t g_setButtons[AW_BTN_COUNT];
static uint8_t g_packedButtons[AW_BTN_COUNT];

/* Turbo: the NTSC renderer is told to draw nothing. ECL_INVISIBLE because it
 * is the frontend's policy for the moment, not part of the machine. */
extern "C" { ECL_INVISIBLE int chimera_render_enabled = 1; }

extern "C" {

ECL_EXPORT const char *GetLoadError(void) { return g_loadError; }

ECL_EXPORT int Init(void)
{
	g_loadError[0] = '\0';
	if (!awdrv_init(g_loadError, sizeof(g_loadError))) return 0;
	awdrv_vsync(&g_vsyncNum, &g_vsyncDen);
	g_inited = 1;
	return 1;
}

ECL_EXPORT void SetButton(int32_t index, int32_t state)
{
	if (index >= 0 && index < AW_BTN_COUNT) g_setButtons[index] = state ? 1 : 0;
}

ECL_EXPORT void SetAxis(int32_t index, int32_t value)
{
	awdrv_set_axis(index, value);
}

ECL_EXPORT void FrameAdvance(uint64_t packed)
{
	for (int i = 0; i < AW_BTN_COUNT; i++)
	{
		g_packedButtons[i] = i < 64 ? (uint8_t)((packed >> i) & 1) : 0;
		awdrv_set_button(i, g_setButtons[i] | g_packedButtons[i]);
	}
	awdrv_frame(chimera_render_enabled);
}

ECL_EXPORT void SetRenderingEnabled(int on) { chimera_render_enabled = on != 0; }

ECL_EXPORT uint32_t *GetVideoBgra(void) { return const_cast<uint32_t *>(awdrv_video()); }
ECL_EXPORT int GetVideoWidth(void) { return AW_VIDEO_WIDTH; }
ECL_EXPORT int GetVideoHeight(void) { return AW_VIDEO_HEIGHT; }

ECL_EXPORT int16_t *GetAudio(void)
{
	int n;
	return const_cast<int16_t *>(awdrv_audio(&n));
}

ECL_EXPORT int GetAudioSampleCount(void)
{
	int n;
	awdrv_audio(&n);
	return n;
}

ECL_EXPORT int GetVsyncNumerator(void) { return g_vsyncNum; }
ECL_EXPORT int GetVsyncDenominator(void) { return g_vsyncDen; }

ECL_EXPORT int InputWasRead(void) { return awdrv_input_was_read(); }

/* memory domains */
ECL_EXPORT int GetMemoryDomainCount(void) { return awdrv_domain_count(); }
ECL_EXPORT const char *GetMemoryDomainName(int i) { return awdrv_domain_name(i); }
ECL_EXPORT uint8_t *GetMemoryDomainPtr(int i) { return awdrv_domain_ptr(i); }
ECL_EXPORT int64_t GetMemoryDomainSize(int i) { return awdrv_domain_size(i); }
ECL_EXPORT int GetMemoryDomainWritable(int i) { return i >= 0 && i < awdrv_domain_count() ? 1 : 0; }

/* drive lights and media */
ECL_EXPORT int32_t GetDriveCount(void) { return awdrv_drive_count(); }
ECL_EXPORT const char *GetDriveName(int32_t i) { return awdrv_drive_name(i); }
ECL_EXPORT int32_t GetDriveLight(int32_t i) { return awdrv_drive_light(i); }
ECL_EXPORT int32_t GetDriveMediaCount(int32_t i) { return awdrv_drive_media_count(i); }
ECL_EXPORT const char *GetDriveMediaName(int32_t i, int32_t n) { return awdrv_drive_media_name(i, n); }
ECL_EXPORT int32_t GetDriveMediaSelected(int32_t i) { return awdrv_drive_media_selected(i); }
ECL_EXPORT int32_t GetDriveMediaInserted(int32_t i) { return awdrv_drive_media_inserted(i); }

/* Save data: an Apple II's disks are its save data, and they are written into
 * the file overlay (libwindows/fileapi.cpp), which a savestate carries. There
 * is no separate file to export yet; the exports exist because the group is
 * all-or-nothing. */
ECL_EXPORT int32_t GetSaveDataFileCount(void) { return 0; }
ECL_EXPORT const char *GetSaveDataFileName(int32_t) { return nullptr; }
ECL_EXPORT int64_t GetSaveDataFileSize(int32_t) { return 0; }
ECL_EXPORT const uint8_t *GetSaveDataFileBuffer(int32_t) { return nullptr; }

/* the machine's own clock, for harnesses that compare machines */
ECL_EXPORT uint64_t GetCycleCount(void) { return awdrv_cycles(); }

} /* extern "C" */
