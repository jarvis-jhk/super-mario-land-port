/* The Game Boy hardware minus the CPU. See machine.h.
 *
 * Time model. The CPU performs every bus access at the START of its machine
 * cycle and then lets four T-cycles pass (machine_tick). All hardware state
 * is therefore "as of the cycle in which the access happens": an event that
 * happens at T-cycle N is visible to an access made at cycle N or later.
 * The numbers in this file (line cycles, delays of a few cycles) are in this
 * time base. They come from measurements of real consoles (the hardware test
 * ROMs listed in docs/HARDWARE_ACCURACY.md); each one says which behaviour it
 * encodes.
 */
#include "machine.h"
#include <string.h>

/* IO register offsets (address - 0xFF00) */
enum {
    R_P1 = 0x00, R_DIV = 0x04, R_TIMA = 0x05, R_TMA = 0x06, R_TAC = 0x07, R_IF = 0x0F,
    R_LCDC = 0x40, R_STAT = 0x41, R_SCY = 0x42, R_SCX = 0x43, R_LY = 0x44,
    R_LYC = 0x45, R_DMA = 0x46, R_BGP = 0x47, R_OBP0 = 0x48, R_OBP1 = 0x49,
    R_WY = 0x4A, R_WX = 0x4B,
};

#define NEVER (INT64_MAX / 4)
#define LINE 456
#define FRAME (154 * LINE)
#define LCD_FIX_TOP_LINES 8     /* lines lcd_fix_top_lines looks at */

static int is_cgb(const Machine *m) { return m->model != MODEL_DMG; }
static int is_agb(const Machine *m) { return m->model == MODEL_AGB; }
static int64_t now(const Machine *m) { return (int64_t)m->cycles; }
static int64_t min64(int64_t a, int64_t b) { return a < b ? a : b; }
static int64_t max64(int64_t a, int64_t b) { return a > b ? a : b; }

/* ------------------------------------------------------------------ */
/* Interrupt flags                                                     */

static uint8_t pending(const Machine *m)
{
    return m->ie & m->io[R_IF] & 0x1F;
}

/* Raise interrupt request bits. `timed` requests come from a hardware event
   at the current cycle (they remember when IF&IE became nonzero, which sets
   the phase of the wake-up from HALT); the others come from register writes.
   A bit that was just acknowledged stays low for a few more cycles: the
   interrupt controller had already seen the request. */
static void irq_set(Machine *m, int bits, int timed)
{
    for (int i = 0; i < 5; i++)
        if (((bits >> i) & 1) && now(m) <= m->ack_block[i])
            bits &= ~(1 << i);
    int was_pending = pending(m) != 0;
    m->io[R_IF] |= (uint8_t)bits;
    if (timed && !was_pending && pending(m))
        m->min_int_time = max64(m->min_int_time, now(m));
}

void machine_ack_interrupt(Machine *m, uint8_t bit)
{
    m->io[R_IF] &= (uint8_t)~bit;
    for (int i = 0; i < 5; i++)
        if ((bit >> i) & 1)
            m->ack_block[i] = now(m) + (i == 2 ? 2 + is_cgb(m) : 2);
}

/* ------------------------------------------------------------------ */
/* Timer                                                               */

static const int tima_bits[4] = { 9, 3, 5, 7 };

static int timer_signal(uint16_t div, uint8_t tac)
{
    return (tac & 4) && ((div >> tima_bits[tac & 3]) & 1);
}

/* TIMA counts up when the selected DIV bit falls. An overflow shows 0 for
   three cycles, then TMA is loaded and the interrupt is requested. For the
   next four cycles TIMA keeps following TMA: a TMA write then reaches TIMA
   too, and a TIMA write is lost. */
static void timer_increment(Machine *m)
{
    if (++m->io[R_TIMA] == 0)
        m->tima_reload_delay = 3;
}

/* The 16-bit divider counter drives TIMA and the APU frame sequencer
   (falling edge of bit 12, 512 Hz). */
static void div_step(Machine *m, uint16_t value)
{
    int timer_before = timer_signal(m->div_counter, m->io[R_TAC]);
    int apu_before = (m->div_counter >> 12) & 1;
    m->div_counter = value;
    if (timer_before && !timer_signal(m->div_counter, m->io[R_TAC]))
        timer_increment(m);
    if (apu_before && !((m->div_counter >> 12) & 1))
        apu_frame_sequencer(&m->apu);
}

static void timer_tick(Machine *m)
{
    if (m->tima_follow)
        m->tima_follow--;
    if (m->tima_reload_delay && --m->tima_reload_delay == 0) {
        m->io[R_TIMA] = m->io[R_TMA];
        m->tima_follow = 4;
        irq_set(m, INT_TIMER, 1);
    }
    div_step(m, (uint16_t)(m->div_counter + 1));
}

/* ------------------------------------------------------------------ */
/* LCD: pixel pipeline timing                                          */
/*
 * Mode 3 (drawing) is not a fixed length. After a start-up phase the LCD
 * plots one pixel per cycle (x runs from 0 to 167; the first 8 pixels are
 * off-screen) and stalls for
 *   - sprites: when x reaches a sprite's X the pipeline waits for the tile
 *     fetch in progress to finish (up to 5 cycles, depending on where in the
 *     8-pixel tile the sprite starts) and then spends 6 cycles fetching it;
 *   - the window: 6 cycles when the window starts at WX.
 * The pipeline is modelled step by step (one step per cycle) so that
 * registers changed during the line (SCX, WX, LCDC) have the effect real
 * hardware has. The same step function, run on a copy of the state, predicts
 * when drawing will end; the LCD's interrupt and bus-blocking decisions use
 * that prediction.
 */
enum {
    PS_NONE,
    PS_M2_LY0,        /* line 0: window Y check (frame start) */
    PS_M2_A,          /* end of line: window Y check for this line */
    PS_M2_B,          /* end of line: window Y check for the next line */
    PS_M3_START,
    PS_M3_PREROLL,    /* drawing starts SCX mod 8 cycles late */
    PS_T0, PS_T1, PS_T2, PS_T3, PS_T4, PS_T5,   /* tile fetch steps */
    PS_L0, PS_L1, PS_L2, PS_L3, PS_L4, PS_L5,   /* sprite fetch steps */
    PS_W0, PS_W1, PS_W2, PS_W3, PS_W4, PS_W5,   /* window start steps */
};
enum { WIN_START = 1, WIN_STARTED = 2 };
enum { OP_SCX, OP_WX, OP_WY, OP_WY2, OP_LCDC };
enum { XPOS_END = 168 };

static int win_en(const PpuModel *P) { return (P->lcdc & 0x20) != 0; }
static int obj_en(const PpuModel *P) { return (P->lcdc & 0x02) != 0; }

/* Cycle (in the line) at which the drawing machinery starts. */
static int m3_start_lc(int cgb) { return 83 + cgb; }

static void ppu_apply(PpuModel *P, int op, uint8_t val, uint8_t mask)
{
    switch (op) {
    case OP_SCX: P->scx = val; break;
    case OP_WX:  P->wx = val; break;
    case OP_WY:  P->wy = val; break;
    case OP_WY2: P->wy2 = P->wy; break;
    case OP_LCDC: {
        uint8_t nl = (uint8_t)((P->lcdc & ~mask) | (val & mask));
        if ((P->lcdc ^ nl) & 0x20) {
            if (!(nl & 0x20)) {
                if (P->win_state == WIN_STARTED || P->xpos == XPOS_END)
                    P->win_state &= ~WIN_STARTED;
            } else if (P->win_state == WIN_START) {
                P->win_state |= WIN_STARTED;
            }
        }
        P->lcdc = nl;
        break;
    }
    }
}

