/* applewin-driver.cpp - upstream AppleWin, frame-stepped.
 *
 * What upstream's Windows front end does across WinMain, the frame window and
 * the message loop, this does in one place and with one clock: the 6502's.
 *
 *   - FrameBase (the window) is ChimeraFrame: a framebuffer, the embedded
 *     ROM table, sound buffers that are plain rings, no message box, no
 *     network.
 *   - The registry is filled from the chimera settings before the machine is
 *     built (waterbox/shims/Registry.cpp), so LoadConfiguration() reads the
 *     project's machine as it would a user's.
 *   - A frame is NTSC_GetCyclesPerFrame() cycles (17030 at 60 Hz, 20280 at
 *     50 Hz), run in the same 1 ms batches upstream's ContinueExecution()
 *     uses, with no speed regulation, no full-speed mode and no audio
 *     feedback into the cycle count: the machine runs the same cycles for the
 *     same inputs on any host, which is the whole point.
 *   - Input is the chimera keyboard controller translated into what the
 *     Win32 window would have sent upstream's Keyboard.cpp (WM_CHAR and
 *     WM_KEYDOWN), and joysticks that libwindows' winmm reports to upstream's
 *     Joystick.cpp. Both upstream files are compiled unmodified.
 *
 * Determinism notes (all of them found by reading, none patched upstream):
 * the clocks in libwindows/timeapi.cpp are emulated; rand() is pinned in
 * libwindows/wincompat.h and seeded from a setting; disk writes go to the
 * file overlay in libwindows/fileapi.cpp.
 */
#include "StdAfx.h"

#include "applewin-driver.h"
#include "chimera-registry.h"

#include <emulibc.h>
#include <waterbox_settings.h>
#include <waterbox_slots.h>

#include "Core.h"
#include "CardManager.h"
#include "CPU.h"
#include "Debugger/Debug.h"
#include "Disk.h"
#include "Disk2CardManager.h"
#include "FrameBase.h"
#include "Harddisk.h"
#include "Interface.h"
#include "Joystick.h"
#include "Keyboard.h"
#include "Log.h"
#include "Memory.h"
#include "Mockingboard.h"
#include "MockingboardCardManager.h"
#include "NTSC.h"
#include "Registry.h"
#include "RGBMonitor.h"
#include "SaveState.h"
#include "SoundBuffer.h"
#include "SoundCore.h"
#include "Speaker.h"
#include "Tfe/NetworkBackend.h"
#include "Utilities.h"
#include "Video.h"
#include "Windows/AppleWin.h"
#include "Configuration/IPropertySheet.h"
#include "../resource/resource.h"

#include <memory>
#include <string>
#include <vector>

/* ---------------------------------------------------------------------------
 * The pinned generator behind rand() (libwindows/wincompat.h). A 31-bit LCG
 * of the C standard's own example: the values do not matter, their being the
 * same on every host does.
 */
static uint32_t g_randState = 1;

extern "C" int chimera_rand(void)
{
	g_randState = g_randState * 1103515245u + 12345u;
	return (int)((g_randState >> 1) & 0x7fffffff);
}

extern "C" void chimera_srand(unsigned seed)
{
	g_randState = seed;
}

/* ---------------------------------------------------------------------------
 * The emulated clock (libwindows/timeapi.cpp). g_nCumulativeCycles is the
 * 6502's own count; the epoch is the sandbox's frozen day, so the No-Slot
 * Clock reads a date that moves with the machine and not with the wall.
 */
static const int64_t kEpochSeconds = 1495889068; /* the sandbox's clock_gettime */

extern "C" uint32_t chimera_emulated_ms(void)
{
	const double clk = g_fCurrentCLK6502 > 0 ? g_fCurrentCLK6502 : CLK_6502_NTSC;
	return (uint32_t)((g_nCumulativeCycles * 1000ull) / (uint64_t)clk);
}

extern "C" int64_t chimera_emulated_epoch_seconds(void)
{
	const double clk = g_fCurrentCLK6502 > 0 ? g_fCurrentCLK6502 : CLK_6502_NTSC;
	return kEpochSeconds + (int64_t)(g_nCumulativeCycles / (uint64_t)clk);
}

/* ---------------------------------------------------------------------------
 * The embedded resources (waterbox/gen-resources.py).
 */
struct ChimeraResource { uint16_t id; uint32_t size; const uint8_t *data; };
extern const ChimeraResource chimera_resources[];
extern const size_t chimera_resource_count;

static const ChimeraResource *FindResource(WORD id)
{
	for (size_t i = 0; i < chimera_resource_count; i++)
		if (chimera_resources[i].id == id) return &chimera_resources[i];
	return nullptr;
}

/* ---------------------------------------------------------------------------
 * Sound buffers. Upstream writes the speaker and the Mockingboard into
 * DirectSound ring buffers by locking a region at its own write offset and
 * asking where the play cursor is; the play cursor of a real card moves with
 * the wall clock. Here it moves when the driver reads, once per frame, so the
 * machine's sound is a function of its frames.
 */
class ChimeraSoundBuffer : public SoundBuffer
{
public:
	ChimeraSoundBuffer(uint32_t size, uint32_t rate, int channels, const char *name)
		: m_buffer(size, 0)
		, m_size(size)
		, m_rate(rate)
		, m_channels(channels)
		, m_name(name ? name : "")
	{
	}

	HRESULT SetCurrentPosition(DWORD) override { return DS_OK; }

	HRESULT GetCurrentPosition(LPDWORD play, LPDWORD write) override
	{
		if (play) *play = (DWORD)m_play;
		if (write) *write = (DWORD)m_write;
		return DS_OK;
	}

	HRESULT Lock(DWORD cursor, DWORD bytes, LPVOID *p1, DWORD *n1, LPVOID *p2, DWORD *n2, DWORD flags) override
	{
		if (flags & DSBLOCK_ENTIREBUFFER)
		{
			m_lockStart = 0;
			*p1 = m_buffer.data();
			*n1 = (DWORD)m_size;
			if (p2) *p2 = nullptr;
			if (n2) *n2 = 0;
			return DS_OK;
		}
		cursor %= m_size;
		m_lockStart = cursor;
		const DWORD first = (DWORD)(m_size - cursor);
		*p1 = m_buffer.data() + cursor;
		*n1 = first < bytes ? first : bytes;
		if (p2 && n2)
		{
			if (*n1 < bytes)
			{
				*p2 = m_buffer.data();
				*n2 = bytes - *n1;
			}
			else
			{
				*p2 = nullptr;
				*n2 = 0;
			}
		}
		return DS_OK;
	}

