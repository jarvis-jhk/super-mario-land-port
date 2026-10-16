/*
 * The Game Boy hardware, minus the CPU.
 *
 * Super Mario Land talks to a handful of chips: work RAM, video RAM, sprite
 * memory (OAM), the LCD controller, a timer, the sound chip (APU), the joypad
 * and the MBC1 bank switcher on the cartridge. This module models all of them
 * cycle by cycle. Whoever "is the CPU" drives it:
 *
 *   - the reference emulator (verify/emu) executes the original machine code
 *     and calls machine_tick()/machine_read()/machine_write() per M-cycle;
 *   - the C port executes C code and lets the timing shadow (shadow.h) call
 *     the same functions at the exact cycles the original code would have.
 *
 * Because both share this code, identical CPU timing gives identical pixels,
 * identical sound and identical register reads (e.g. the DIV-based random
 * numbers), which is what makes bit-exact verification possible.
 */
#ifndef SML_MACHINE_H
#define SML_MACHINE_H

#include <stdint.h>
#include <stddef.h>
#include "lcd.h"
#include "apu.h"

/* Joypad bits (same layout as the game's hJoyHeld variable) */
enum {
    BTN_A = 0x01, BTN_B = 0x02, BTN_SELECT = 0x04, BTN_START = 0x08,
    BTN_RIGHT = 0x10, BTN_LEFT = 0x20, BTN_UP = 0x40, BTN_DOWN = 0x80,
};

/* Interrupt flag bits (IF / IE) */
enum { INT_VBLANK = 1, INT_STAT = 2, INT_TIMER = 4, INT_SERIAL = 8, INT_JOYPAD = 16 };

/* IO register addresses used by the game */
enum {
    rP1 = 0xFF00, rDIV = 0xFF04, rTIMA = 0xFF05, rTMA = 0xFF06, rTAC = 0xFF07,
    rIF = 0xFF0F,
    rNR10 = 0xFF10, rNR11, rNR12, rNR13, rNR14,
    rNR21 = 0xFF16, rNR22, rNR23, rNR24,
    rNR30 = 0xFF1A, rNR31, rNR32, rNR33, rNR34,
    rNR41 = 0xFF20, rNR42, rNR43, rNR44,
    rNR50 = 0xFF24, rNR51, rNR52,
    rWAVE = 0xFF30,
    rLCDC = 0xFF40, rSTAT, rSCY, rSCX, rLY, rLYC, rDMA, rBGP, rOBP0, rOBP1, rWY, rWX,
    rIE = 0xFFFF,
};

/* Which console runs the game. Super Mario Land is a DMG game; on a Game Boy
   Color or Game Boy Advance it runs in DMG compatibility mode with slightly
   different timing. The console-verified TAS resyncs were made on a GBA. */
typedef enum {
    MODEL_DMG,   /* original Game Boy (default) */
    MODEL_CGB,   /* Game Boy Color, DMG compatibility mode */
    MODEL_AGB,   /* Game Boy Advance, DMG compatibility mode */
} MachineModel;

typedef struct Machine Machine;

/* Instrumentation callbacks (all optional). */
typedef struct {
    void *user;
    void (*on_io_write)(Machine *m, uint16_t addr, uint8_t val, void *user);   /* FF00-FFFF */
    void (*on_io_read)(Machine *m, uint16_t addr, uint8_t val, void *user);    /* FF00-FF7F */
    void (*on_mem_write)(Machine *m, uint16_t addr, uint8_t val, void *user);  /* 8000-DFFF, FE00-FE9F */
    void (*on_mem_read)(Machine *m, uint16_t addr, uint8_t val, void *user);   /* 8000-DFFF, FE00-FE9F;
                                                                                  val = value returned */
    void (*on_frame)(Machine *m, void *user);
} MachineHooks;

/* Timing state of the LCD controller's pixel pipeline. It does not produce
   pixels (lcd.c does that, one line at a time); it only knows how many
   cycles each line's drawing phase takes, which decides when mode 0 starts.
   The whole struct is copied to predict future events. */
typedef struct {
    int state;                 /* PS_* : which step runs next */
    int64_t at;                /* T-cycle at which that step runs */
    int xpos;                  /* pixel being drawn (0..167 are plotted; 8 off-screen) */
    int endx;                  /* xpos at which the current 8-pixel tile ends */
    int win_state;             /* WIN_START / WIN_STARTED bits */
    int we_master;             /* window Y condition met earlier this frame */
    int nspr, cur_spr;         /* next sprite to fetch, sprite being fetched */
    uint8_t spr_x[11];         /* X of this line's sprites sorted; 0xFF ends the list */
    uint8_t scx, wx, wy, wy2, lcdc;   /* registers as the pipeline sees them */
    int ly;                    /* LY counter: the line the LCD is on */
    int64_t ly_time;           /* T-cycle at which the next line starts */
    int64_t last_m0;           /* T-cycle at which the last line's drawing ended */
    int plot_x;                /* pixel plotted by the latest step, or -1 */
    int line_valid;            /* sprite list of the first line after enable: none */
    struct { int64_t at; uint8_t op, val, mask; } q[8];   /* register writes in flight */
    int nq;
} PpuModel;