/* A register write whose effect reaches the pipeline `delay` cycles after
   the write (the pipeline steps at the cycles after that see it). */
static void ppu_write(PpuModel *P, int64_t t, int op, uint8_t val, uint8_t mask, int delay)
{
    if (delay <= 0) {
        ppu_apply(P, op, val, mask);
        return;
    }
    if (P->nq == 8) {           /* cannot happen: writes are 4+ cycles apart */
        ppu_apply(P, P->q[0].op, P->q[0].val, P->q[0].mask);
        memmove(&P->q[0], &P->q[1], 7 * sizeof P->q[0]);
        P->nq--;
    }
    P->q[P->nq].at = t + delay + 1;
    P->q[P->nq].op = (uint8_t)op;
    P->q[P->nq].val = val;
    P->q[P->nq].mask = mask;
    P->nq++;
}

static void ps_next(PpuModel *P, int64_t t, int n, int state)
{
    P->state = state;
    P->at = t + n;
}

/* Sprites of line ly sorted by X (stable), as the hardware selects them:
   the first 10 in OAM order whose rows cover the line, using the Y, X and
   size values captured during the OAM scan (see lcd_scan_oam). */
static void ppu_build_sprites(const Machine *m, PpuModel *P, int ly)
{
    int n = 0;
    uint8_t xs[10];
    for (int i = 0; i < 40 && n < 10; i++) {
        int top = m->scan_y[i] - 16;
        int h = m->scan_large[i] ? 16 : 8;
        if (ly >= top && ly < top + h)
            xs[n++] = m->scan_x[i];
    }
    for (int i = 1; i < n; i++) {
        uint8_t v = xs[i];
        int j = i - 1;
        while (j >= 0 && xs[j] > v) { xs[j + 1] = xs[j]; j--; }
        xs[j + 1] = v;
    }
    memcpy(P->spr_x, xs, (size_t)n);
    P->spr_x[n] = 0xFF;
    P->nspr = 0;
}

static void plot_pixel(PpuModel *P, int cgb)
{
    int x = P->xpos;
    /* the window starts when drawing reaches WX */
    if (P->wx == x && (P->we_master || (P->wy2 == P->ly && win_en(P))) && x < 167) {
        if (P->win_state == 0 && win_en(P))
            P->win_state = WIN_START | WIN_STARTED;
        else if (!cgb && (P->win_state == 0 || x == 166))
            P->win_state |= WIN_START;
    }
    P->plot_x = x;
    P->xpos = x + 1;
}

static void plot_if_no_sprite(PpuModel *P, int cgb)
{
    if (P->spr_x[P->nspr] == P->xpos) {
        /* a sprite waits here: the pixel is held back (unless sprites are
           off on a DMG, where they are skipped) */
        if (!(obj_en(P) || cgb)) {
            do P->nspr++; while (P->spr_x[P->nspr] == P->xpos);
            plot_pixel(P, cgb);
        }
    } else {
        plot_pixel(P, cgb);
    }
}

/* True if a pending window start must restart the fetcher now. */
static int win_restart(PpuModel *P, int cgb)
{
    int start = 0;
    if (P->xpos < 167 || cgb) {
        P->win_state &= WIN_STARTED;
        start = P->win_state != 0;
    }
    if (!win_en(P))
        P->win_state &= ~WIN_STARTED;
    return start;
}

/* End of drawing: remember when, and wait for the next line's start. */
static void ppu_draw_end(PpuModel *P, int cgb, int64_t t)
{
    int64_t line_start = P->ly_time - LINE;
    P->last_m0 = t;
    if (P->ly < 143) {
        P->state = PS_M2_A;
        P->at = line_start + 450;
    } else {
        P->state = PS_M2_LY0;
        P->at = line_start + (int64_t)LINE * (154 - P->ly) + 1 + cgb;
    }
}

static void ppu_run(PpuModel *P, const Machine *m, int64_t t)
{
    int cgb = is_cgb(m);

    P->plot_x = -1;
    for (int i = 0; i < P->nq; ) {
        if (P->q[i].at <= t) {
            ppu_apply(P, P->q[i].op, P->q[i].val, P->q[i].mask);
            memmove(&P->q[i], &P->q[i + 1], (size_t)(P->nq - i - 1) * sizeof P->q[0]);
            P->nq--;
        } else {
            i++;
        }
    }
    if (P->state == PS_NONE || P->at != t)
        return;

    for (;;) {
        int st = P->state;
        switch (st) {
        case PS_M2_LY0:
            P->we_master = win_en(P) && P->wy == 0;
            ps_next(P, t, m3_start_lc(cgb) - (1 + cgb), PS_M3_START);
            return;
        case PS_M2_A:
            P->we_master |= win_en(P) && P->ly == P->wy;
            ps_next(P, t, 4, PS_M2_B);
            return;
        case PS_M2_B:
            P->we_master |= win_en(P) && P->ly + 1 == P->wy;
            ps_next(P, t, 2 + m3_start_lc(cgb), PS_M3_START);
            return;
        case PS_M3_START:
            P->xpos = 0;
            if ((P->win_state & WIN_START) && win_en(P))
                P->win_state = WIN_STARTED;
            else
                P->win_state = 0;
            P->state = PS_M3_PREROLL;
            continue;
        case PS_M3_PREROLL: {
            int fine = P->scx & 7;
            if (P->xpos < 80 && (P->xpos & 7) != fine) {
                P->xpos++;
                ps_next(P, t, 1, PS_M3_PREROLL);
                return;
            }
            ppu_build_sprites(m, P, P->ly);
            int sprite0 = P->spr_x[0] == 0;
            int delay = 1 - cgb;
            if (sprite0 && (obj_en(P) || cgb))
                delay += fine < 5 ? fine : 5;
            P->xpos = 0;
            P->endx = 8 - fine;
            {
                static const int first[8] = { PS_T0, PS_T1, PS_T2, PS_T3, PS_T4, PS_T5, PS_T5, PS_T5 };
                P->state = first[fine];
                P->at = t + delay;
            }
            if (delay > 0)
                return;
            continue;
        }

        case PS_T0:
            if ((P->win_state & WIN_START) && win_restart(P, cgb)) { P->state = PS_W0; continue; }
            P->endx = P->xpos + 8 < XPOS_END ? P->xpos + 8 : XPOS_END;
            goto tile_inc;
        case PS_T1: case PS_T2: case PS_T3: case PS_T4:
            if ((P->win_state & WIN_START) && win_restart(P, cgb)) { P->state = PS_W0; continue; }
        tile_inc:
            plot_if_no_sprite(P, cgb);
            if (P->xpos == XPOS_END)
                ppu_draw_end(P, cgb, t);
            else
                ps_next(P, t, 1, st == PS_T0 ? PS_T1 : st + 1);
            return;
        case PS_T5:
            if ((P->win_state & WIN_START) && win_restart(P, cgb)) { P->state = PS_W0; continue; }
            if (P->spr_x[P->nspr] == P->xpos) {
                if (obj_en(P) || cgb) {
                    P->cur_spr = P->nspr;
                    P->state = PS_L0;
                    continue;
                }
                do P->nspr++; while (P->spr_x[P->nspr] == P->xpos);
            }
            plot_pixel(P, cgb);
            if (P->xpos == P->endx) {
                if (P->endx < XPOS_END)
                    ps_next(P, t, 1, PS_T0);
                else
                    ppu_draw_end(P, cgb, t);
                return;
            }
            ps_next(P, t, 1, PS_T5);
            return;

        case PS_L0:
            ps_next(P, t, 1, PS_L1);
            return;
        case PS_L1: case PS_L2: case PS_L3: case PS_L4:
            if ((P->win_state & WIN_START) && win_restart(P, cgb)) { P->state = PS_W0; continue; }
            plot_if_no_sprite(P, cgb);
            goto sprite_inc;
        case PS_L5:
            if ((P->win_state & WIN_START) && win_restart(P, cgb)) { P->state = PS_W0; continue; }
            plot_if_no_sprite(P, cgb);
            if (P->cur_spr == P->nspr)
                P->nspr++;
        sprite_inc:
            if (P->xpos == P->endx) {
                if (P->xpos < XPOS_END)
                    ps_next(P, t, 1, PS_T0);
                else
                    ppu_draw_end(P, cgb, t);
            } else {
                ps_next(P, t, 1, st == PS_L5 ? PS_T5 : st + 1);
            }
            return;

        case PS_W0: case PS_W1: case PS_W2: case PS_W3: case PS_W4: case PS_W5:
            if (st == PS_W0 && P->xpos == P->endx)
                P->endx = P->xpos + 8 < XPOS_END ? P->xpos + 8 : XPOS_END;
            if (!win_en(P) && cgb) {
                plot_if_no_sprite(P, cgb);
                if (P->xpos == P->endx) {
                    if (P->xpos < XPOS_END)
                        ps_next(P, t, 1, PS_T0);
                    else
                        ppu_draw_end(P, cgb, t);
                    return;
                }
            }
            ps_next(P, t, 1, st == PS_W5 ? PS_T0 : st + 1);
            return;
        default:
            return;
        }
    }
}