	HRESULT Unlock(LPVOID, DWORD n1, LPVOID, DWORD n2) override
	{
		/* the write cursor is the end of what was last written */
		m_write = (m_lockStart + n1 + n2) % m_size;
		return DS_OK;
	}

	HRESULT Stop() override
	{
		m_status &= ~(DSBSTATUS_PLAYING | DSBSTATUS_LOOPING);
		return DS_OK;
	}

	HRESULT Play(DWORD, DWORD, DWORD flags) override
	{
		m_status |= DSBSTATUS_PLAYING;
		if (flags & DSBPLAY_LOOPING) m_status |= DSBSTATUS_LOOPING;
		return DS_OK;
	}

	HRESULT SetVolume(LONG volume) override { m_volume = volume; return DS_OK; }
	HRESULT GetVolume(LONG *volume) override { *volume = m_volume; return DS_OK; }
	HRESULT GetStatus(LPDWORD status) override { *status = m_status; return DS_OK; }
	HRESULT Restore() override { return DS_OK; }

	bool playing() const { return (m_status & DSBSTATUS_PLAYING) != 0; }
	bool muted() const { return m_volume <= DSBVOLUME_MIN; }
	uint32_t rate() const { return m_rate; }
	int channels() const { return m_channels; }
	const std::string &name() const { return m_name; }

	/* bytes between the play cursor and the write cursor */
	size_t pending() const
	{
		return m_write >= m_play ? m_write - m_play : m_size - (m_play - m_write);
	}

	/* consumes up to n bytes at the play cursor into out; returns the count */
	size_t read(uint8_t *out, size_t n)
	{
		const size_t avail = pending();
		if (n > avail) n = avail;
		size_t done = 0;
		while (done < n)
		{
			const size_t first = m_size - m_play;
			const size_t take = first < n - done ? first : n - done;
			memcpy(out + done, m_buffer.data() + m_play, take);
			m_play = (m_play + take) % m_size;
			done += take;
		}
		return done;
	}

private:
	std::vector<uint8_t> m_buffer;
	size_t m_size;
	uint32_t m_rate;
	int m_channels;
	std::string m_name;
	size_t m_play = 0;
	size_t m_write = 0;
	size_t m_lockStart = 0;
	DWORD m_status = 0;
	LONG m_volume = DSBVOLUME_MAX;
};

/* the voices upstream has open, weak: their owners (VOICE structs) hold them */
static std::vector<std::weak_ptr<ChimeraSoundBuffer>> g_voices;

bool DSAvailable()
{
	return true;
}

/* ---------------------------------------------------------------------------
 * A network card with the cable unplugged.
 */
class NoNetwork : public NetworkBackend
{
public:
	void transmit(const int, uint8_t *) override {}
	int receive(const int, uint8_t *) override { return -1; }
	void update(const ULONG) override {}
	void getMACAddress(const uint32_t, MACAddress &) override {}
	bool isValid() override { return false; }
	const std::string &getInterfaceName() override { return m_name; }

private:
	std::string m_name;
};

/* ---------------------------------------------------------------------------
 * The property sheet: upstream's Win32 configuration dialog, reduced to the
 * values the emulator asks it for.
 */
class ChimeraPropertySheet : public IPropertySheet
{
public:
	void Init() override {}
	uint32_t GetVolumeMax() override { return 99; }
	bool SaveStateSelectImage(HWND, bool) override { return false; }
	void ResetAllToDefault() override {}
	void ApplyConfigAfterClose(UINT) override {}
	void ApplyNewConfigFromSnapshot() override {}
	void ConfigSaveApple2Type(eApple2Type type) override { REGSAVE(REGVALUE_APPLE2_TYPE, type); }

	UINT GetScrollLockToggle() override { return 0; }
	void SetScrollLockToggle(UINT) override {}
	UINT GetJoystickCursorControl() override { return 0; }
	void SetJoystickCursorControl(UINT) override {}
	UINT GetJoystickCenteringControl() override { return m_centering; }
	void SetJoystickCenteringControl(UINT v) override { m_centering = v; }
	UINT GetAutofire(UINT button) override { return (m_autofire >> button) & 1; }
	UINT GetAutofire() override { return m_autofire; }
	void SetAutofire(UINT v) override { m_autofire = v; }
	bool GetButtonsSwapState() override { return m_swapButtons; }
	void SetButtonsSwapState(bool v) override { m_swapButtons = v; }
	UINT GetMouseShowCrosshair() override { return 0; }
	void SetMouseShowCrosshair(UINT) override {}
	UINT GetMouseRestrictToWindow() override { return 0; }
	void SetMouseRestrictToWindow(UINT) override {}
	UINT GetTheFreezesF8Rom() override { return m_freezesF8; }
	void SetTheFreezesF8Rom(UINT v) override { m_freezesF8 = v; }

private:
	UINT m_centering = JOYSTICK_MODE_CENTERING;
	UINT m_autofire = 0;
	bool m_swapButtons = false;
	UINT m_freezesF8 = 0;
};

/* ---------------------------------------------------------------------------
 * The frame: upstream's window, headless.
 */
class ChimeraFrame : public FrameBase
{
public:
	void Initialize(bool resetVideoState) override
	{
		Video &video = GetVideo();
		const size_t pixels = (size_t)video.GetFrameBufferWidth() * video.GetFrameBufferHeight();
		m_framebuffer.assign(pixels * sizeof(bgra_t), 0);
		video.Initialize(m_framebuffer.data(), resetVideoState);
		LogFileTimeUntilFirstKeyReadReset();
	}

	void Destroy() override
	{
		GetVideo().Destroy();
		m_framebuffer.clear();
	}

	void FrameDrawDiskLEDS() override {}
	void FrameDrawDiskStatus() override {}
	void FrameRefreshStatus(int) override {}
	void FrameUpdateApple2Type() override {}
	void FrameSetCursorPosByMousePos() override {}
	bool GetFullScreenShowSubunitStatus() override { return false; }
	void SetFullScreenShowSubunitStatus(bool) override {}
	void SetWindowedModeShowDiskiiStatus(bool) override {}
	bool GetBestDisplayResolutionForFullScreen(UINT &, UINT &, UINT, UINT) override { return false; }
	int SetViewportScale(int scale, bool) override { return scale; }
	void SetAltEnterToggleFullScreen(bool) override {}
	void SetLoadedSaveStateFlag(const bool) override {}
	void VideoPresentScreen() override {}
	void ResizeWindow() override {}