struct Machine {
    MachineModel model;

    /* cartridge */
    const uint8_t *rom;
    size_t rom_size;
    uint8_t rom_bank;          /* MBC1 bank mapped at 4000-7FFF */

    /* memories */
    uint8_t vram[0x2000];      /* 8000-9FFF */
    uint8_t wram[0x2000];      /* C000-DFFF (echoed at E000-FDFF) */
    uint8_t oam[0xA0];         /* FE00-FE9F */
    uint8_t io[0x80];          /* FF00-FF7F raw register storage */
    uint8_t hram[0x7F];        /* FF80-FFFE */
    uint8_t ie;                /* FFFF */

    /* timer */
    uint16_t div_counter;      /* internal 16-bit counter; DIV = high byte */
    int tima_reload_delay;     /* cycles until TIMA is reloaded after an overflow */
    int tima_follow;           /* cycles left in which TIMA still follows TMA */

    /* interrupts */
    int64_t min_int_time;      /* when IF&IE last became nonzero (HALT wake-up phase) */
    int64_t ack_block[5];      /* IF bit i is not set again until this cycle (after an ack) */
    int cpu_halted;            /* set by the CPU while it is in HALT */

    /* LCD controller */
    int lcd_on;
    int ly;                    /* LY counter: line 0..153 the LCD is on */
    int line_cycle;            /* cycle within that line, 0..455 (for tracing) */
    PpuModel ppu;
    uint8_t scan_y[40], scan_x[40], scan_large[40];   /* OAM as scanned on this line */
    int64_t enable_cycle;      /* when the LCD was last switched on */
    int stat_reg;              /* STAT interrupt enables as the LCD sees them (bits 3-6) */
    int64_t ev_m0, ev_m1, ev_m2, ev_lyc, ev_stat1, ev_wy2;   /* scheduled LCD events */
    int ms_stat, ms_lyc;       /* registers latched by the mode 0/1/2 interrupt logic */
    int li_stat, li_lyc, li_stat_src, li_lyc_src;   /* same for the LYC interrupt logic */
    int64_t m0_pred;           /* predicted end of drawing on this line (0 = unknown) */
    int vram_exact_read_done;
    int lcd_off_cycles;
    LcdState lcd;
    uint8_t framebuffer[LCD_W * LCD_H];   /* shades 0..3 */
    LcdRegs line_regs[LCD_H];  /* registers each line was drawn with (output only,
                                  for the frontend's widescreen view) */
    int fix_top_lines;         /* presentation only (frontend): redraw the top lines a
                                  late vertical-blank handler left with the old scroll */
    long top_lines_fixed;      /* frames in which that happened (for tests) */

    /* cartridge bus: the last value that crossed it, for reads of unmapped RAM */
    uint8_t cart_bus;
    int64_t cart_bus_time;

    /* OAM DMA */
    uint8_t dma_pos;           /* byte being copied; >= 160 when idle */
    uint8_t dma_start;         /* position at which a restarted transfer begins */
    int64_t dma_next;          /* cycle of the next copy step; INT64_MAX/4 when idle */
    int dma_src;               /* DMA_SRC_* class of the current source page */

    /* sound */
    Apu apu;

    /* joypad */
    uint8_t buttons;           /* BTN_* currently held */

    uint64_t cycles;           /* T-cycles since power on */
    uint64_t frame_count;
    int frame_done;            /* set at the start of vblank / every 70224 LCD-off cycles */

    MachineHooks hooks;
};

/* State as left by the console's boot ROM when it jumps to $0100. */
void machine_init(Machine *m, MachineModel model, const uint8_t *rom, size_t rom_size);
/* Parse "dmg" / "cgb" / "agb"; returns -1 if unknown. */
int machine_model_from_name(const char *name);

/* Advance the hardware by n T-cycles (n is a multiple of 4). */
void machine_tick(Machine *m, int n);

/* Bus access as seen by the CPU at the current cycle, including the LCD's
   VRAM/OAM locking, IO side effects and MBC1 writes. No time passes. */
uint8_t machine_read(Machine *m, uint16_t addr);
void machine_write(Machine *m, uint16_t addr, uint8_t v);

/* Side-effect-free read (debuggers, DMA source, timing shadow decoding). */
uint8_t machine_peek(const Machine *m, uint16_t addr);

/* Interrupts that are both requested (IF) and enabled (IE). */
static inline uint8_t machine_pending_interrupts(const Machine *m)
{
    return m->ie & m->io[0x0F] & 0x1F;
}

/* Acknowledge interrupt bit (clear it in IF) as the CPU does when it pushes
   the low byte of the return address. Interrupt requests the LCD or timer
   raise within the next two cycles are swallowed by the acknowledge. */
void machine_ack_interrupt(Machine *m, uint8_t bit);

/* Offset into the ROM image for a CPU address in 0000-7FFF. */
static inline size_t machine_rom_offset(const Machine *m, uint16_t addr)
{
    return addr < 0x4000 ? addr : (size_t)m->rom_bank * 0x4000 + (addr - 0x4000);
}

#endif