/* The LY counter moves to the next line (after the steps of the cycle). */
static void ppu_ly_event(PpuModel *P)
{
    P->ly = P->ly == 153 ? 0 : P->ly + 1;
    P->ly_time += LINE;
    P->line_valid = 1;
}

/* Cycle at which the pipeline plots pixel `target` (the next time it does),
   assuming no more register changes. Used for the mode 0 interrupt (pixel
   166) and for the cycle at which VRAM/OAM/STAT see mode 0 (pixel 167). */
static int64_t ppu_predict(const Machine *m, int target)
{
    PpuModel P = m->ppu;
    for (int64_t t = now(m) + 1; t < now(m) + 2 * FRAME; t++) {
        ppu_run(&P, m, t);
        if (P.plot_x == target)
            return t;
        if (t == P.ly_time)
            ppu_ly_event(&P);
    }
    return NEVER;
}

/* ------------------------------------------------------------------ */
/* LCD: line counter, registers read back, bus blocking                */

static int lcd_lc(const Machine *m)
{
    return LINE - (int)(m->ppu.ly_time - now(m));
}

/* First cycle after cc at which the position within the frame is fc. */
static int64_t next_frame_cycle(const Machine *m, int64_t fc, int64_t cc)
{
    int64_t cur = (int64_t)m->ppu.ly * LINE + (LINE - (m->ppu.ly_time - cc));
    int64_t d = fc - cur;
    if (d <= 0)
        d += FRAME;
    return cc + d;
}

static int64_t next_line_cycle(const Machine *m, int lc, int64_t cc)
{
    int64_t cur = LINE - (m->ppu.ly_time - cc);
    int64_t d = lc - cur;
    if (d <= 0)
        d += LINE;
    return cc + d;
}

/* True if cc lies in the first 81 cycles after the LCD was switched on,
   when that first line behaves as if it were in hblank. */
static int inactive_after_enable(const Machine *m, int64_t cc)
{
    return cc < m->enable_cycle + 81;
}

static int64_t m0_time_of_line(Machine *m)
{
    if (m->m0_pred == 0 || now(m) >= m->m0_pred)
        m->m0_pred = ppu_predict(m, 167);
    return m->m0_pred < m->ppu.ly_time ? m->m0_pred : m->ppu.last_m0;
}

/* The line number LYC is compared with, and how many cycles that remains
   so. LY==LYC is evaluated 2 cycles before the line changes; on line 153
   the comparison already switches to line 0 after 6 cycles. */
typedef struct { int ly; int to_next; } LycCmp;

static LycCmp lyc_cmp(const Machine *m)
{
    LycCmp c = { m->ppu.ly, (int)(m->ppu.ly_time - now(m)) };
    if (c.ly == 153) {
        c.to_next -= LINE - 6;
        if (c.to_next <= 0) { c.ly = 0; c.to_next += LINE; }
    } else {
        c.to_next -= 2;
        if (c.to_next <= 0) { c.ly++; c.to_next += LINE; }
    }
    return c;
}

/* Mode bits and LY=LYC flag of STAT as read at this cycle. */
static int lcd_stat_bits(Machine *m)
{
    if (!m->lcd_on)
        return 0;
    int ly = m->ppu.ly, lc = lcd_lc(m), stat = 0;
    long fc = (long)ly * LINE + lc;
    int64_t cc = now(m);
    if (fc >= 144 * LINE - 3 && fc < FRAME - 3) {
        if (fc >= 144 * LINE - 2 && fc < FRAME - 4 + (ly == 153 && is_agb(m)))
            stat = 1;
    } else if (lc < 77 || lc >= LINE - 3) {
        if (!inactive_after_enable(m, cc + 1))
            stat = 2;
    } else if (cc + 2 < m0_time_of_line(m)) {
        if (!inactive_after_enable(m, cc + 1))
            stat = 3;
    }
    LycCmp c = lyc_cmp(m);
    if (m->io[R_LYC] == c.ly && c.to_next > 2 - is_agb(m))
        stat |= 4;
    return stat;
}

static int lcd_ly_reg(const Machine *m)
{
    if (!m->lcd_on)
        return 0;
    int ly = m->ppu.ly;
    int to_next = (int)(m->ppu.ly_time - now(m));
    if (ly == 153) {
        if (to_next <= LINE - is_agb(m))
            ly = 0;
    } else if (to_next <= 6) {
        ly = to_next == 6 ? (ly & (ly + 1)) : ly + 1;
    }
    return ly;
}

static int vram_readable(Machine *m)
{
    int64_t cc = now(m);
    if (!m->lcd_on || m->ppu.ly >= 144 || inactive_after_enable(m, cc + 1 - is_cgb(m)) ||
        lcd_lc(m) < 76 + 3 * is_cgb(m))
        return 1;
    return cc + 2 >= m0_time_of_line(m);
}

static int vram_writable(Machine *m)
{
    int64_t cc = now(m);
    if (!m->lcd_on || m->ppu.ly >= 144 || inactive_after_enable(m, cc + 1 - is_cgb(m)) ||
        lcd_lc(m) < 79)
        return 1;
    return cc + 2 >= m0_time_of_line(m);
}

static int oam_readable(Machine *m)
{
    int64_t cc = now(m);
    if (!m->lcd_on || inactive_after_enable(m, cc + 4))
        return 1;
    if (lcd_lc(m) + 4 >= LINE)
        return m->ppu.ly >= 143 && m->ppu.ly < 153;
    return m->ppu.ly >= 144 || cc + 2 >= m0_time_of_line(m);
}

static int oam_writable(Machine *m)
{
    int64_t cc = now(m);
    if (!m->lcd_on || inactive_after_enable(m, cc + 4))
        return 1;
    if (lcd_lc(m) + 3 + is_cgb(m) >= LINE)
        return m->ppu.ly >= 143 && m->ppu.ly < 153;
    return m->ppu.ly >= 144 || cc + 2 >= m0_time_of_line(m) ||
           (lcd_lc(m) == 76 && !is_cgb(m));
}

