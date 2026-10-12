# The sound engine

Super Mario Land's sound engine lives in ROM bank 3 at `$6600-$7FFF` (code
at `$6662-$6DFF`, data around it). Its C translation is in
`src/game/sound_engine.c` (tick, init, panning, channel helpers),
`sound_sfx.c` (sound effects) and `sound_music.c` (music player);
internals in `sound_private.h`, requests and sound numbers in `sound.h`.

## Timing: the timer interrupt

The engine is not called by the game loop. The timer interrupt (vector
`00:0050`) calls `03:7FF0` (`sound_timer_tick`), which jumps to
`03:6662`. The timer is set up for 64 Hz at normal tempo (the tempo of everything
the engine does follows the timer's rate). One call is one
"tick"; durations below are in ticks.

The interrupt handler re-enables interrupts (`ei`) right at its start, so
vblank and the LCD STAT interrupt can run in the middle of an engine tick.
That is why the mailboxes are in `shared.h`: the main program must use
`shared_read/shared_write` for them.
On entry and exit the engine writes `3` and `7` to `$00FF` (it thinks it is
enabling cartridge RAM; the cartridge has none, so it is harmless). The
translation does these writes with `bus_write`.

One tick:

1. If the game asked for a pause (`$FFDF` = 1): mute everything, forget the
   effects, play the first pause beep and start the pause timer (`$FFDE` =
   `$30`). While the timer is nonzero the engine only counts it down and
   plays beeps at `$28` (C7), `$20` (C6), `$18` (C7); at `$10` it is held
   until the unpause request (`$FFDF` = 2) clears it. The requests of the
   game are discarded meanwhile.
2. Otherwise, if `$FFD3` is nonzero (set together with a 1-UP request), it
   is cleared and the 1-UP jingle is put into the square mailbox, replacing
   whatever was asked.
3. `PlaySquareSFX`, `PlayNoiseSFX`, `PlayWaveSFX`, `StartMusic`,
   `PlayMusic`, `PanStereo`, in this order.
4. All four request mailboxes and `$FFDF` are cleared: a request is seen by
   exactly one tick.

## Mailboxes and status bytes

| Address | Meaning |
|---------|---------|
| `$DFE0` | request: square effect 1-11 (`SFX_*`) |
| `$DFE1` | square effect playing (0 = none); also what the effects' priority rules look at |
| `$DFE8` | request: song 1-19 (`MUS_*`) |
| `$DFE9` | song playing (0 = none). The game waits on it after jingles. Becomes 0 at the end of a song |
| `$DFF0` | request: wave effect, 1 = boss cry |
| `$DFF1` | wave effect playing |
| `$DFF8` | request: noise effect 1-4 (`NOISE_*`) |
| `$DFF9` | noise effect playing |
| `$FFD3` | 1-UP re-post flag (see above) |
| `$FFDE`, `$FFDF` | pause timer / pause request |

Requests are valid only in range (square 1-11, noise 1-4, song 1-19, wave
exactly 1); anything else is ignored. The square and noise requests also
set the lock bit (below) even when the effect then refuses to start.

### Effect blocks (`$DFE0`, `$DFF0`, `$DFF8`)

Each effect channel has a block: `+0` request, `+1` number playing, `+2`
position (ticks in the current step), `+3` duration (ticks per step), `+4`
and `+5` per-effect state (counter, frequency...), square also `+6` (sweep).
`start_sfx` (`03:69C6`) fills the block and loads five (square/wave) or
four (noise) register values from a table in ROM. The "continue" routines
then run on each following tick; `update_sound_progress` increments the
position and returns 0 when it reaches the duration (position then restarts
at 0), which is when the effect does its next step or stops. A stopping
effect writes envelope `$08`/trigger `$80` (silence) to its channel and
clears its lock bit.

Square effect priority: a new effect does not start while a more important
one is playing. From low to high: jump/superball < stomp < coin/bump/gira <
mushroom, injury, 1-UP, flower (checked in `sfx_outranked_by_*`). The injury
effect is only blocked by the 1-UP, the flower and mushroom never are.
Death cry and brick shatter are blocked by a running explosion.

### Lock bytes (`$DF1F`, `$DF2F`, `$DF3F`, `$DF4F`)

Bit 7 of the last byte of each music channel block means "a sound effect
owns this channel": the music keeps running but does not write the
channel's registers. Effects set it when they start (square 1, noise, wave
for the boss cry) and clear it when they stop. Bit 0 is cleared by the
music at every note start (unused otherwise).

## Wave channel: the boss cry and its random numbers

