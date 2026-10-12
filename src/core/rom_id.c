/* Identification of the user's ROM image. */
#include "core.h"
#include "../util/sha1.h"
#include <string.h>

RomVersion core_identify_rom(const uint8_t *rom, size_t size)
{
    char hex[41];
    sha1_hex(rom, size, hex);
    if (!strcmp(hex, "418203621b887caa090215d97e3f509b79affd3e"))
        return ROM_SML_V11;
    if (!strcmp(hex, "3a4ddb39b234a67ffb361ee7abc3d23e0a8b1c89"))
        return ROM_SML_V10;
    return ROM_UNKNOWN;
}

const char *core_rom_description(RomVersion v)
{
    switch (v) {
    case ROM_SML_V11: return "Super Mario Land (World) (Rev A / v1.1)";
    case ROM_SML_V10: return "Super Mario Land (World) (v1.0)";
    default:          return "unknown ROM";
    }
}