/* ------------------------------------------------------------------ */
/* LCD: STAT interrupt                                                 */
/*
 * The STAT interrupt line is the OR of four sources (LY=LYC, mode 0, mode 1,
 * mode 2), each enabled in STAT; a request is made when the line rises.
 * Rather than track the line, every source is an event with a precise time
 * (below), and an event requests the interrupt unless another enabled source
 * keeps the line high already ("blocked"). STAT and LYC writes reach this
 * logic with a few cycles of delay, so each source works on its own latched
 * copy of the registers (ms_*, li_*), which is refreshed when it fires.
 */
enum { ST_M0 = 0x08, ST_M1 = 0x10, ST_M2 = 0x20, ST_LYC = 0x40 };

/* When the LYC source next fires: 2 cycles before the line LYC starts;
   LYC=0 fires on line 153 (6 cycles in) where LY already reads 0. */
static int64_t lyc_schedule(const Machine *m, int stat, int lyc, int64_t cc)
{
    if (!(stat & ST_LYC) || lyc >= 154)
        return NEVER;
    return next_frame_cycle(m, lyc ? (int64_t)lyc * LINE - 2 : 153 * LINE + 6, cc);
}

static int64_t mode2_schedule(const Machine *m, int stat, int64_t cc)
{
    if (!(stat & ST_M2))
        return NEVER;
    int64_t last = 143 * LINE + 452, ly0 = 153 * LINE + 454;
    int64_t fc = (int64_t)m->ppu.ly * LINE + (LINE - (m->ppu.ly_time - cc));
    if ((fc >= last && fc < ly0) || (stat & ST_M0))
        return next_frame_cycle(m, ly0, cc);
    return next_line_cycle(m, 452, cc);
}

static int64_t mode1_schedule(const Machine *m, int64_t cc)
{
    return next_frame_cycle(m, 144 * LINE - 2, cc);
}

/* Pull events that depend on the length of drawing up to date after
   something that changes it. `adv` is how far ahead of the write the
   pipeline had been stepped when the write took effect. */
static void mode3_cycles_change(Machine *m, int adv)
{
    m->m0_pred = 0;
    if (m->ev_m0 != NEVER && m->ev_m0 > now(m) + adv)
        m->ev_m0 = ppu_predict(m, 166);
}

/* An LYC-source register pair changed. */
static void lyc_irq_reg_change(Machine *m, int stat, int lyc)
{
    int64_t cc = now(m);
    int64_t src_time = lyc_schedule(m, stat, lyc, cc);
    m->li_stat_src = stat;
    m->li_lyc_src = lyc;
    m->ev_lyc = min64(m->ev_lyc, src_time);
    uint64_t left = (uint64_t)(m->ev_lyc - cc);
    if (is_cgb(m)) {
        if (left > 6 || (src_time != m->ev_lyc && left > 2))
            m->li_lyc = lyc;
        if (left > 2)
            m->li_stat = stat;
    } else {
        if (left > 4 || src_time != m->ev_lyc)
            m->li_lyc = lyc;
        m->li_stat = stat;
    }
}

static void lyc_event(Machine *m)
{
    int flag = 0;
    if ((m->li_stat | m->li_stat_src) & ST_LYC) {
        int cmp = m->ppu.ly == 153 ? 0 : m->ppu.ly + 1;
        int blocked = (m->li_lyc <= 144 && m->li_lyc > 0) ? (m->li_stat & ST_M2)
                                                          : (m->li_stat & ST_M1);
        flag = m->li_lyc == cmp && !blocked;
    }
    int64_t t = m->ev_lyc;
    m->li_lyc = m->li_lyc_src;
    m->li_stat = m->li_stat_src;
    m->ev_lyc = lyc_schedule(m, m->li_stat, m->li_lyc, t);
    if (flag)
        irq_set(m, INT_STAT, 1);
}

static void m0_event(Machine *m)
{
    int flag = ((m->stat_reg | m->ms_stat) & ST_M0) &&
               (!(m->ms_stat & ST_LYC) || m->ppu.ly != m->ms_lyc);
    m->ms_lyc = m->io[R_LYC];
    m->ms_stat = m->stat_reg;
    m->ev_m0 = (m->stat_reg & ST_M0) ? ppu_predict(m, 166) : NEVER;
    if (flag)
        irq_set(m, INT_STAT, 1);
}

static void m1_event(Machine *m)
{
    int flag = (m->stat_reg & ST_M1) && !(m->ms_stat & (ST_M2 | ST_M0));
    m->ms_stat = m->stat_reg;
    m->ev_m1 += FRAME;
    irq_set(m, flag ? INT_VBLANK | INT_STAT : INT_VBLANK, 1);
    m->frame_done = 1;
}

static void m2_event(Machine *m)
{
    /* the line this mode 2 belongs to: the next one if we are at the end of this */
    int ly = (m->ppu.ly_time - m->ev_m2 < 16) ? (m->ppu.ly == 153 ? 0 : m->ppu.ly + 1) : m->ppu.ly;
    int blocked_m1 = ly == 0 && (m->ms_stat & ST_M1);
    int blocked_lyc = (m->ms_stat & ST_LYC) && (ly == 0 ? ly : ly - 1) == m->ms_lyc;
    int flag = !blocked_m1 && !blocked_lyc;
    m->ms_lyc = m->io[R_LYC];
    m->ms_stat = m->stat_reg;
    if (flag)
        irq_set(m, INT_STAT, 1);
    int64_t next = FRAME;
    if (!(m->stat_reg & ST_M0)) {
        next = LINE;
        if (ly == 0)
            next -= 2;
        else if (ly == 144)
            next += LINE * 9 + 2;
    }
    m->ev_m2 += next;
}

/* Does writing STAT (old -> data) raise the interrupt line for a moment?
   On a DMG every enable bit is momentarily set during the write, so the
   interrupt fires if a source would be true right now and was not enabled
   before. The Color's logic is different and depends on exact cycles. */
static int stat_write_triggers_dmg(Machine *m, int old)
{
    LycCmp c = lyc_cmp(m);
    int lyc = m->li_lyc_src;
    if (m->ppu.ly < 144) {
        int64_t m0 = m->ev_m0;
        if (m0 == NEVER && m->ppu.ly_time - now(m) < LINE - 80 - 160)
            m0 = ppu_predict(m, 166);
        if (m0 == NEVER || m0 < m->ppu.ly_time)
            return c.ly == lyc && !(old & ST_LYC);
        return !(old & ST_M0) && !(c.ly == lyc && (old & ST_LYC));
    }
    return !(old & ST_M1) && !(c.ly == lyc && (old & ST_LYC));
}

static int stat_write_triggers_cgb(Machine *m, int old, int data)
{
    if (!(data & ~old & (ST_LYC | ST_M2 | ST_M1 | ST_M0)))
        return 0;
    int ly = m->ppu.ly;
    int to_next = (int)(m->ppu.ly_time - now(m));
    LycCmp c = lyc_cmp(m);
    int lycperiod = c.ly == m->li_lyc_src && c.to_next > 2;
    if (lycperiod && (old & ST_LYC))
        return 0;
    int lyc_now = lycperiod && (data & ST_LYC);

    /* mode 0, LYC or mode 1 */
    int a;
    if (ly < 143 || (ly == 143 && to_next > 2)) {
        if (m->ev_m0 < m->ppu.ly_time || to_next <= 4)
            a = lyc_now;
        else if (old & ST_M0)
            a = 0;
        else
            a = (data & ST_M0) || lyc_now;
    } else if ((old & ST_M1) && (ly < 153 || to_next > 3)) {
        a = 0;
    } else {
        a = ((data & ST_M1) && (ly < 153 || to_next > 4)) || lyc_now;
    }
    if (a)
        return 1;

    /* mode 2: only in the last few cycles before the line ends */
    if ((old & ST_M2) || (data & (ST_M2 | ST_M0)) != ST_M2)
        return 0;
    if (ly < 143)
        return to_next <= 4 && to_next > 2;
    return 0;
}

