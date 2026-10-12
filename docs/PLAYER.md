# Player translation

Mario's normal gameplay routines are translated to C, including jump,
run and fire control at 03:498B. The internal 4966 and 4975 paths belong
to that entry; they are not separate callable routines.

`src/game/player_input.c` handles the held and newly pressed buttons:

* B selects walking or running speed while Mario is on the ground.
* A starts a jump only on a new press while grounded. Releasing A while
  rising skips to jump-arc step 15 and stores the previous step minus one
  in C209, preserving the original's byte wraparound.
* A crouching jump keeps the crouching pose and existing arc step. A
  running jump also keeps the existing arc step.
* A newly pressed Down sets momentum to 32; the following B test still
  runs, so simultaneous presses retain the original behaviour.
* Normal gameplay has one superball slot and requires the flower.
  Shooter stages have three projectile slots. A newly pressed A or B
  fires immediately; holding A decrements the cooldown and fires when
  it reaches zero. A successful shot resets the cooldown to 12.

Projectile OAM writes and sound requests use synchronized shared-memory
accesses. The plane and submarine keep their original projectile tiles
and facing-dependent spawn positions.

`src/game/level_play.c` translates GameState_00 (00:0627). It preserves
the original order of level loading, collisions, jump arcs, input,
animation, projectiles, enemies, timer and rewards. Its five object-arc
calls are expressed as a C loop; the original has no loop branch there,
so the C loop emits no extra `taken()` events.

The active ROM bank (FFFD) is shared with the timer interrupt. Both the
RAM byte and the MBC write are synchronized: changing FFFD before the
shadow has finished a banked callee can make a timer interrupt restore
the next bank too early. `banks.h` and Mario's drawing code use shared
accesses for this byte.

The function-test registry includes MarioJumpAndFire and
UpdateNormalGameplay. The latter combines its callees' existing
validity predicates for level pointers, worlds, sprites, enemy IDs,
demo lists and floaty slots. MarioJumpAndFire needs no validity filter.
Verification results and remaining movie coverage gaps are recorded in
`docs/PLAN.md`.