The only wave effect is the boss cry (request 1): the engine takes the wave
channel, loads the cry's wave pattern into wave RAM (`$FF30-$FF3F`) and
writes NR30-NR34 from ROM. A pitch counter (`$DFF5`) starts at `$D0 +
(DIV & $1F)`; on each following tick the counter rises by 2 for 13 ticks and
then falls by 3 until tick 30, and the frequency low byte (NR33) written
is the counter with its lowest three bits replaced by `DIV & 7` — a
"random" wobble. Because the engine runs inside the timer interrupt, DIV
has nearly the same value at every tick (it is the same position in the
timer period), so the "random" values are practically constants; the
translation reads `rDIV` with `io_read`, exactly like the original, so the
values match whatever the hardware model gives. When done, the music's own
wave pattern (pointer in `$DF36/$DF37`) is copied back.

## Music

### Song data

`$663C` holds 19 pointers to song headers (`$6F98-$6FE5`, `$78DC-$793F`,
`$7D6A`), 11 bytes each: one unused byte, a pointer to the song's note
length table, and one pointer for each of the four channels (square 1,
square 2, wave, noise; a zero pointer means the channel is unused). A
channel pointer leads to a list of 16-bit pattern pointers, ending with
`$FFFF` followed by the address to restart from (songs loop), or `$0000`
(the song ends: all channels are silenced, `$DFE9` becomes 0).

A pattern is a byte stream:

| Byte | Meaning |
|------|---------|
| `00` | end of pattern: next pointer of the list |
| `9D a b c` | instrument: `a`, `b`, `c` go to `CH_PARAM_LO`, `CH_PARAM_HI`, `CH_DUTY`. Square: NRx2 envelope, 0, NRx1 duty. Wave: pointer `b:a` to a 16-byte wave pattern (copied to wave RAM at once) and the NR32 output level |
| `A0-AF` | set the note length to entry `n` of the length table (ticks); the note follows |
| `01` | rest |
| other | note: the byte is an offset into the pitch table `$6E74` (words, so notes are even numbers). Noise channel: offset of a 5-byte drum in `$6F06` |

### Channel state

Four 16-byte blocks at `$DF10` (square 1), `$DF20`, `$DF30` (wave), `$DF40`
(noise); the header at `$DF00` holds the song's byte 0 and, at `$DF01`, its
note length table pointer. Block offsets: `+0/1` pattern list pointer, `+2`
ticks left, `+3` note length, `+4/5` stream pointer, `+6..8` instrument,
`+9/A` frequency, `+B` rest flag, `+E` vibrato step, `+F` lock (above).

`PlayMusic` loops over the channels. A used channel (high byte of `+0/1`
nonzero) counts `+2` down; at 0 it reads commands until a note comes, writes
the channel's registers (NRx1, NRx2, NRx3, NRx4 with the trigger bit; the
wave DAC is cycled through NR30 first) unless locked, and sets `+2 = +3`.
While a note is held, tone channels get a vibrato (`apply_vibrato`): every
tick the frequency registers are rewritten with the note frequency plus a
signed nibble from the table at `$6E3D`, indexed by the tick count since the
note started (two steps per table byte, high nibble first). The wave channel
also switches its output level to 100% when 6 ticks remain, if its level
byte has bit 7 set. After all channels the three vibrato step counters
advance.

Quirks preserved from the original: when an instrument's second byte is
nonzero on a square/noise channel, the envelope byte is not loaded and the
CPU's leftover E register (low address byte of the frequency entry, `$4B`
for noise) is written to NRx2 instead; the noise channel reads
its rest flag `$DF4B`, which the noise path never writes.

### Starting a song and panning

`StartMusic` accepts requests 1-19, mutes the channels (`03:6B8C`), loads
the header, sets each channel's tick count to 1 (so every channel starts at
the next tick) and its pattern pointer, then copies the song's four
stereo bytes from `$6B2F`: mode (1 mono, 2 stereo), swap interval, and two
NR51 masks. The first mask is written to NR51 immediately.

Each tick `PanStereo` (only when a song is playing and the song is stereo)
counts up `$FFD5`; when it reaches the interval it resets, toggles bit 0 of
`$FFD7` and writes mask 1 or mask 2 to NR51 (which channels go to which
speaker), so stereo songs move sounds between the speakers. While the
explosion noise plays, `PanExplosion` instead alternates the noise between
left (`$F7`) and right (`$7F`) every two ticks (and the normal swap may
overwrite it on the same tick). Mono songs return before doing either.

## Using the engine from the game

* Request: write the number to the mailbox (`shared_write` from main
  program code); it is picked up at the next tick.
* `init_sound()` (`03:7FF3`) resets the state (called at start-up and when
  levels and screens start); it clears the playing bytes and the lock bytes
  and routes all channels to both speakers.