static void lcd_stat_write(Machine *m, uint8_t data)
{
    int old = m->stat_reg;
    int stat = data & 0x78;
    m->stat_reg = stat;
    lyc_irq_reg_change(m, stat, m->li_lyc_src);
    if (m->lcd_on) {
        if ((stat & ST_M0) && m->ev_m0 == NEVER)
            m->ev_m0 = ppu_predict(m, 166);
        m->ev_m2 = mode2_schedule(m, stat, now(m));
        if (is_cgb(m) ? stat_write_triggers_cgb(m, old, stat) : stat_write_triggers_dmg(m, old))
            irq_set(m, INT_STAT, 0);
    }
    if (now(m) + 2 * is_cgb(m) < min64(min64(m->ev_m0, m->ev_m1), m->ev_m2))
        m->ms_stat = stat;
}

static int lyc_write_blocked(Machine *m, int data)
{
    int to_next = (int)(m->ppu.ly_time - now(m));
    if (m->ppu.ly < 144)
        return (m->stat_reg & ST_M0) && m->ev_m0 > m->ppu.ly_time && data == m->ppu.ly;
    return (m->stat_reg & ST_M1) && !(m->ppu.ly == 153 && to_next <= 2 + 2 * is_cgb(m));
}

static int lyc_write_triggers(Machine *m, int old, int data)
{
    if (!(m->stat_reg & ST_LYC) || data >= 154 || lyc_write_blocked(m, data))
        return 0;
    LycCmp c = lyc_cmp(m);
    if (c.to_next <= 4 + 2 * is_cgb(m)) {
        if (old == c.ly && c.to_next > 2 * is_cgb(m))
            return 0;          /* LY and LYC change together: the flag never drops */
        c.ly = c.ly == 153 ? 0 : c.ly + 1;
    }
    return data == c.ly;
}

static void lcd_lyc_write(Machine *m, uint8_t data)
{
    int old = m->li_lyc_src;
    if (data == old)
        return;
    lyc_irq_reg_change(m, m->li_stat_src, data);
    if (now(m) + 5 * is_cgb(m) + 1 < min64(m->ev_m0, m->ev_m2))
        m->ms_lyc = data;
    if (m->lcd_on && lyc_write_triggers(m, old, data)) {
        if (is_cgb(m))
            m->ev_stat1 = now(m) + 5;
        else
            irq_set(m, INT_STAT, 0);
    }
}

/* ------------------------------------------------------------------ */
/* LCD: switching on and off, per-cycle update                         */

static void lcd_enable(Machine *m)
{
    int cgb = is_cgb(m);
    int64_t cc = now(m);
    PpuModel *P = &m->ppu;
    m->lcd_on = 1;
    m->enable_cycle = cc;
    memset(m->scan_y, 0, sizeof m->scan_y);
    memset(m->scan_x, 0, sizeof m->scan_x);
    memset(m->scan_large, 0, sizeof m->scan_large);
    P->ly = 0;
    P->ly_time = cc + LINE;
    P->nq = 0;
    P->win_state = 0;
    P->we_master = (P->lcdc & 0x20) && P->wy == 0;
    P->last_m0 = 0;
    P->line_valid = 0;
    P->xpos = 0;
    ps_next(P, cc, m3_start_lc(cgb) + 2, PS_M3_START);
    lcd_begin_frame(&m->lcd);
    m->ly = 0;
    m->li_lyc = m->li_lyc_src;
    m->li_stat = m->li_stat_src;
    m->ms_lyc = m->li_lyc_src;
    m->m0_pred = 0;
    m->ev_lyc = min64(lyc_schedule(m, m->li_stat, m->li_lyc, cc),
                      lyc_schedule(m, m->li_stat_src, m->li_lyc_src, cc));
    m->ev_m1 = mode1_schedule(m, cc);
    m->ev_m2 = mode2_schedule(m, m->stat_reg, cc);
    m->ev_m0 = (m->stat_reg & ST_M0) ? ppu_predict(m, 166) : NEVER;
    m->ev_stat1 = m->ev_wy2 = NEVER;
}

static void lcd_disable(Machine *m)
{
    m->lcd_on = 0;
    m->ly = 0;
    m->ppu.state = PS_NONE;
    m->ppu.at = NEVER;
    m->ppu.nq = 0;
    m->ev_m0 = m->ev_m1 = m->ev_m2 = m->ev_lyc = m->ev_stat1 = m->ev_wy2 = NEVER;
    m->lcd_off_cycles = 0;
    memset(m->framebuffer, 0, sizeof m->framebuffer);
}

static void lcd_fix_top_lines(Machine *m, const LcdRegs *r);

static void lcd_render_current_line(Machine *m)
{
    LcdRegs r = {
        m->io[R_LCDC], m->io[R_SCY], m->io[R_SCX], m->io[R_WY],
        m->io[R_WX], m->io[R_BGP], m->io[R_OBP0], m->io[R_OBP1],
    };
    lcd_render_line(&m->lcd, &r, m->ppu.ly, m->vram, m->oam,
                    m->framebuffer + m->ppu.ly * LCD_W);
    if (m->ppu.ly < LCD_H)
        m->line_regs[m->ppu.ly] = r;
    if (m->fix_top_lines)
        lcd_fix_top_lines(m, &r);
}

/* Presentation only: when the vertical-blank handler runs late, its
   SCX = SCY = 0 for the status bar lands after the first line(s) were drawn
   with the previous frame's playfield scroll, and the top of the status bar
   flickers. Once a line near the top shows those registers, the earlier lines
   that differ from it only in scroll are drawn again with them. */
static void lcd_fix_top_lines(Machine *m, const LcdRegs *r)
{
    int ly = m->ppu.ly;
    if (ly < 1 || ly >= LCD_FIX_TOP_LINES || r->scx || r->scy)
        return;
    const LcdRegs *top = &m->line_regs[0];
    if (top->scx == 0 && top->scy == 0)
        return;
    for (int y = 0; y < ly; y++) {
        const LcdRegs *s = &m->line_regs[y];
        if (s->scx != top->scx || s->scy != top->scy || s->lcdc != r->lcdc || s->wy != r->wy ||
            s->wx != r->wx || s->bgp != r->bgp || s->obp0 != r->obp0 || s->obp1 != r->obp1)
            return;
    }
    if ((r->lcdc & LCDC_WIN_ON) && r->wy < ly)
        return;     /* the window counter has moved on; leave it */
    m->top_lines_fixed++;
    for (int y = 0; y < ly; y++) {
        LcdState scratch = { 0, 0 };
        lcd_render_line(&scratch, r, y, m->vram, m->oam, m->framebuffer + y * LCD_W);
        m->line_regs[y] = *r;
    }
}

/* The OAM scan (line cycles 0..79) looks at one byte pair per cycle: entry i
   has its Y and X read at cycle 2i, and its size (LCDC bit 2) is taken at
   cycle 2i+1 (on a Color also at 2i, and either being large counts). So
   changes to OAM, to the size bit, or a DMA that blocks OAM, during the scan
   affect only the entries not yet scanned. */
