/* Placeholder for no-ROM CI builds (make ACCESS_TABLE=src/core/access_table_stub.c).
 * The real table is generated locally from the player's ROM
 * (tools/regen_access_table.py) and is never published. A negative size
 * marks this build: it compiles and links every target, but cannot run
 * the game (core_has_access_table() is false). */
#include "shadow.h"

const AccessEntry shadow_access_table[1] = { { 0, 0, 0 } };
const int shadow_access_table_size = -1;