	int FrameMessageBox(LPCSTR text, LPCSTR caption, UINT type) override
	{
		fprintf(stderr, "[applewin] %s: %s\n", caption ? caption : "", text ? text : "");
		/* a question is answered the way that changes nothing */
		if ((type & 0xF) == MB_YESNO || (type & 0xF) == MB_YESNOCANCEL) return IDNO;
		if ((type & 0xF) == MB_OKCANCEL) return IDCANCEL;
		return IDOK;
	}

	/* a 1-bit .bmp's rows, bottom-up as GetBitmapBits hands them over: the
	 * clone character sets (NTSC_CharSet.cpp) */
	void GetBitmap(WORD id, LONG cb, LPVOID bits) override
	{
		const ChimeraResource *res = FindResource(id);
		if (!res) throw std::runtime_error("no bitmap resource " + std::to_string(id));
		const uint8_t *b = res->data;
		if (res->size < 54 || b[0] != 'B' || b[1] != 'M') throw std::runtime_error("bad bitmap resource");
		auto u32 = [&](size_t at) { return (uint32_t)b[at] | ((uint32_t)b[at + 1] << 8) | ((uint32_t)b[at + 2] << 16) | ((uint32_t)b[at + 3] << 24); };
		const uint32_t offset = u32(10);
		const int32_t height = (int32_t)u32(22);
		uint32_t imageSize = u32(34);
		if (imageSize == 0) imageSize = res->size - offset;
		if (height <= 0 || offset + imageSize > res->size || (LONG)imageSize > cb)
			throw std::runtime_error("unexpected bitmap resource layout");
		const size_t stride = imageSize / height;
		uint8_t *out = (uint8_t *)bits;
		for (int32_t row = 0; row < height; row++)
			memcpy(out + (size_t)(height - row - 1) * stride, b + offset + (size_t)row * stride, stride);
	}

	std::shared_ptr<NetworkBackend> CreateNetworkBackend(const std::string &) override
	{
		return std::make_shared<NoNetwork>();
	}

	std::shared_ptr<SoundBuffer> CreateSoundBuffer(uint32_t size, uint32_t rate, int channels, const char *name) override
	{
		const uint32_t frame = channels * sizeof(int16_t);
		const uint32_t aligned = ((size + frame - 1) / frame) * frame;
		auto voice = std::make_shared<ChimeraSoundBuffer>(aligned, rate, channels, name);
		g_voices.push_back(voice);
		return voice;
	}

	BYTE *GetResource(WORD id, LPCSTR, uint32_t expectedSize) override
	{
		const ChimeraResource *res = FindResource(id);
		if (!res || res->size != expectedSize)
		{
			fprintf(stderr, "[applewin] resource %u: %s\n", id, res ? "wrong size" : "missing");
			return nullptr;
		}
		return const_cast<BYTE *>(res->data);
	}

	void Restart() override {}
	std::string Video_GetScreenShotFolder() const override { return ""; }

	const uint8_t *framebuffer() const { return m_framebuffer.data(); }

private:
	std::vector<uint8_t> m_framebuffer;
};

static ChimeraFrame g_frame;
static ChimeraPropertySheet g_propertySheet;

FrameBase &GetFrame() { return g_frame; }
IPropertySheet &GetPropertySheet() { return g_propertySheet; }

Video &GetVideo()
{
	static Video video;
	return video;
}

/* misc.h's MessageBox: upstream's file layer calls the Win32 one directly */
int MessageBox(HWND, LPCSTR text, LPCSTR caption, UINT type)
{
	return GetFrame().FrameMessageBox(text, caption, type);
}

/* Windows/AppleWin.h: what the Win32 front end exports to the core */
bool g_bRestartFullScreen = false;
bool GetLoadedSaveStateFlag() { return false; }
bool GetHookAltTab() { return false; }
bool GetHookAltGrControl() { return false; }
bool GetFullScreenResolutionChangedByUser() { return false; }
void SingleStep(bool) {}

/* ---------------------------------------------------------------------------
 * Input state.
 */
static uint8_t g_held[AW_BTN_COUNT];      /* this frame's levels */
static uint8_t g_heldLast[AW_BTN_COUNT];  /* last frame's, for edges */
static int g_heldFrames[AW_BTN_COUNT];    /* how long a key has been down (auto-repeat) */
static int g_axis[AW_AXIS_COUNT] = { 127, 127, 127, 127 };
static bool g_capsLock = true;            /* the IIe's latching key; AppleWin boots with it on */
static bool g_joystickPlugged[2] = { true, false };
static bool g_inputRead;

/* a key of the wire: which Win32 virtual key it is (for AKD and GetKeyState),
 * and the ASCII it types plain and shifted (0 = not a typing key) */
struct KeyDef
{
	uint8_t vk;
	uint8_t plain;
	uint8_t shifted;
};

