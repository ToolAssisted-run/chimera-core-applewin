/* test-fileapi.cpp - the write overlay behind AppleWin's file API.
 *
 * A disk image is a read-only file; the machine writes tracks into it. This
 * proves, natively, that a handle reads back exactly what was written where
 * it was written, the base everywhere else, across chunk boundaries and past
 * the end of the base, and that a file the machine creates works too.
 *
 *   c++ -std=gnu++17 -I waterbox/libwindows waterbox/tests/test-fileapi.cpp \
 *       waterbox/libwindows/fileapi.cpp waterbox/libwindows/winhandles.cpp -o test-fileapi
 */
#include "windows.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

extern "C" uint32_t chimera_emulated_ms(void) { return 0; }
extern "C" int64_t chimera_emulated_epoch_seconds(void) { return 0; }

static int failures;
#define CHECK(cond) do { if (!(cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

int main(int argc, char **argv)
{
	const char *path = argc > 1 ? argv[1] : "test-fileapi.base";
	/* a base of 200 KiB with a known pattern, spanning four 64 KiB chunks */
	const size_t baseSize = 200 * 1024;
	{
		std::vector<uint8_t> base(baseSize);
		for (size_t i = 0; i < baseSize; i++) base[i] = (uint8_t)(i * 7 + (i >> 8));
		FILE *f = fopen(path, "wb");
		fwrite(base.data(), 1, baseSize, f);
		fclose(f);
	}

	HANDLE h = CreateFile(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	CHECK(h != INVALID_HANDLE_VALUE);
	CHECK(GetFileSize(h, NULL) == baseSize);

	/* read the base */
	uint8_t buf[1024];
	DWORD got = 0;
	CHECK(SetFilePointer(h, 65536 - 512, NULL, FILE_BEGIN) == 65536 - 512);
	CHECK(ReadFile(h, buf, 1024, &got, NULL) && got == 1024);
	for (size_t i = 0; i < 1024; i++) CHECK(buf[i] == (uint8_t)((65536 - 512 + i) * 7 + ((65536 - 512 + i) >> 8)));

	/* a track written across a chunk boundary reads back, and its neighbours are the base */
	uint8_t track[4096];
	for (size_t i = 0; i < sizeof track; i++) track[i] = (uint8_t)(0xA5 ^ i);
	DWORD wrote = 0;
	CHECK(SetFilePointer(h, 65536 - 2048, NULL, FILE_BEGIN) == 65536 - 2048);
	CHECK(WriteFile(h, track, sizeof track, &wrote, NULL) && wrote == sizeof track);
	CHECK(SetFilePointer(h, 65536 - 2048 - 16, NULL, FILE_BEGIN) == 65536 - 2048 - 16);
	uint8_t around[4096 + 32];
	CHECK(ReadFile(h, around, sizeof around, &got, NULL) && got == sizeof around);
	for (size_t i = 0; i < 16; i++) CHECK(around[i] == (uint8_t)((65536 - 2048 - 16 + i) * 7 + ((65536 - 2048 - 16 + i) >> 8)));
	for (size_t i = 0; i < 4096; i++) CHECK(around[16 + i] == track[i]);
	for (size_t i = 0; i < 16; i++) CHECK(around[16 + 4096 + i] == (uint8_t)((65536 + 2048 + i) * 7 + ((65536 + 2048 + i) >> 8)));

	/* the base file itself is untouched */
	{
		FILE *f = fopen(path, "rb");
		fseek(f, 65536 - 2048, SEEK_SET);
		uint8_t b[16];
		fread(b, 1, 16, f);
		fclose(f);
		for (size_t i = 0; i < 16; i++) CHECK(b[i] == (uint8_t)((65536 - 2048 + i) * 7 + ((65536 - 2048 + i) >> 8)));
	}

	/* writing past the end grows the file; the gap reads as zeros */
	CHECK(SetFilePointer(h, 0, NULL, FILE_END) == baseSize);
	CHECK(SetFilePointer(h, 100, NULL, FILE_CURRENT) == baseSize + 100);
	CHECK(WriteFile(h, track, 10, &wrote, NULL) && wrote == 10);
	CHECK(GetFileSize(h, NULL) == baseSize + 110);
	CHECK(SetFilePointer(h, (LONG)baseSize, NULL, FILE_BEGIN) == baseSize);
	CHECK(ReadFile(h, buf, 110, &got, NULL) && got == 110);
	for (size_t i = 0; i < 100; i++) CHECK(buf[i] == 0);
	for (size_t i = 0; i < 10; i++) CHECK(buf[100 + i] == track[i]);
	/* a read at the end is a short read, not an error */
	CHECK(ReadFile(h, buf, 16, &got, NULL) && got == 0);
	CloseHandle(h);

	/* a file the machine creates lives in the overlay alone */
	HANDLE n = CreateFile("does-not-exist-on-disk", GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
	CHECK(n != INVALID_HANDLE_VALUE);
	CHECK(GetFileSize(n, NULL) == 0);
	CHECK(WriteFile(n, track, 300, &wrote, NULL) && wrote == 300);
	CHECK(SetFilePointer(n, 0, NULL, FILE_BEGIN) == 0);
	CHECK(ReadFile(n, buf, 300, &got, NULL) && got == 300);
	CHECK(memcmp(buf, track, 300) == 0);
	CloseHandle(n);
	CHECK(fopen("does-not-exist-on-disk", "rb") == NULL);

	/* a mounted image is never reported read-only */
	CHECK(GetFileAttributes(path) == FILE_ATTRIBUTE_NORMAL);
	CHECK(GetFileAttributes("does-not-exist-on-disk") == INVALID_FILE_ATTRIBUTES);

	remove(path);
	if (failures) { fprintf(stderr, "%d checks failed\n", failures); return 1; }
	printf("the file overlay reads back what the machine wrote and leaves the image alone\n");
	return 0;
}