static void lcd_scan_oam(Machine *m, int lc)
{
    int i = lc >> 1;
    int large = (m->ppu.lcdc >> 2) & 1;
    if (!(lc & 1)) {
        int blocked = m->dma_pos < 160;
        m->scan_y[i] = blocked ? 0xFF : m->oam[i * 4];
        m->scan_x[i] = blocked ? 0xFF : m->oam[i * 4 + 1];
        if (is_cgb(m))
            m->scan_large[i] = (uint8_t)large;
    } else {
        m->scan_large[i] = is_cgb(m) ? (uint8_t)(m->scan_large[i] | large) : (uint8_t)large;
    }
}

static void lcd_tick(Machine *m)
{
    int64_t t = now(m);
    PpuModel *P = &m->ppu;

    ppu_run(P, m, t);

    /* scheduled events, in the order they are handled when simultaneous */
    if (m->ev_stat1 <= t) {
        m->ev_stat1 = NEVER;
        irq_set(m, INT_STAT, 0);
    }
    if (m->ev_wy2 <= t) {
        m->ev_wy2 = NEVER;
        ppu_apply(P, OP_WY2, 0, 0);
        mode3_cycles_change(m, 0);
    }
    if (m->ev_m1 <= t)
        m1_event(m);
    if (m->ev_lyc <= t)
        lyc_event(m);
    if (lcd_lc(m) == 80) {
        /* the sprites of the next lines are looked up now */
        if (P->ly < 144)
            lcd_render_current_line(m);
        mode3_cycles_change(m, 0);
    }
    if (m->ev_m2 <= t)
        m2_event(m);
    if (m->ev_m0 <= t)
        m0_event(m);

    if (t == P->ly_time) {
        ppu_ly_event(P);
        m->ly = P->ly;
        if (P->ly == 0)
            lcd_begin_frame(&m->lcd);
    }
    m->line_cycle = lcd_lc(m) % LINE;
    if (P->line_valid && m->line_cycle < 80)
        lcd_scan_oam(m, m->line_cycle);
}

/* ------------------------------------------------------------------ */
/* OAM DMA                                                             */
/*
 * Writing DMA starts a copy of 160 bytes, one per machine cycle, beginning
 * 8 cycles after the write. While it runs OAM cannot be accessed, and the
 * CPU bus conflicts with the DMA source (reads return the byte being
 * copied). The model follows the hardware's byte counter, so a transfer
 * restarted while running behaves like the real thing.
 */
enum { DMA_ROM, DMA_VRAM, DMA_SRAM, DMA_WRAM, DMA_INVALID, DMA_OFF };

static int dma_src_class(const Machine *m, uint8_t page)
{
    if (page < 0x80) return DMA_ROM;
    if (page < 0xA0) return DMA_VRAM;
    if (page < 0xC0) return DMA_SRAM;
    if (page < 0x100 - is_cgb(m) * 0x20) return DMA_WRAM;
    return DMA_INVALID;
}

/* Which 4 KiB pages of the CPU address space conflict with the transfer. */
static int dma_conflict(const Machine *m, uint16_t addr)
{
    static const uint16_t dmg[] = { 0xFCFF, 0x0300, 0xFCFF, 0xFCFF, 0x0000, 0 };
    static const uint16_t cgb[] = { 0xFCFF, 0x0300, 0xFCFF, 0xF000, 0xFCFF, 0 };
    const uint16_t *tab = is_cgb(m) ? cgb : dmg;
    return (tab[m->dma_src] >> (addr >> 12)) & 1;
}

static uint8_t dma_source_byte(const Machine *m, int i)
{
    uint8_t page = m->io[R_DMA];
    switch (m->dma_src) {
    case DMA_ROM:  return machine_peek(m, (uint16_t)((page << 8) + i));
    case DMA_VRAM: return m->vram[((page << 8) + i) & 0x1FFF];
    case DMA_WRAM: return m->wram[(((page >> 4) & 1) * 0x1000) + (((page & 0xF) << 8) + i)];
    default:       return 0xFF;
    }
}

static void dma_step(Machine *m)
{
    m->dma_pos = (uint8_t)(m->dma_pos + 1);
    if (m->dma_pos == m->dma_start) {
        m->dma_pos = 0;
        m->dma_start = 0;
    }
    if (m->dma_pos < 160) {
        m->oam[m->dma_pos] = dma_source_byte(m, m->dma_pos);
    } else if (m->dma_pos == 160) {
        if (m->dma_start == 0) {
            m->dma_pos = 254;
            m->dma_src = DMA_OFF;
            m->dma_next = NEVER;
            return;
        }
    }
    m->dma_next += 4;
}

static void dma_tick(Machine *m)
{
    if (m->dma_next == NEVER)
        return;
    if (m->cpu_halted)
        m->dma_next++;          /* the transfer is frozen while the CPU sleeps */
    else if (now(m) == m->dma_next)
        dma_step(m);
}

/* ------------------------------------------------------------------ */

void machine_tick(Machine *m, int n)
{
    for (int i = 0; i < n; i++) {
        m->cycles++;
        timer_tick(m);
        if (m->lcd_on) {
            lcd_tick(m);
        } else if (++m->lcd_off_cycles >= FRAME) {
            m->lcd_off_cycles = 0;
            m->frame_done = 1;
        }
        dma_tick(m);
    }
    apu_tick(&m->apu, n);
}

static uint8_t joypad_read(const Machine *m)
{
    uint8_t sel = m->io[R_P1] & 0x30;
    uint8_t v = 0x0F;
    if (!(sel & 0x10)) v &= (uint8_t)~((m->buttons >> 4) & 0x0F);  /* directions */
    if (!(sel & 0x20)) v &= (uint8_t)~(m->buttons & 0x0F);         /* buttons */
    return 0xC0 | sel | v;
}

static uint8_t io_read(Machine *m, uint16_t addr)
{
    int r = addr & 0x7F;
    if (addr >= 0xFF10 && addr < 0xFF40)
        return apu_read(&m->apu, addr);
    switch (r) {
    case R_P1:   return joypad_read(m);
    case R_DIV:  return (uint8_t)(m->div_counter >> 8);
    case R_TAC:  return m->io[R_TAC] | 0xF8;
    case R_IF:   return m->io[R_IF] | 0xE0;
    case R_STAT: return (uint8_t)((m->io[R_STAT] & 0xF8) | 0x80 | lcd_stat_bits(m));
    case R_LY:   return (uint8_t)lcd_ly_reg(m);
    }
    return m->io[r];
}

static void lcdc_write(Machine *m, uint8_t v)
{
    uint8_t old = m->io[R_LCDC];
    if (old == v)
        return;
    int cgb = is_cgb(m);
    if ((old ^ v) & 0x80) {
        /* STAT as it was read just before the switch: its LY=LYC flag is
           kept while the LCD is off */
        int stat = (v & 0x80) ? m->io[R_STAT] : (m->io[R_STAT] | lcd_stat_bits(m));
        if (v & 0x80) {
            m->ppu.lcdc = v;
            lcd_enable(m);
        } else {
            lcd_disable(m);
            m->ppu.lcdc = v;
        }
        m->io[R_LY] = 0;
        m->io[R_STAT] &= 0xF8;
        if (v & 0x80) {
            if ((m->io[R_STAT] & ST_LYC) && m->io[R_LYC] == 0 && !(stat & 4))
                irq_set(m, INT_STAT, 0);
        } else {
            m->io[R_STAT] |= stat & 4;
        }
    } else if (!(v & 0x80)) {
        ppu_apply(&m->ppu, OP_LCDC, v, 0xFF);
    } else if (cgb) {
        ppu_write(&m->ppu, now(m), OP_LCDC, v, 0xFF, 2);
        if ((old ^ v) & 0x20)
            mode3_cycles_change(m, 2);
    } else {
        ppu_write(&m->ppu, now(m), OP_LCDC, v, (uint8_t)~0x04, 0);
        int obj2x = (old ^ v) & 0x04;
        if (obj2x)
            ppu_write(&m->ppu, now(m), OP_LCDC, v, 0x04, 2);
        if ((old ^ v) & (0x20 | 0x02))
            mode3_cycles_change(m, obj2x ? 2 : 0);
    }
    m->io[R_LCDC] = v;
}

