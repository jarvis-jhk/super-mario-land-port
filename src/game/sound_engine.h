/*
 * The sound engine (ROM bank 3, 6600-7FFF). It runs inside the timer
 * interrupt; the game talks to it only through the mailboxes in sound.h.
 * See docs/SOUND_ENGINE.md. Each comment names the original routine.
 */
#ifndef SML_SOUND_ENGINE_H
#define SML_SOUND_ENGINE_H

#include <stdbool.h>
#include <stdint.h>

/* ---- sound_engine.c: entry points, channel setup, panning ---- */
void sound_timer_tick(void);                    /* 03:7FF0 Call_7FF0 (-> 03:6662 _Call_7FF0) */
void init_sound(void);                          /* 03:7FF3 InitSound (-> 03:6A33), main program */
void mute_channels(void);                       /* 03:6A54 InitSound.muteChannels */
void setup_channel_square1(uint16_t data);      /* 03:69E5 SetupChannel.square1 */
void setup_channel_square2(uint16_t data);      /* 03:69EC SetupChannel.square2 */
void setup_channel_wave(uint16_t data);         /* 03:69F3 SetupChannel.wave */
void setup_channel_noise(uint16_t data);        /* 03:69FA SetupChannel.noise */
void setup_wave_pattern(uint16_t data);         /* 03:6A26 SetupWavePattern */
uint16_t lookup_new_sound_pointer(uint8_t number, uint16_t table); /* 03:6A07 LookupSoundPointer */
uint16_t lookup_sound_pointer(uint8_t number, uint16_t table);     /* 03:6A0A LookupSoundPointer.lookup */
uint8_t update_sound_progress(uint16_t position_addr); /* 03:6A19 updateSoundProgress */
void pan_explosion(uint16_t timer_addr);        /* 03:6AF6 PanExplosion */
void pan_stereo(void);                          /* 03:6B09 PanStereo */

/* ---- sound_sfx.c: sound effects ---- */
void play_square_sfx(void);                     /* 03:6A6A PlaySquareSFX */
void play_noise_sfx(void);                      /* 03:6A8E PlayNoiseSFX */
void play_wave_sfx(void);                       /* 03:66F6 PlayWaveSFX */
void start_sfx(uint16_t position_addr, uint8_t duration, uint16_t channel_data); /* 03:69C6 Jmp_69C6 */
bool sfx_outranked_by_stomp(void);              /* 03:6796 Call_6791.jmp_6796 (true = Z flag) */
bool sfx_outranked_by_coin(void);               /* 03:679C Call_6791.jmp_679C */
bool sfx_outranked_by_grow(void);               /* 03:6791 Call_6791 */
bool sfx_outranked_by_explosion(void);          /* 03:695F Call_695F */
void start_timer_tick_sfx(void);                /* 03:676E StartTimerTickSFX */
void start_gira_sfx(void);                      /* 03:677B StartGiraSFX */
void start_jump_sfx(void);                      /* 03:67AF StartJumpSFX */
void continue_jump_sfx(void);                   /* 03:67C4 ContinueJumpSFX */
void start_superball_sfx(void);                 /* 03:67E4 StartSuperballSFX */
void continue_square_sfx(void);                 /* 03:67F0 ContinueSquareSFX */
void stop_square_sfx(void);                     /* 03:67F5 ContinueSquareSFX.stop */
void start_coin_sfx(void);                      /* 03:6813 StartCoinSFX */
void continue_coin_sfx(void);                   /* 03:681D ContinueCoinSFX */
void start_stomp_sfx(void);                     /* 03:683D StartStompSFX */
void continue_stomp_sfx(void);                  /* 03:6849 ContinueStompSFX */
void start_flower_sfx(void);                    /* 03:6868 StartFlowerSFX */
void start_grow_sfx(void);                      /* 03:687A StartGrowSFX */
void continue_sweep_square_sfx(void);           /* 03:6887 ContinueSweepSquareSFX */
void start_bump_sfx(void);                      /* 03:68A5 StartBumpSFX */
void start_injury_sfx(void);                    /* 03:68C4 StartInjurySFX */
void continue_injury_sfx(void);                 /* 03:68D2 ContinueInjurySFX */
void start_one_up_sfx(void);                    /* 03:68F0 StartOneUpSFX */
void continue_one_up_sfx(void);                 /* 03:6916 ContinueOneUpSFX */
void start_explosion_sfx(void);                 /* 03:6957 StartExplosionSFX */
void start_death_cry_sfx(void);                 /* 03:6970 StartDeathCrySFX */
void continue_death_cry_sfx(void);              /* 03:697C ContinueDeathCrySFX */
void start_fire_breath_sfx(void);               /* 03:6997 StartFireBreathSFX */
void start_brick_shatter_sfx(void);             /* 03:69A3 StartBrickShatterSFX */
void continue_noise_sfx(void);                  /* 03:69AF ContinueNoiseSFX */
void stop_noise_sfx(void);                      /* 03:69B4 ContinueNoiseSFX.stop */

/* ---- sound_music.c: songs ---- */
void start_music(void);                         /* 03:6AB5 StartMusic */
void setup_song(uint16_t song);                 /* 03:6B8C Call_6B8C */
void copy_pointer(uint16_t src, uint16_t dst);  /* 03:6B86 CopyPointer */
uint8_t copy_pointer_indirect(uint16_t src, uint16_t dst); /* 03:6B7B CopyPointerIndirect */
void increment_pointer(uint16_t addr);          /* 03:6C30 IncrementPointer */
void increment_pointer_twice(uint16_t addr);    /* 03:6C3C IncrementPointerTwice */
uint8_t load_from_pointer(uint16_t addr);       /* 03:6C45 LoadFromHLindirect */
void play_music(void);                          /* 03:6CBE PlayMusic */
uint8_t vibrato_offset_byte(uint8_t step);      /* 03:6DD3 PlayMusic.call_6DD3 */
void apply_vibrato(uint16_t block, int channel); /* 03:6DDC PlayMusic.jmp_6DDC */

#endif