static const KeyDef kKeys[AW_BTN_COUNT] = {
	/* AW_BTN_RESET */      { 0, 0, 0 },
	/* ESCAPE */            { VK_ESCAPE, 0x1B, 0x1B },
	/* 1..0 */              { '1', '1', '!' }, { '2', '2', '@' }, { '3', '3', '#' }, { '4', '4', '$' }, { '5', '5', '%' },
	                        { '6', '6', '^' }, { '7', '7', '&' }, { '8', '8', '*' }, { '9', '9', '(' }, { '0', '0', ')' },
	/* MINUS EQUALS */      { VK_OEM_MINUS, '-', '_' }, { VK_OEM_PLUS, '=', '+' },
	/* DELETE */            { VK_DELETE, 0, 0 },
	/* TAB */               { VK_TAB, 0x09, 0x09 },
	/* Q..P */              { 'Q', 'q', 'Q' }, { 'W', 'w', 'W' }, { 'E', 'e', 'E' }, { 'R', 'r', 'R' }, { 'T', 't', 'T' },
	                        { 'Y', 'y', 'Y' }, { 'U', 'u', 'U' }, { 'I', 'i', 'I' }, { 'O', 'o', 'O' }, { 'P', 'p', 'P' },
	/* [ ] \ */             { VK_OEM_4, '[', '{' }, { VK_OEM_6, ']', '}' }, { VK_OEM_5, '\\', '|' },
	/* CONTROL */           { VK_CONTROL, 0, 0 },
	/* A..L */              { 'A', 'a', 'A' }, { 'S', 's', 'S' }, { 'D', 'd', 'D' }, { 'F', 'f', 'F' }, { 'G', 'g', 'G' },
	                        { 'H', 'h', 'H' }, { 'J', 'j', 'J' }, { 'K', 'k', 'K' }, { 'L', 'l', 'L' },
	/* ; ' RETURN */        { VK_OEM_1, ';', ':' }, { VK_OEM_7, '\'', '"' }, { VK_RETURN, 0x0D, 0x0D },
	/* SHIFT */             { VK_SHIFT, 0, 0 },
	/* Z..M */              { 'Z', 'z', 'Z' }, { 'X', 'x', 'X' }, { 'C', 'c', 'C' }, { 'V', 'v', 'V' }, { 'B', 'b', 'B' },
	                        { 'N', 'n', 'N' }, { 'M', 'm', 'M' },
	/* , . / */             { VK_OEM_COMMA, ',', '<' }, { VK_OEM_PERIOD, '.', '>' }, { VK_OEM_2, '/', '?' },
	/* CAPSLOCK */          { VK_CAPITAL, 0, 0 },
	/* OPEN/SOLID APPLE */  { VK_LMENU, 0, 0 }, { VK_RMENU, 0, 0 },
	/* SPACE */             { VK_SPACE, ' ', ' ' },
	/* GRAVE */             { VK_OEM_3, '`', '~' },
	/* LEFT RIGHT UP DOWN */{ VK_LEFT, 0, 0 }, { VK_RIGHT, 0, 0 }, { VK_UP, 0, 0 }, { VK_DOWN, 0, 0 },
	/* P1 B1 B2, P2 B1 */   { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 },
	/* NEXT DISK 1, 2 */    { 0, 0, 0 }, { 0, 0, 0 },
};

/* what GetKeyState() tells upstream (Keyboard.cpp's modifiers, Joystick.cpp's
 * SHIFT-key mod, KeybToggleCapsLock's toggle bit) */
extern "C" int chimera_key_state(int vk)
{
	if (vk == VK_CAPITAL) return g_capsLock ? 1 : 0;
	bool down = false;
	switch (vk)
	{
		case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT: down = g_held[AW_BTN_SHIFT]; break;
		case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL: down = g_held[AW_BTN_CONTROL]; break;
		case VK_MENU: down = g_held[AW_BTN_OPEN_APPLE] || g_held[AW_BTN_SOLID_APPLE]; break;
		case VK_LMENU: down = g_held[AW_BTN_OPEN_APPLE]; break;
		case VK_RMENU: down = g_held[AW_BTN_SOLID_APPLE]; break;
		case VK_SCROLL: case VK_NUMLOCK: down = false; break;
		default:
			for (int i = 0; i < AW_BTN_COUNT; i++)
				if (kKeys[i].vk == vk && g_held[i]) { down = true; break; }
			break;
	}
	return down ? -128 : 0;
}

/* what joyGetPos() tells upstream: the Apple's pushbuttons are PB0 (open
 * apple, joystick 1 button 1), PB1 (solid apple, joystick 1 button 2) and
 * PB2 (joystick 2's button, which upstream re-maps itself) */
extern "C" int chimera_joystick_state(unsigned index, unsigned *x, unsigned *y, unsigned *buttons)
{
	if (index >= 2 || !g_joystickPlugged[index]) return 0;
	if (index == 0)
	{
		*x = (unsigned)g_axis[AW_AXIS_P1_X];
		*y = (unsigned)g_axis[AW_AXIS_P1_Y];
		*buttons = ((g_held[AW_BTN_P1_BUTTON1] || g_held[AW_BTN_OPEN_APPLE]) ? JOY_BUTTON1 : 0)
			| ((g_held[AW_BTN_P1_BUTTON2] || g_held[AW_BTN_SOLID_APPLE]) ? JOY_BUTTON2 : 0);
	}
	else
	{
		*x = (unsigned)g_axis[AW_AXIS_P2_X];
		*y = (unsigned)g_axis[AW_AXIS_P2_Y];
		*buttons = g_held[AW_BTN_P2_BUTTON1] ? JOY_BUTTON1 : 0;
	}
	return 1;
}

/* ---------------------------------------------------------------------------
 * The keyboard, typed. A key going down is what the Win32 window would have
 * posted: WM_KEYDOWN for the AKD flag, then WM_CHAR with the character the
 * layout gives it - Ctrl makes a control code, Shift the shifted glyph, and
 * caps lock is upstream's own (KeybQueueKeypress uppercases). A key held
 * repeats as the IIe's keyboard encoder does: after half a second, fifteen
 * times a second.
 */
static const int kRepeatDelayFrames = 32;
static const int kRepeatPeriodFrames = 4;

static void TypeKey(int index)
{
	const KeyDef &k = kKeys[index];
	const bool shift = g_held[AW_BTN_SHIFT];
	const bool ctrl = g_held[AW_BTN_CONTROL];

	switch (index)
	{
		case AW_BTN_LEFT: case AW_BTN_RIGHT: case AW_BTN_UP: case AW_BTN_DOWN: case AW_BTN_DELETE:
			KeybQueueKeypress(k.vk, NOT_ASCII);
			return;
		default:
			break;
	}
	if (k.plain == 0) return;

	uint8_t ch = shift ? k.shifted : k.plain;
	if (ctrl)
	{
		/* a control code for a letter; the few others the IIe makes */
		if (k.plain >= 'a' && k.plain <= 'z') ch = (uint8_t)(k.plain - 'a' + 1);
		else if (k.plain == '[') ch = 0x1B;
		else if (k.plain == '\\') ch = 0x1C;
		else if (k.plain == ']') ch = 0x1D;
		else if (k.plain == '6') ch = 0x1E;
		else if (k.plain == '-') ch = 0x1F;
		else if (k.plain == '@' || (k.plain == '2' && shift)) ch = 0x00;
	}
	KeybQueueKeypress(ch, ASCII);
}