static void io_write(Machine *m, uint16_t addr, uint8_t v)
{
    int r = addr & 0x7F;
    if (addr >= 0xFF10 && addr < 0xFF40) {
        apu_write(&m->apu, addr, v);
        return;
    }
    switch (r) {
    case R_DIV: {
        /* Resetting the divider can make the selected bit fall: TIMA
           counts, also if it would have risen within 3 cycles. */
        uint8_t tac = m->io[R_TAC];
        if (tac & 4) {
            int half = 1 << tima_bits[tac & 3];
            if ((m->div_counter & ((half << 1) - 1)) >= half - 3)
                timer_increment(m);
        }
        int apu_before = (m->div_counter >> 12) & 1;
        m->div_counter = 0;
        if (apu_before)
            apu_frame_sequencer(&m->apu);
        return;
    }
    case R_TIMA:
        m->tima_reload_delay = 0;
        if (!m->tima_follow)
            m->io[r] = v;
        return;
    case R_TMA:
        m->io[r] = v;
        if (m->tima_follow)
            m->io[R_TIMA] = v;
        return;
    case R_TAC: {
        uint8_t old = m->io[R_TAC], nv = v & 7;
        if (old != nv) {
            if (old & 4) {
                /* Changing the timer while it runs can make the signal fall
                   (the new bit being 0 while the old one is 1) and TIMA
                   count; a boundary due within 3 cycles counts as well. */
                int ob = tima_bits[old & 3];
                int new_high = (nv & 4) && ((m->div_counter >> tima_bits[nv & 3]) & 1);
                uint32_t period = 2u << ob;
                uint32_t phase = (m->div_counter & (period - 1)) + (new_high ? 0 : 1u << ob) + 3;
                if (phase >= period)
                    timer_increment(m);
            }
            /* AGB quirk: enabling or retuning counts a falling edge
               between the old and the new selected bit, enabled or not. */
            if (is_agb(m) && (nv & 4) &&
                ((m->div_counter >> tima_bits[old & 3]) & 1) &&
                !((m->div_counter >> tima_bits[nv & 3]) & 1))
                timer_increment(m);
        }
        m->io[r] = nv;
        return;
    }
    case R_IF:
        /* requests due within the next cycle are raised first, then lost */
        for (int i = 0; i < 5; i++)
            if (!((v >> i) & 1))
                m->ack_block[i] = now(m) + 1;
        m->io[r] = v & 0x1F;
        return;
    case R_LCDC: lcdc_write(m, v); return;
    case R_STAT:
        lcd_stat_write(m, v);
        if (!m->lcd_on && (m->io[R_STAT] & 4) &&
            (~m->io[R_STAT] & ST_LYC & (is_cgb(m) ? v : -1)))
            irq_set(m, INT_STAT, 0);
        m->io[r] = (uint8_t)((m->io[r] & 0x87) | (v & 0x78));
        return;
    case R_LY:   return;
    case R_SCX:
        if (m->lcd_on) {
            ppu_write(&m->ppu, now(m), OP_SCX, v, 0xFF, 2 * is_cgb(m));
            mode3_cycles_change(m, 2 * is_cgb(m));
        } else {
            m->ppu.scx = v;
        }
        break;
    case R_WX:
        if (m->lcd_on) {
            ppu_write(&m->ppu, now(m), OP_WX, v, 0xFF, 1 + is_cgb(m));
            mode3_cycles_change(m, 1 + is_cgb(m));
        } else {
            m->ppu.wx = v;
        }
        break;
    case R_WY:
        if (m->lcd_on) {
            ppu_write(&m->ppu, now(m), OP_WY, v, 0xFF, 1 + is_cgb(m));
            if (is_cgb(m)) {
                m->ev_wy2 = now(m) + 6;
            } else {
                ppu_write(&m->ppu, now(m), OP_WY2, 0, 0, 2);
                mode3_cycles_change(m, 2);
            }
        } else {
            m->ppu.wy = v;
            m->ppu.wy2 = v;
        }
        break;
    case R_LYC:
        lcd_lyc_write(m, v);
        m->io[r] = v;
        return;
    case R_DMA:
        /* the transfer starts 8 cycles from now; a running one carries on
           for 2 more bytes first */
        m->io[r] = v;
        m->dma_start = (uint8_t)(m->dma_pos + 2);
        m->dma_next = now(m) + 4;
        m->dma_src = dma_src_class(m, v);
        return;
    }
    /* Only real registers keep what is written to them. */
    switch (r) {
    case R_P1: m->io[r] = (m->io[r] & 0xCF) | (v & 0x30); break;   /* button/direction select */
    case 0x01: m->io[r] = v; break;
    case 0x02: m->io[r] = v | 0x7E; break;       /* serial control (no transfers modelled) */
    case R_SCY: case R_SCX: case R_BGP: case R_OBP0: case R_OBP1: case R_WY: case R_WX:
        m->io[r] = v;
        break;
    case 0x72: case 0x73:
        if (is_cgb(m)) m->io[r] = v;
        break;
    case 0x75:
        if (is_cgb(m)) m->io[r] = v | 0x8F;
        break;
    }
}

uint8_t machine_peek(const Machine *m, uint16_t addr)
{
    if (addr < 0x8000) {
        size_t off = machine_rom_offset(m, addr);
        return off < m->rom_size ? m->rom[off] : 0xFF;
    }
    if (addr < 0xA000) return m->vram[addr - 0x8000];
    if (addr < 0xC000) return 0xFF;                    /* no cartridge RAM */
    if (addr < 0xE000) return m->wram[addr - 0xC000];
    if (addr < 0xFE00) return m->wram[addr - 0xE000];
    if (addr < 0xFEA0) return m->oam[addr - 0xFE00];
    if (addr < 0xFF00) return 0x00;
    if (addr < 0xFF80) {
        if (addr == 0xFF04) return (uint8_t)(m->div_counter >> 8);
        if (addr == 0xFF44) return (uint8_t)lcd_ly_reg(m);
        return m->io[addr & 0x7F];
    }
    if (addr < 0xFFFF) return m->hram[addr - 0xFF80];
    return m->ie;
}

