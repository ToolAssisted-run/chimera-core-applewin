/* Files, over stdio, with a write overlay.
 *
 * AppleWin reads and writes its disk images through the Win32 file API: a
 * .dsk is read a track at a time and written back a track at a time, a hard
 * disk image a block at a time. In the sandbox those images are the project's
 * files, mounted read-only, and the machine's writes to them are part of the
 * machine - a movie replays the disk as much as the RAM - so they cannot go to
 * the host and must be in a savestate.
 *
 * So a handle is the base file (read-only, never written) plus an overlay of
 * every 64 KiB chunk that has been written, in ordinary guest memory. A read
 * takes the overlay where there is one and the base elsewhere; a write
 * materialises the chunk from the base first. It is the chimera-core-dosbox-x
 * hard-disk design at the file API instead of the block device, which is what
 * makes it cover floppies, WOZ images and hard disks alike without touching
 * upstream. A file CREATED by the machine (a new blank disk) is an overlay
 * with no base.
 */
#include "fileapi.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>
#include <algorithm>

namespace
{
    constexpr uint64_t CHUNK = 64 * 1024;

    struct FILE_HANDLE : public CHANDLE
    {
        FILE_HANDLE(FILE *base, uint64_t size)
            : f(base)
            , size(size)
        {
        }
        ~FILE_HANDLE() override
        {
            if (f)
            {
                fclose(f);
            }
        }

        FILE *f = nullptr;
        uint64_t size = 0;
        uint64_t pos = 0;
        // chunk index -> its bytes; a map rather than a hash so a savestate's
        // heap layout does not depend on a seed
        std::map<uint64_t, std::vector<uint8_t>> overlay;

        bool readBase(uint64_t offset, uint8_t *dst, size_t len)
        {
            if (f == nullptr)
            {
                memset(dst, 0, len);
                return true;
            }
            if (fseek(f, (long)offset, SEEK_SET) != 0)
                return false;
            const size_t got = fread(dst, 1, len, f);
            if (got < len)
                memset(dst + got, 0, len - got);
            return true;
        }

        std::vector<uint8_t> *materialise(uint64_t chunk)
        {
            auto it = overlay.find(chunk);
            if (it != overlay.end())
                return &it->second;
            std::vector<uint8_t> block(CHUNK, 0);
            const uint64_t start = chunk * CHUNK;
            if (start < size)
            {
                const size_t span = (size_t)std::min<uint64_t>(CHUNK, size - start);
                if (!readBase(start, block.data(), span))
                    return nullptr;
            }
            auto ins = overlay.emplace(chunk, std::move(block));
            return &ins.first->second;
        }

        size_t read(void *dst, size_t len)
        {
            if (pos >= size)
                return 0;
            len = (size_t)std::min<uint64_t>(len, size - pos);
            uint8_t *out = (uint8_t *)dst;
            size_t done = 0;
            while (done < len)
            {
                const uint64_t chunk = pos / CHUNK;
                const uint64_t within = pos % CHUNK;
                const size_t take = (size_t)std::min<uint64_t>(len - done, CHUNK - within);
                auto it = overlay.find(chunk);
                if (it != overlay.end())
                    memcpy(out + done, it->second.data() + within, take);
                else if (!readBase(pos, out + done, take))
                    break;
                pos += take;
                done += take;
            }
            return done;
        }

        size_t write(const void *src, size_t len)
        {
            const uint8_t *in = (const uint8_t *)src;
            size_t done = 0;
            while (done < len)
            {
                const uint64_t chunk = pos / CHUNK;
                const uint64_t within = pos % CHUNK;
                const size_t take = (size_t)std::min<uint64_t>(len - done, CHUNK - within);
                std::vector<uint8_t> *block = materialise(chunk);
                if (block == nullptr)
                    break;
                memcpy(block->data() + within, in + done, take);
                pos += take;
                done += take;
                if (pos > size)
                    size = pos;
            }
            return done;
        }
    };

    FILE_HANDLE &handle(HANDLE h)
    {
        return dynamic_cast<FILE_HANDLE &>(*h);
    }
} // namespace