static void UpdateKeyboard(void)
{
	for (int i = 0; i < AW_BTN_COUNT; i++)
	{
		const bool down = g_held[i] != 0;
		const bool was = g_heldLast[i] != 0;
		const KeyDef &k = kKeys[i];

		if (down && !was)
		{
			g_heldFrames[i] = 0;
			if (k.vk) KeybAnyKeyDown(WM_KEYDOWN, k.vk, false);
			if (i == AW_BTN_CAPSLOCK)
			{
				g_capsLock = !g_capsLock;
				KeybToggleCapsLock();
			}
			else if (i == AW_BTN_RESET)
			{
				/* Ctrl-Reset: the key on the IIe's keyboard, with or without Ctrl held */
				GetFrame().g_bFreshReset = true;
				CtrlReset();
			}
			else
			{
				TypeKey(i);
			}
		}
		else if (down && was)
		{
			g_heldFrames[i]++;
			if (g_heldFrames[i] >= kRepeatDelayFrames && ((g_heldFrames[i] - kRepeatDelayFrames) % kRepeatPeriodFrames) == 0)
				TypeKey(i);
		}
		else if (!down && was)
		{
			if (k.vk) KeybAnyKeyDown(WM_KEYUP, k.vk, false);
		}
		g_heldLast[i] = g_held[i];
	}
}

/* ---------------------------------------------------------------------------
 * The disks: what the project mounted, and which of them is in each drive.
 */
struct DriveState
{
	std::vector<std::string> media; /* the slot's names, in swap order */
	int selected = -1;
	std::string name;
};

static DriveState g_drives[4]; /* Disk II 1 and 2, hard disk 1 and 2 */
static const char *kDriveNames[4] = { "Disk II 1", "Disk II 2", "Hard Disk 1", "Hard Disk 2" };

static void ReadSlot(const char *id, std::vector<std::string> &out)
{
	const int n = wbx_slot_count(id);
	for (int i = 0; i < n; i++)
	{
		char name[512];
		if (wbx_slot_name(id, i, name, sizeof(name))) out.push_back(name);
	}
}

static bool InsertFloppy(int drive, int index)
{
	if (GetCardMgr().QuerySlot(SLOT6) != CT_Disk2) return false;
	Disk2InterfaceCard &card = dynamic_cast<Disk2InterfaceCard &>(GetCardMgr().GetRef(SLOT6));
	DriveState &d = g_drives[drive];
	if (index < 0 || index >= (int)d.media.size()) return false;
	const ImageError_e err = card.InsertDisk(drive, d.media[index], false, false);
	if (err != eIMAGE_ERROR_NONE)
	{
		fprintf(stderr, "[applewin] drive %d: cannot insert '%s' (error %d)\n", drive + 1, d.media[index].c_str(), (int)err);
		return false;
	}
	d.selected = index;
	return true;
}

/* ---------------------------------------------------------------------------
 * Settings -> registry. The machine is described to upstream the way its own
 * configuration dialog would have written it.
 */
static void SettingStr(const char *key, const char *dflt, char *out, int outsz)
{
	strncpy(out, dflt, outsz - 1);
	out[outsz - 1] = 0;
	wbx_setting_str(key, out, outsz);
}

static eApple2Type ModelFromSetting(const char *s)
{
	if (!strcmp(s, "apple2")) return A2TYPE_APPLE2;
	if (!strcmp(s, "apple2plus")) return A2TYPE_APPLE2PLUS;
	if (!strcmp(s, "apple2jplus")) return A2TYPE_APPLE2JPLUS;
	if (!strcmp(s, "apple2e")) return A2TYPE_APPLE2E;
	if (!strcmp(s, "pravets82")) return A2TYPE_PRAVETS82;
	if (!strcmp(s, "pravets8m")) return A2TYPE_PRAVETS8M;
	if (!strcmp(s, "pravets8a")) return A2TYPE_PRAVETS8A;
	if (!strcmp(s, "tk3000")) return A2TYPE_TK30002E;
	if (!strcmp(s, "base64a")) return A2TYPE_BASE64A;
	return A2TYPE_APPLE2EENHANCED;
}

static VideoType_e VideoFromSetting(const char *s)
{
	if (!strcmp(s, "colorMonitor")) return VT_COLOR_MONITOR_NTSC;
	if (!strcmp(s, "colorIdealized")) return VT_COLOR_IDEALIZED;
	if (!strcmp(s, "rgbCard")) return VT_COLOR_VIDEOCARD_RGB;
	if (!strcmp(s, "monoTv")) return VT_MONO_TV;
	if (!strcmp(s, "monoAmber")) return VT_MONO_AMBER;
	if (!strcmp(s, "monoGreen")) return VT_MONO_GREEN;
	if (!strcmp(s, "monoWhite")) return VT_MONO_WHITE;
	return VT_COLOR_TV;
}

static int MemoryPatternFromSetting(const char *s)
{
	if (!strcmp(s, "zero")) return MIP_ZERO;
	if (!strcmp(s, "random")) return MIP_RANDOM;
	if (!strcmp(s, "ff00FullPage")) return MIP_FF_00_FULL_PAGE;
	if (!strcmp(s, "00ffHalfPage")) return MIP_00_FF_HALF_PAGE;
	if (!strcmp(s, "ff00HalfPage")) return MIP_FF_00_HALF_PAGE;
	if (!strcmp(s, "pageAddressLow")) return MIP_PAGE_ADDRESS_LOW;
	if (!strcmp(s, "pageAddressHigh")) return MIP_PAGE_ADDRESS_HIGH;
	return MIP_FF_FF_00_00;
}

