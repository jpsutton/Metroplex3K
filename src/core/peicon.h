// Icons from a Windows executable's resources (the game's own SC3U.exe): the load dialogs show the
// document icons the program registers for its file kinds. Reads RT_GROUP_ICON / RT_ICON and decodes
// the DIB (1/4/8/24/32 bpp with AND mask) to RGBA.
#pragma once
#include <cstdint>
#include "core/bytes.h"
#include "core/image.h"

namespace m3k {

// Icon `groupId` at `size` x `size`, deepest colour variant. False if the file is not a PE image, the group or
// size is missing, or the bitmap is unsupported.
bool loadPeIcon(const Bytes& exe, uint32_t groupId, int size, Image& out);

// DIB of one RT_ICON resource (BITMAPINFOHEADER, height doubled for the AND mask) to RGBA.
bool decodeIconDib(const uint8_t* p, size_t n, Image& out);

}  // namespace m3k