uint8_t machine_read(Machine *m, uint16_t addr)
{
    uint8_t v;
    if (addr >= 0xFF00) {
        if (addr < 0xFF80) {
            v = io_read(m, addr);
            if (m->hooks.on_io_read)
                m->hooks.on_io_read(m, addr, v, m->hooks.user);
            return v;
        }
        return addr < 0xFFFF ? m->hram[addr - 0xFF80] : m->ie;
    }
    if (m->dma_pos < 160 && addr < 0xFE00 && dma_conflict(m, addr)) {
        /* the bus is busy with the transfer: we see the byte being copied */
        v = m->oam[m->dma_pos];
        if (is_cgb(m) && m->dma_src != DMA_WRAM && addr >= 0xC000)
            v = m->wram[(((m->io[R_DMA] >> 4) & 1) * 0x1000) + (addr & 0xFFF)];
        if (is_cgb(m) && m->dma_src == DMA_VRAM)
            m->oam[m->dma_pos] = 0;
    } else if (addr >= 0xA000 && addr < 0xC000) {
        /* no cartridge RAM: the bus floats and still carries the last value
           read from the cartridge (or, on the original, from work RAM) */
        v = now(m) - m->cart_bus_time >= 8 ? 0xFF : m->cart_bus;
    } else if (addr >= 0x8000 && addr < 0xA000) {
        if (!vram_readable(m)) {
            v = 0xFF;
        } else {
            v = m->vram[addr - 0x8000];
            /* one read at the very cycle drawing ends sees zero */
            if (addr < 0x9000 && m->lcd_on && !m->vram_exact_read_done &&
                now(m) + 2 == m0_time_of_line(m)) {
                m->vram_exact_read_done = 1;
                v = 0;
            }
        }
    } else if (addr >= 0xFE00) {
        if (!oam_readable(m) || m->dma_pos < 160)
            v = 0xFF;
        else
            v = machine_peek(m, addr);
    } else {
        v = machine_peek(m, addr);
    }
    if (addr < 0x8000 || (!is_cgb(m) && addr >= 0xC000)) {
        m->cart_bus = v;
        m->cart_bus_time = now(m);
    }
    if (m->hooks.on_mem_read &&
        ((addr >= 0x8000 && addr < 0xE000) || (addr >= 0xFE00 && addr < 0xFEA0)))
        m->hooks.on_mem_read(m, addr, v, m->hooks.user);
    return v;
}

void machine_write(Machine *m, uint16_t addr, uint8_t v)
{
    if (addr < 0xFE00 && m->dma_pos < 160 && dma_conflict(m, addr)) {
        /* writes during the transfer land in OAM instead */
        if (is_cgb(m)) {
            if (addr < 0xC000)
                m->oam[m->dma_pos] = m->dma_src != DMA_VRAM ? v : 0;
            else if (m->dma_src != DMA_WRAM)
                m->wram[(((m->io[R_DMA] >> 4) & 1) * 0x1000) + (addr & 0xFFF)] = v;
        } else {
            m->oam[m->dma_pos] = m->dma_src == DMA_WRAM ? (m->oam[m->dma_pos] & v) : v;
        }
        return;
    }
    if (addr < 0x8000) {
        if (addr >= 0x2000 && addr < 0x4000) {     /* MBC1 ROM bank */
            uint8_t bank = v & 0x1F;
            if (bank == 0) bank = 1;
            m->rom_bank = (uint8_t)(bank & ((m->rom_size / 0x4000) - 1));
        }
        return;
    }
    if (addr < 0xA000) {
        if (!vram_writable(m))
            return;
        m->vram[addr - 0x8000] = v;
    } else if (addr < 0xC000) {
        return;
    } else if (addr < 0xE000) {
        m->wram[addr - 0xC000] = v;
    } else if (addr < 0xFE00) {
        m->wram[addr - 0xE000] = v;
        return;
    } else if (addr < 0xFEA0) {
        if (!oam_writable(m) || m->dma_pos < 160)
            return;
        m->oam[addr - 0xFE00] = v;
    } else if (addr < 0xFF00) {
        return;
    } else {
        if (addr < 0xFF80) io_write(m, addr, v);
        else if (addr < 0xFFFF) m->hram[addr - 0xFF80] = v;
        else m->ie = v;
        if (m->hooks.on_io_write)
            m->hooks.on_io_write(m, addr, v, m->hooks.user);
        return;
    }
    if (m->hooks.on_mem_write)
        m->hooks.on_mem_write(m, addr, v, m->hooks.user);
}

int machine_model_from_name(const char *name)
{
    if (!strcmp(name, "dmg")) return MODEL_DMG;
    if (!strcmp(name, "cgb")) return MODEL_CGB;
    if (!strcmp(name, "agb")) return MODEL_AGB;
    return -1;
}

void machine_init(Machine *m, MachineModel model, const uint8_t *rom, size_t rom_size)
{
    int16_t *audio = m->apu.out;
    size_t audio_cap = m->apu.out_cap;
    memset(m, 0, sizeof *m);
    m->apu.out = audio;
    m->apu.out_cap = audio_cap;
    m->model = model;
    m->rom = rom;
    m->rom_size = rom_size;
    m->rom_bank = 1;
    apu_init(&m->apu);

    /* Hardware state when the boot ROM jumps to $0100. The timing values
       (divider phase, LCD position) are what the real boot ROMs leave. */
    memset(m->io, 0xFF, sizeof m->io);
    m->io[R_P1] = 0xCF;
    m->io[0x01] = 0x00;
    m->io[0x02] = model == MODEL_DMG ? 0x7E : 0x7C;
    m->io[R_TIMA] = m->io[R_TMA] = 0;
    m->io[R_TAC] = 0;
    m->io[R_IF] = 0x01;
    m->io[R_LCDC] = 0x91;
    m->io[R_STAT] = 0x80;
    m->io[R_SCY] = m->io[R_SCX] = m->io[R_LYC] = 0;
    m->io[R_LY] = 0;
    m->io[R_DMA] = model == MODEL_DMG ? 0xFF : 0x00;
    m->io[R_BGP] = 0xFC;
    m->io[R_OBP0] = m->io[R_OBP1] = model == MODEL_DMG ? 0xFF : 0x00;
    m->io[R_WY] = m->io[R_WX] = 0;
    m->dma_pos = 254;
    m->dma_next = NEVER;
    m->dma_src = DMA_OFF;
    m->dma_start = 0;
    for (int i = 0; i < 5; i++)
        m->ack_block[i] = -1;
    m->min_int_time = 0;
    m->cart_bus = 0xFF;
    m->cart_bus_time = -1000;
    m->enable_cycle = -1000;

    int line, ly;
    if (model == MODEL_DMG) {
        m->div_counter = 0xABCC;
        ly = 153;
        line = 396;
        m->apu.fs_step = 2;
    } else {
        m->div_counter = model == MODEL_AGB ? 0x1EA4 : 0x1EA0;
        ly = 144;
        line = model == MODEL_AGB ? 168 : 164;
        m->apu.fs_step = 1;
    }
    /* LCD on, somewhere in vblank */
    PpuModel *P = &m->ppu;
    m->lcd_on = 1;
    lcd_begin_frame(&m->lcd);
    P->lcdc = 0x91;
    P->ly = ly;
    P->ly_time = LINE - line;
    P->line_valid = 1;
    P->plot_x = -1;
    P->state = PS_M2_LY0;
    P->at = (int64_t)(FRAME - ly * LINE - line) + 1 + is_cgb(m);
    m->ly = ly;
    m->stat_reg = 0;
    m->li_stat = m->li_stat_src = 0;
    m->li_lyc = m->li_lyc_src = 0;
    m->ms_stat = m->ms_lyc = 0;
    m->ev_m1 = mode1_schedule(m, 0);
    m->ev_m2 = NEVER;
    m->ev_m0 = NEVER;
    m->ev_lyc = NEVER;
    m->ev_stat1 = m->ev_wy2 = NEVER;
    m->m0_pred = 0;

    /* Sound registers after boot: the boot ROM played its chime on square 1 */
    m->apu.power = 1;
    m->apu.regs[0xFF11 - 0xFF10] = 0xBF;
    m->apu.regs[0xFF12 - 0xFF10] = 0xF3;
    m->apu.regs[0xFF24 - 0xFF10] = 0x77;
    m->apu.regs[0xFF25 - 0xFF10] = 0xF3;
    m->apu.regs[0xFF26 - 0xFF10] = 0x80;
    m->apu.ch[0].dac_on = 1;
}