static void FillRegistry(void)
{
	char s[64];

	SettingStr("model", "apple2eEnhanced", s, sizeof(s));
	chimera_registry_put(REG_CONFIG, REGVALUE_APPLE2_TYPE, (uint32_t)ModelFromSetting(s));

	SettingStr("video", "colorTv", s, sizeof(s));
	chimera_registry_put(REG_CONFIG, REGVALUE_VIDEO_MODE, (uint32_t)VideoFromSetting(s));
	chimera_registry_put(REG_CONFIG, REGVALUE_VIDEO_STYLE, (uint32_t)(wbx_setting_bool("halfScanlines", 0) ? VS_HALF_SCANLINES : VS_NONE));
	chimera_registry_put(REG_CONFIG, REGVALUE_VIDEO_REFRESH_RATE, (uint32_t)(wbx_setting_long("refreshRate", 60) == 50 ? VR_50HZ : VR_60HZ));

	chimera_registry_put(REG_CONFIG, REGVALUE_ENHANCE_DISK_SPEED, (uint32_t)(wbx_setting_bool("enhanceDiskSpeed", 1) ? 1 : 0));

	/* the joysticks: a PC joystick to upstream is one libwindows reports */
	g_joystickPlugged[0] = wbx_setting_bool("joystick1", 1) != 0;
	g_joystickPlugged[1] = wbx_setting_bool("joystick2", 0) != 0;
	chimera_registry_put(REG_CONFIG, REGVALUE_JOYSTICK0_EMU_TYPE, (uint32_t)(g_joystickPlugged[0] ? J0C_JOYSTICK1 : J0C_DISABLED));
	chimera_registry_put(REG_CONFIG, REGVALUE_JOYSTICK1_EMU_TYPE, (uint32_t)(g_joystickPlugged[1] ? J1C_JOYSTICK2 : J1C_DISABLED));

	/* the slots: upstream's defaults (printer, SSC, Mockingboard in 4, Disk II
	 * in 6) unless the project says otherwise */
	SettingStr("mockingboard", "slot4", s, sizeof(s));
	const bool mb4 = !strcmp(s, "slot4") || !strcmp(s, "slot4and5");
	const bool mb5 = !strcmp(s, "slot5") || !strcmp(s, "slot4and5");
	chimera_registry_put((std::string(REG_CONFIG "\\" REG_CONFIG_SLOT) + "4").c_str(), REGVALUE_CARD_TYPE, (uint32_t)(mb4 ? CT_MockingboardC : CT_Empty));
	chimera_registry_put((std::string(REG_CONFIG "\\" REG_CONFIG_SLOT) + "5").c_str(), REGVALUE_CARD_TYPE, (uint32_t)(mb5 ? CT_MockingboardC : CT_Empty));

	SettingStr("memoryPattern", "ffff0000", s, sizeof(s));
	g_nMemoryClearType = MemoryPatternFromSetting(s);

	chimera_srand((unsigned)wbx_setting_long("randomSeed", 1));
}

/* ---------------------------------------------------------------------------
 * The machine.
 */
static bool g_inited;
static uint32_t g_videoOut[AW_VIDEO_WIDTH * AW_VIDEO_HEIGHT];
static int16_t g_audioOut[AW_AUDIO_MAX_SAMPLES * 2];
static int g_audioCount;
static int g_speakerVolume = 60;
static int g_mockingboardVolume = 60;

/* the speaker resampler: upstream produces one sample every 23 cycles (see
 * Speaker.cpp's SetClksPerSpkrSample), 44369 Hz at the NTSC clock, and the
 * frontend wants 44100; a fixed-ratio linear interpolation, in integers */
static int64_t g_spkrPos;            /* 32.32 fixed point position in the speaker stream */
static int16_t g_spkrLastL, g_spkrLastR;
static std::vector<int16_t> g_spkrRaw;
static int64_t g_audioAccumulator;   /* frames of output owed, in 1/CLK units */

/* frame->mem shadow copy for pokes from outside (the memory domains hand out
 * memmain; a page the CPU is running from lives in mem) */
static void SyncPokesIntoMachine(void)
{
	if (!GetIsMemCacheValid()) return;
	LPBYTE main = MemGetBankPtr(0, false);
	for (UINT page = 0; page < 256; page++)
	{
		LPBYTE alt = MemGetMainPtr((WORD)(page * 256));
		if (alt != main + page * 256) memcpy(alt, main + page * 256, 256);
	}
}

int awdrv_init(char *error, size_t errorLen)
{
	error[0] = 0;
	try
	{
		FillRegistry();
		g_speakerVolume = (int)wbx_setting_long("speakerVolume", 60);
		g_mockingboardVolume = (int)wbx_setting_long("mockingboardVolume", 60);

		ReadSlot("floppy1", g_drives[0].media);
		ReadSlot("floppy2", g_drives[1].media);
		if (g_drives[0].media.empty())
		{
			/* no project slots (a bare image opened in the frontend): the
			 * plain "rom" mount is the disk in drive 1 */
			FILE *f = fopen("rom", "rb");
			if (f)
			{
				fclose(f);
				g_drives[0].media.push_back("rom");
			}
		}
		std::vector<std::string> hard;
		ReadSlot("harddisk", hard);
		for (size_t i = 0; i < hard.size() && i < 2; i++) g_drives[2 + i].media.push_back(hard[i]);

		SetAppleWinVersion(1, 30, 0, 0);
		g_nAppMode = MODE_LOGO;
		g_bFullSpeed = false;
		g_dwSpeed = SPEED_NORMAL;

		KeybReset();
		GetVideo().SetVidHD(false);
		LoadConfiguration(false);
		SetCurrentCLK6502();
		JoyInitialize();
		VideoSwitchVideocardPalette(RGB_GetVideocard(), GetVideo().GetVideoType());

		GetFrame().Initialize(true);
		SpkrInitialize();

		/* the drives: the Disk II card is upstream's default in slot 6; a hard
		 * disk controller goes into slot 7 only when the project brought one */
		{
			LPCSTR floppies[NUM_DRIVES] = { nullptr, nullptr };
			bool connected[NUM_DRIVES] = { true, true };
			bool boot = false;
			if (!g_drives[0].media.empty()) floppies[DRIVE_1] = g_drives[0].media[0].c_str();
			if (!g_drives[1].media.empty()) floppies[DRIVE_2] = g_drives[1].media[0].c_str();
			InsertFloppyDisks(SLOT6, floppies, connected, boot);
			if (floppies[DRIVE_1]) g_drives[0].selected = 0;
			if (floppies[DRIVE_2]) g_drives[1].selected = 0;

			LPCSTR hards[NUM_HARDDISKS] = { nullptr, nullptr };
			if (!g_drives[2].media.empty()) hards[HARDDISK_1] = g_drives[2].media[0].c_str();
			if (!g_drives[3].media.empty()) hards[HARDDISK_2] = g_drives[3].media[0].c_str();
			InsertHardDisks(SLOT7, hards, boot);
			if (hards[HARDDISK_1]) g_drives[2].selected = 0;
			if (hards[HARDDISK_2]) g_drives[3].selected = 0;
		}

		DebugInitialize();
		MemInitialize();
		GetCardMgr().Reset(true);

		/* upstream's BTN_RUN from the logo: a power-on */
		ResetMachineState();
		g_nAppMode = MODE_RUNNING;
	}
	catch (const std::exception &e)
	{
		snprintf(error, errorLen, "AppleWin could not build the machine: %s", e.what());
		return 0;
	}

	for (int d = 0; d < 4; d++) g_drives[d].name = kDriveNames[d];
	g_inited = true;
	return 1;
}