DWORD SetFilePointer(HANDLE hFile, LONG lDistanceToMove, PLONG lpDistanceToMoveHigh, DWORD dwMoveMethod)
{
    FILE_HANDLE &fh = handle(hFile);
    int64_t base = 0;
    switch (dwMoveMethod)
    {
    case FILE_BEGIN: base = 0; break;
    case FILE_CURRENT: base = (int64_t)fh.pos; break;
    case FILE_END: base = (int64_t)fh.size; break;
    default: return INVALID_SET_FILE_POINTER;
    }
    int64_t distance = lDistanceToMove;
    if (lpDistanceToMoveHigh)
        distance |= ((int64_t)*lpDistanceToMoveHigh) << 32;
    const int64_t target = base + distance;
    if (target < 0)
        return INVALID_SET_FILE_POINTER;
    fh.pos = (uint64_t)target;
    if (lpDistanceToMoveHigh)
        *lpDistanceToMoveHigh = (LONG)(fh.pos >> 32);
    return (DWORD)fh.pos;
}

BOOL ReadFile(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead, LPOVERLAPPED)
{
    FILE_HANDLE &fh = handle(hFile);
    const size_t got = fh.read(lpBuffer, nNumberOfBytesToRead);
    if (lpNumberOfBytesRead)
        *lpNumberOfBytesRead = (DWORD)got;
    return TRUE;
}

BOOL WriteFile(HANDLE hFile, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite, LPDWORD lpNumberOfBytesWritten, LPOVERLAPPED)
{
    FILE_HANDLE &fh = handle(hFile);
    const size_t done = fh.write(lpBuffer, nNumberOfBytesToWrite);
    if (lpNumberOfBytesWritten)
        *lpNumberOfBytesWritten = (DWORD)done;
    return done == nNumberOfBytesToWrite;
}

BOOL DeleteFile(LPCTSTR)
{
    /* nothing in the sandbox is the machine's to delete */
    return FALSE;
}

DWORD GetFileSize(HANDLE hFile, LPDWORD lpFileSizeHigh)
{
    FILE_HANDLE &fh = handle(hFile);
    if (lpFileSizeHigh)
        *lpFileSizeHigh = (DWORD)(fh.size >> 32);
    return (DWORD)fh.size;
}

HANDLE CreateFile(LPCTSTR lpFileName, DWORD dwDesiredAccess, DWORD, LPSECURITY_ATTRIBUTES, DWORD dwCreationDisposition, DWORD, HANDLE)
{
    (void)dwDesiredAccess;
    if (dwCreationDisposition == CREATE_NEW || dwCreationDisposition == CREATE_ALWAYS)
    {
        /* a file the machine makes lives in the overlay alone */
        return new FILE_HANDLE(nullptr, 0);
    }
    if (dwCreationDisposition != OPEN_EXISTING)
        return INVALID_HANDLE_VALUE;

    /* always read-only underneath, whatever access was asked: the writes go
     * to the overlay, and the sandbox would refuse a writable open anyway */
    FILE *f = fopen(lpFileName, "rb");
    if (!f)
        return INVALID_HANDLE_VALUE;
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    return new FILE_HANDLE(f, size < 0 ? 0 : (uint64_t)size);
}

DWORD GetFileAttributes(const char *filename)
{
    /* A mounted image is read-only on the host and writable to the machine
     * (the overlay), so it is never reported READONLY - that would make
     * AppleWin write-protect the disk. */
    FILE *f = fopen(filename, "rb");
    if (!f)
        return INVALID_FILE_ATTRIBUTES;
    fclose(f);
    return FILE_ATTRIBUTE_NORMAL;
}

DWORD GetFullPathName(const char *filename, DWORD length, char *buffer, char **filePart)
{
    /* one flat directory: a name is its own full path */
    strncpy(buffer, filename, length);
    if (length)
        buffer[length - 1] = 0;
    if (filePart)
        *filePart = buffer;
    return (DWORD)strlen(buffer);
}

DWORD GetCurrentDirectory(DWORD length, char *buffer)
{
    if (length)
        buffer[0] = 0;
    return 0;
}

BOOL GetOpenFileName(LPOPENFILENAME)
{
    return FALSE;
}

BOOL GetSaveFileName(LPOPENFILENAME)
{
    return FALSE;
}