void awdrv_set_button(int index, int down)
{
	if (index >= 0 && index < AW_BTN_COUNT) g_held[index] = down ? 1 : 0;
}

void awdrv_set_axis(int index, int value)
{
	if (index < 0 || index >= AW_AXIS_COUNT) return;
	if (value < 0) value = 0;
	if (value > 255) value = 255;
	g_axis[index] = value;
}

/* lag: the frame read the keyboard or the game port */
extern "C" void chimera_input_was_read(void) { g_inputRead = true; }

static void DrainVideo(void)
{
	Video &video = GetVideo();
	const UINT width = video.GetFrameBufferWidth();
	const UINT bw = video.GetFrameBufferBorderWidth();
	const UINT bh = video.GetFrameBufferBorderHeight();
	const UINT height = video.GetFrameBufferHeight();
	const uint32_t *src = (const uint32_t *)g_frame.framebuffer();
	if (!src) return;
	/* a Windows DIB is bottom-up: row 0 of the picture is the last row of the buffer */
	for (int y = 0; y < AW_VIDEO_HEIGHT; y++)
	{
		const UINT srcRow = height - 1 - bh - (UINT)y;
		const uint32_t *row = src + (size_t)srcRow * width + bw;
		uint32_t *out = g_videoOut + (size_t)y * AW_VIDEO_WIDTH;
		for (int x = 0; x < AW_VIDEO_WIDTH; x++) out[x] = row[x] | 0xFF000000u;
	}
}

/* how many output samples this frame owes at 44100: cycles * 44100 / CLK,
 * carried exactly in an integer accumulator so the average is right */
static int OutputSamplesThisFrame(uint32_t cycles)
{
	const int64_t clk = (int64_t)g_fCurrentCLK6502; /* 1020484 or 1015625 */
	g_audioAccumulator += (int64_t)cycles * SPKR_SAMPLE_RATE;
	const int n = (int)(g_audioAccumulator / clk);
	g_audioAccumulator -= (int64_t)n * clk;
	return n;
}

static void DrainAudio(uint32_t cycles)
{
	const int want = OutputSamplesThisFrame(cycles);
	g_audioCount = want > AW_AUDIO_MAX_SAMPLES ? AW_AUDIO_MAX_SAMPLES : want;
	memset(g_audioOut, 0, sizeof(int16_t) * 2 * (size_t)g_audioCount);

	for (auto it = g_voices.begin(); it != g_voices.end();)
	{
		std::shared_ptr<ChimeraSoundBuffer> voice = it->lock();
		if (!voice) { it = g_voices.erase(it); continue; }
		++it;
		if (!voice->playing()) continue;
		const bool isSpeaker = voice->name() == "Spkr";
		const int gain = isSpeaker ? g_speakerVolume : g_mockingboardVolume;
		const int channels = voice->channels();

		if (isSpeaker)
		{
			/* everything the speaker wrote this frame, at its own 1/23-cycle
			 * rate, resampled to what the frame owes */
			const size_t bytes = voice->pending();
			std::vector<uint8_t> raw(bytes);
			voice->read(raw.data(), bytes);
			const int16_t *in = (const int16_t *)raw.data();
			const size_t frames = bytes / (sizeof(int16_t) * channels);
			for (size_t i = 0; i < frames; i++)
			{
				g_spkrRaw.push_back(in[i * channels]);
				g_spkrRaw.push_back(channels > 1 ? in[i * channels + 1] : in[i * channels]);
			}
			/* step = input rate / output rate = (CLK/23) / 44100, in 32.32 */
			const int64_t clksPerSample = (int64_t)g_fClksPerSpkrSample;
			const int64_t step = ((int64_t)g_fCurrentCLK6502 << 32) / (clksPerSample * SPKR_SAMPLE_RATE);
			size_t consumed = 0;
			for (int o = 0; o < g_audioCount; o++)
			{
				const size_t idx = (size_t)(g_spkrPos >> 32);
				const int64_t frac = g_spkrPos & 0xffffffff;
				int16_t l0 = g_spkrLastL, r0 = g_spkrLastR, l1 = g_spkrLastL, r1 = g_spkrLastR;
				if (idx < g_spkrRaw.size() / 2)
				{
					l0 = g_spkrRaw[idx * 2]; r0 = g_spkrRaw[idx * 2 + 1];
					l1 = idx + 1 < g_spkrRaw.size() / 2 ? g_spkrRaw[idx * 2 + 2] : l0;
					r1 = idx + 1 < g_spkrRaw.size() / 2 ? g_spkrRaw[idx * 2 + 3] : r0;
				}
				const int l = (int)(l0 + (((int64_t)(l1 - l0) * frac) >> 32));
				const int r = (int)(r0 + (((int64_t)(r1 - r0) * frac) >> 32));
				g_audioOut[o * 2] = (int16_t)(g_audioOut[o * 2] + (l * gain) / 100);
				g_audioOut[o * 2 + 1] = (int16_t)(g_audioOut[o * 2 + 1] + (r * gain) / 100);
				g_spkrPos += step;
				consumed = (size_t)(g_spkrPos >> 32);
			}
			if (consumed > g_spkrRaw.size() / 2) consumed = g_spkrRaw.size() / 2;
			if (consumed > 0)
			{
				g_spkrLastL = g_spkrRaw[(consumed - 1) * 2];
				g_spkrLastR = g_spkrRaw[(consumed - 1) * 2 + 1];
				g_spkrRaw.erase(g_spkrRaw.begin(), g_spkrRaw.begin() + (ptrdiff_t)(consumed * 2));
				g_spkrPos -= (int64_t)consumed << 32;
			}
			/* a frontend edit that stalls the stream cannot let it grow without bound */
			if (g_spkrRaw.size() > 8 * 44100) g_spkrRaw.clear();
		}
		else
		{
			/* the Mockingboard runs at 44100 and regulates itself against the
			 * play cursor (MockingboardCardManager's numSamplesError), so it
			 * is read at exactly the frame's rate */
			const size_t bytes = (size_t)g_audioCount * sizeof(int16_t) * channels;
			std::vector<uint8_t> raw(bytes, 0);
			const size_t got = voice->read(raw.data(), bytes);
			const int16_t *in = (const int16_t *)raw.data();
			const int frames = (int)(got / (sizeof(int16_t) * channels));
			for (int i = 0; i < frames; i++)
			{
				const int l = in[i * channels];
				const int r = channels > 1 ? in[i * channels + 1] : l;
				g_audioOut[i * 2] = (int16_t)(g_audioOut[i * 2] + (l * gain) / 100);
				g_audioOut[i * 2 + 1] = (int16_t)(g_audioOut[i * 2 + 1] + (r * gain) / 100);
			}
		}
	}
}

void awdrv_frame(int render)
{
	if (!g_inited) return;
	g_inputRead = false;

	SyncPokesIntoMachine();

	/* disk swaps on the button's edge, before the frame's cycles */
	for (int d = 0; d < 2; d++)
	{
		const int btn = d == 0 ? AW_BTN_NEXT_DISK1 : AW_BTN_NEXT_DISK2;
		if (g_held[btn] && !g_heldLast[btn] && g_drives[d].media.size() > 1)
			InsertFloppy(d, (g_drives[d].selected + 1) % (int)g_drives[d].media.size());
	}

	UpdateKeyboard();

	/* upstream's ContinueExecution(), for exactly one frame of cycles. The
	 * NTSC renderer always runs, drawn or not: its scanner clock is what a
	 * program reads on the floating bus and at the VBL, so a frame without
	 * it would be a different machine (upstream's full-speed mode resyncs
	 * that clock from the cycle count and is a different machine on purpose).
	 * Turbo only skips handing the picture over. */
	const uint32_t frameCycles = NTSC_GetCyclesPerFrame();
	const uint32_t batch = (uint32_t)(g_fCurrentCLK6502 / 1000.0); /* 1 ms */
	uint32_t done = 0;
	while (done < frameCycles)
	{
		const uint32_t want = frameCycles - done < batch ? frameCycles - done : batch;
		const uint32_t executed = CpuExecute(want, true);
		done += executed;
		g_dwCyclesThisFrame += executed;
		GetCardMgr().Update(executed);
		SpkrUpdate(executed);
		if (g_dwCyclesThisFrame >= frameCycles) g_dwCyclesThisFrame -= frameCycles;
	}

	if (render) DrainVideo();
	DrainAudio(done);

	/* the domains hand out memmain: bring it up to date with the CPU's pages */
	MemGetBankPtr(0, true);
}

const uint32_t *awdrv_video(void) { return g_videoOut; }

const int16_t *awdrv_audio(int *sampleCount)
{
	*sampleCount = g_audioCount;
	return g_audioOut;
}

int awdrv_input_was_read(void) { return g_inputRead ? 1 : 0; }

/* the frame rate as a fraction: the 6502 clock over the cycles in a frame.
 * NTSC: (157500000/11 * 65/912) / 17030; PAL: (14250450 * 65/912) / 20280. */
static int64_t Gcd(int64_t a, int64_t b)
{
	while (b) { const int64_t t = a % b; a = b; b = t; }
	return a;
}

void awdrv_vsync(int *numerator, int *denominator)
{
	const uint32_t frameCycles = g_inited ? NTSC_GetCyclesPerFrame() : 17030;
	int64_t num, den;
	if (g_inited && GetVideo().GetVideoRefreshRate() == VR_50HZ)
	{
		num = 14250450LL * 65;
		den = 912LL * frameCycles;
	}
	else
	{
		num = 157500000LL * 65;
		den = 11LL * 912 * frameCycles;
	}
	const int64_t g = Gcd(num, den);
	*numerator = (int)(num / g);
	*denominator = (int)(den / g);
}

/* ---- memory domains ---- */
int awdrv_domain_count(void) { return g_inited ? 2 : 0; }

const char *awdrv_domain_name(int i)
{
	switch (i)
	{
		case 0: return "Main RAM";
		case 1: return "Aux RAM";
		default: return nullptr;
	}
}

uint8_t *awdrv_domain_ptr(int i)
{
	if (!g_inited) return nullptr;
	switch (i)
	{
		case 0: return MemGetBankPtr(0, false);
		case 1: return MemGetBankPtr(1, false);
		default: return nullptr;
	}
}

int64_t awdrv_domain_size(int i)
{
	if (!g_inited) return 0;
	if (i == 0) return 0x10000;
	if (i == 1) return MemGetBankPtr(1, false) ? 0x10000 : 0;
	return 0;
}

/* ---- drives ---- */
int awdrv_drive_count(void)
{
	int n = 0;
	for (int d = 0; d < 4; d++) if (!g_drives[d].media.empty()) n = d + 1;
	return n;
}

const char *awdrv_drive_name(int i) { return i >= 0 && i < 4 ? kDriveNames[i] : nullptr; }

int awdrv_drive_light(int i)
{
	if (!g_inited || i < 0 || i >= 4) return 0;
	Disk_Status_e s1 = DISK_STATUS_OFF, s2 = DISK_STATUS_OFF;
	if (i < 2)
	{
		if (GetCardMgr().QuerySlot(SLOT6) != CT_Disk2) return 0;
		dynamic_cast<Disk2InterfaceCard &>(GetCardMgr().GetRef(SLOT6)).GetLightStatus(&s1, &s2);
		const Disk_Status_e s = i == 0 ? s1 : s2;
		return s == DISK_STATUS_READ ? 1 : s == DISK_STATUS_WRITE ? 2 : 0;
	}
	if (GetCardMgr().QuerySlot(SLOT7) != CT_GenericHDD) return 0;
	dynamic_cast<HarddiskInterfaceCard &>(GetCardMgr().GetRef(SLOT7)).GetLightStatus(&s1);
	return s1 == DISK_STATUS_READ ? 1 : s1 == DISK_STATUS_WRITE ? 2 : 0;
}

int awdrv_drive_media_count(int i) { return i >= 0 && i < 4 ? (int)g_drives[i].media.size() : 0; }

const char *awdrv_drive_media_name(int i, int n)
{
	if (i < 0 || i >= 4 || n < 0 || n >= (int)g_drives[i].media.size()) return nullptr;
	return g_drives[i].media[n].c_str();
}

int awdrv_drive_media_selected(int i) { return i >= 0 && i < 4 ? g_drives[i].selected : -1; }
int awdrv_drive_media_inserted(int i) { return i >= 0 && i < 4 && g_drives[i].selected >= 0 ? 1 : 0; }

uint64_t awdrv_cycles(void) { return g_nCumulativeCycles; }
