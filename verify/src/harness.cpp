/* ============================================================================
 *  verify/src/harness.cpp
 *
 *  Host-side, hardware-free verification of the LCD1602A driver.
 *
 *  The REAL firmware sources are compiled unchanged (as C++) together with
 *  this file:
 *      verify/fake/stm32f10x*.h   shadows the device headers; every peripheral
 *                                 register is a proxy that records the access.
 *      Delay.c                    the real SysTick-based delay, driven by a
 *                                 virtual microsecond clock.
 *      MyPCF8574.c                the real software I2C / PCF8574 driver.
 *      MyLCD1602A.c               the real LCD driver.
 *
 *  Model chain:  GPIOB PB6/PB7 writes -> open-drain I2C bus -> PCF8574
 *  (quasi-bidirectional port, power-on latch 0xFF, read = latch AND driven
 *  lines) -> HD44780 (8/4-bit, DL bit, E falling-edge latch, RS/RW, full
 *  instruction decode, DDRAM/CGRAM, AC, display flags, per-instruction
 *  execution time / BUSY).
 *
 *  Exit code 0 = all "fixed driver" checks passed, 1 = failed / expected fail.
 *
 *  Build/run: see verify/run.ps1
 * ========================================================================== */
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include <algorithm>

#include "stm32f10x.h"
#include "MyLCD1602A.h"

typedef unsigned long ulong;

/* ============================== configuration ============================= */
static uint8_t     g_undef_nibble      = 0x00;  /* DB3..DB0 in 8-bit mode   */
static uint64_t    g_power_on_busy_us  = 0;     /* HD44780 power-on BUSY    */
static std::string g_label             = "run";
static bool        g_latch_verbose     = true;

/* ============================== virtual clock ============================= */
static uint64_t g_now = 0;

/* open-drain bus lines: 1 = high/released, 0 = driven low */
static int g_m_sda      = 1;
static int g_m_scl      = 1;
static int g_slave_pull = 0;   /* PCF8574 pulls SDA low (ACK or slave data)   */

static int line_sda(void) { return (g_m_sda == 0 || g_slave_pull) ? 0 : 1; }
static int line_scl(void) { return g_m_scl ? 1 : 0; }

/* ============================== logging ================================== */
static void ev(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsprintf(buf, fmt, ap);
    va_end(ap);
    printf("[%9lu us] %s\n", (ulong)g_now, buf);
    fflush(stdout);
}

struct Violation { uint64_t t; std::string who; std::string what; };
static std::vector<Violation> g_viol;

static void violf(const char *who, const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsprintf(buf, fmt, ap);
    va_end(ap);
    Violation v;
    v.t = g_now; v.who = who; v.what = buf;
    g_viol.push_back(v);
    printf("[%9lu us] ** VIOLATION [%s] %s\n", (ulong)g_now, who, buf);
    fflush(stdout);
}

static std::string fmt(const char *f, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, f);
    vsprintf(buf, f, ap);
    va_end(ap);
    return std::string(buf);
}

/* ======================= HD44780 (LCD1602A) emulator ====================== */
struct LatchRec
{
    uint64_t    t;
    int         mode4;        /* mode before this latch */
    int         rs, rw;
    int         nibble;       /* D7..D4 on the PCF8574 pins */
    int         byte;         /* decoded 8-bit value */
    bool        undeclared;   /* low nibble undefined (8-bit mode) */
    bool        clean;        /* RS/D7..D4 were stable while E was high */
    bool        accepted;     /* executed (not ignored because of BUSY) */
    bool        busy_at_latch;
    int         kind;         /* 0 = 8-bit latch, 1 = 4-bit high, 2 = 4-bit low */
    std::string name;
    uint64_t    e_width_us;
};

struct InstrRec
{
    uint64_t    t;
    int         code;
    int         rs;
    std::string name;
};

struct Snapshot
{
    bool four_bit, two_line, display_on;
    int  ac;
    std::string row1, row2;
    std::string row1_hex;
};

struct Lcd
{
    /* mode / state */
    bool four_bit   = false;
    bool two_line   = false;
    bool font_5x10  = false;
    bool disp_on    = false;
    bool cursor_on  = false;
    bool blink_on   = false;
    bool entry_inc  = true;
    bool entry_shift= false;
    int  ac         = 0;
    bool ac_cgram   = false;

    uint8_t ddram[128];
    uint8_t cgram[64];

    uint64_t busy_until_us = 0;

    /* pin state as seen at the PCF8574 port */
    int e_prev = 0, rs_prev = 0, rw_prev = 0, nib_prev = 0;
    uint64_t e_rise_us = 0;
    bool e_rise_valid = false;

    /* 4-bit nibble pairing */
    int nibble_phase = 0;   /* 0 = expect high nibble, 1 = expect low */
    int hi_nib = 0;

    /* records + stats */
    std::vector<LatchRec> latches;
    std::vector<InstrRec> instrs;
    int      e_pulses = 0;
    uint64_t e_min_us = (uint64_t)-1;
    uint64_t e_max_us = 0;
    int      unclear_edges = 0;
    int      ignored_busy = 0;

    void power_on(void)
    {
        memset(ddram, 0x20, sizeof(ddram));
        memset(cgram, 0x00, sizeof(cgram));
        busy_until_us = g_power_on_busy_us;
        ev("HD44780: power-on state = 8-bit bus, display OFF, DDRAM filled with 0x20 (space)");
    }

    bool busy_now(void) const { return g_now < busy_until_us; }

    /* Value that the LCD drives on DB7..DB4 while RW=1. */
    uint8_t data_out_nibble(bool e, int rs) const
    {
        if (rs)
        {
            uint8_t d = ac_cgram ? cgram[ac & 0x3F] : ddram[ac & 0x7F];
            return (uint8_t)(e ? ((d >> 4) & 0x0F) : (d & 0x0F));
        }
        uint8_t busy = busy_now() ? 1u : 0u;
        int a = ac & 0x7F;
        if (e)
        {
            return (uint8_t)((busy << 3) | ((a >> 4) & 0x07));
        }
        return (uint8_t)(a & 0x0F);
    }

    void on_port_change(uint8_t oldv, uint8_t newv)
    {
        (void)oldv;
        int rs  = newv & 0x01;
        int rw  = (newv >> 1) & 0x01;
        int e   = (newv >> 2) & 0x01;
        int nib = (newv >> 4) & 0x0F;

        if (e_prev == 0 && e == 1)
        {
            e_rise_us = g_now;
            e_rise_valid = true;
        }
        if (e_prev == 1 && e == 0)
        {
            uint64_t width = e_rise_valid ? (g_now - e_rise_us) : 0;
            e_pulses++;
            if (width < e_min_us) { e_min_us = width; }
            if (width > e_max_us) { e_max_us = width; }

            bool clean = (nib == nib_prev) && (rs == rs_prev) && (rw == rw_prev);

            if (rw)
            {
                ev("HD44780: E falling while RW=1 -> read phase, no write latch");
            }
            else
            {
                if (!clean)
                {
                    unclear_edges++;
                    violf("HD44780",
                          "E falling edge with simultaneous change of RS/D7-D4 "
                          "(was RS=%d D=0x%X, now RS=%d D=0x%X) - data not stable "
                          "before the latch edge (byte may be garbage)",
                          rs_prev, nib_prev, rs, nib);
                }
                latch_nibble(nib, rs, clean, width);
            }
        }
        e_prev = e; rs_prev = rs; rw_prev = rw; nib_prev = nib;
    }

    void latch_nibble(int nib, int rs, bool clean, uint64_t width)
    {
        if (!four_bit)
        {
            uint8_t byte = (uint8_t)((nib << 4) | g_undef_nibble);
            execute(byte, rs, clean, width, true, 0, nib);
            return;
        }

        if (nibble_phase == 0)
        {
            hi_nib = nib;
            nibble_phase = 1;
            LatchRec r;
            r.t = g_now; r.mode4 = 1; r.rs = rs; r.rw = 0; r.nibble = nib;
            r.byte = (nib << 4); r.undeclared = false; r.clean = clean;
            r.accepted = false; r.busy_at_latch = busy_now(); r.kind = 1;
            r.name = "4-bit HIGH nibble (waiting for LOW nibble)";
            r.e_width_us = width;
            latches.push_back(r);
            if (g_latch_verbose)
            {
                ev("HD44780: [4-bit] latch HIGH nibble 0x%X  (E width %lu us)",
                   nib, (ulong)width);
            }
            return;
        }

        uint8_t byte = (uint8_t)((hi_nib << 4) | nib);
        nibble_phase = 0;
        execute(byte, rs, clean, width, false, 2, nib);
    }

    void execute(uint8_t byte, int rs, bool clean, uint64_t width,
                 bool undefined_low, int kind, int nibble)
    {
        LatchRec r;
        r.t = g_now; r.mode4 = four_bit ? 1 : 0; r.rs = rs; r.rw = 0;
        r.nibble = nibble; r.byte = (int)byte; r.undeclared = undefined_low;
        r.clean = clean; r.busy_at_latch = busy_now(); r.kind = kind;
        r.e_width_us = width;

        bool busy = r.busy_at_latch;

        if (busy)
        {
            r.accepted = false;
            r.name = fmt("IGNORED because LCD is BUSY (0x%02X)", byte);
            ignored_busy++;
            violf("HD44780",
                  "instruction/data 0x%02X latched while BUSY (busy for another "
                  "%ld us) -> the HD44780 ignores it",
                  byte, (long)(busy_until_us - g_now));
            latches.push_back(r);
            return;
        }

        r.accepted = true;
        uint64_t dur = 37;
        std::string name;

        if (rs)
        {
            int old_ac = ac;
            if (ac_cgram)
            {
                cgram[ac & 0x3F] = byte;
                name = fmt("DATA 0x%02X -> CGRAM[0x%02X]", byte, ac & 0x3F);
            }
            else
            {
                ddram[ac & 0x7F] = byte;
                name = fmt("DATA 0x%02X -> DDRAM[0x%02X]", byte, ac & 0x7F);
            }
            int step = entry_inc ? 1 : -1;
            ac = (ac + step) & 0x7F;
            if (entry_shift)
            {
                /* display shift is not modelled in the DDRAM image */
            }
            (void)old_ac;
            dur = 37;
        }
        else if (byte == 0x01)
        {
            memset(ddram, 0x20, sizeof(ddram));
            ac = 0; ac_cgram = false;
            name = "CLEAR DISPLAY (0x01)";
            dur = 1520;
        }
        else if (byte == 0x02)
        {
            ac = 0; ac_cgram = false;
            name = "RETURN HOME (0x02)";
            dur = 1520;
        }
        else if ((byte & 0xFC) == 0x04)
        {
            entry_inc   = ((byte & 0x02) != 0);      /* I/D */
            entry_shift = ((byte & 0x01) != 0);      /* S   */
            name = fmt("ENTRY MODE SET (0x%02X) I/D=%d S=%d",
                       byte, entry_inc ? 1 : 0, entry_shift ? 1 : 0);
            dur = 37;
        }
        else if ((byte & 0xF8) == 0x08)
        {
            disp_on   = ((byte & 0x04) != 0);        /* D */
            cursor_on = ((byte & 0x02) != 0);        /* C */
            blink_on  = ((byte & 0x01) != 0);        /* B */
            name = fmt("DISPLAY ON/OFF (0x%02X) D=%d C=%d B=%d",
                       byte, disp_on ? 1 : 0, cursor_on ? 1 : 0, blink_on ? 1 : 0);
            dur = 37;
        }
        else if ((byte & 0xF0) == 0x10)
        {
            name = fmt("CURSOR/DISPLAY SHIFT (0x%02X)", byte);
            dur = 37;
        }
        else if ((byte & 0xE0) == 0x20)
        {
            int dl = (byte >> 4) & 1;
            int n  = (byte >> 3) & 1;
            int f  = (byte >> 2) & 1;
            four_bit  = (dl == 0);
            two_line  = (n == 1);
            font_5x10 = (f == 1);
            nibble_phase = 0;
            name = fmt("FUNCTION SET (0x%02X) DL=%d(%s) N=%d(%d-line) F=%d",
                       byte, dl, four_bit ? "4-bit" : "8-bit", n,
                       two_line ? 2 : 1, f);
            dur = 37;
        }
        else if ((byte & 0x80) == 0x80)
        {
            ac = byte & 0x7F;
            ac_cgram = false;
            name = fmt("SET DDRAM ADDRESS (0x%02X) AC=0x%02X", byte, ac);
            dur = 37;
        }
        else if ((byte & 0xC0) == 0x40)
        {
            ac = byte & 0x3F;
            ac_cgram = true;
            name = fmt("SET CGRAM ADDRESS (0x%02X) AC=0x%02X", byte, ac);
            dur = 37;
        }
        else
        {
            name = fmt("NOP/unused (0x%02X)", byte);
            dur = 37;
        }

        busy_until_us = g_now + dur;
        r.name = name;
        latches.push_back(r);
        instrs.push_back({g_now, byte, rs, name});

        if (g_latch_verbose)
        {
            ev("HD44780: latch 0x%02X RS=%d mode=%s%s -> %s  (E width %lu us)",
               byte, rs, four_bit ? "4-bit" : "8-bit",
               undefined_low ? " [D3-D0 UNDEFINED]" : "", name.c_str(),
               (ulong)width);
        }
    }

    std::string row_string(int base) const
    {
        std::string s;
        for (int i = 0; i < 16; i++)
        {
            uint8_t c = ddram[(base + i) & 0x7F];
            s.push_back((c >= 0x20 && c <= 0x7E) ? (char)c : '.');
        }
        return s;
    }

    std::string row_hex(int base) const
    {
        std::string s;
        for (int i = 0; i < 16; i++)
        {
            uint8_t c = ddram[(base + i) & 0x7F];
            char b[8];
            sprintf(b, "%02X ", c);
            s += b;
        }
        return s;
    }

    std::string trim_right(std::string s) const
    {
        while (!s.empty() && (s[s.size() - 1] == ' ' || s[s.size() - 1] == '.'))
        {
            s.erase(s.size() - 1);
        }
        return s;
    }

    Snapshot snapshot(void) const
    {
        Snapshot s;
        s.four_bit = four_bit;
        s.two_line = two_line;
        s.display_on = disp_on;
        s.ac = ac;
        s.row1 = row_string(0x00);
        s.row2 = row_string(0x40);
        s.row1_hex = row_hex(0x00);
        return s;
    }
};

static Lcd g_lcd;

/* ========================== PCF8574 emulator ============================== */
struct Pcf
{
    uint8_t latch = 0xFF;   /* PCF8574 power-on: all outputs high */

    uint8_t ext_value(void) const
    {
        int rs = latch & 0x01;
        int rw = (latch >> 1) & 0x01;
        int e  = (latch >> 2) & 0x01;
        if (!rw)
        {
            /* LCD data pins are inputs (high-Z) -> pulled high by the board */
            return 0xFF;
        }
        uint8_t nib = g_lcd.data_out_nibble(e != 0, rs != 0);
        /* D3..D0 of the LCD are not connected on the 4-bit backpack */
        return (uint8_t)((nib << 4) | 0x0F);
    }

    uint8_t read_port(void) const { return (uint8_t)(latch & ext_value()); }

    void write_port(uint8_t v)
    {
        uint8_t old = latch;
        latch = v;
        ev("PCF8574 <= 0x%02X   (P7..P4=D7..D4=0x%X  P3=BL=%d  P2=E=%d  P1=RW=%d  P0=RS=%d)",
           v, (v >> 4) & 0x0F, (v >> 3) & 1, (v >> 2) & 1, (v >> 1) & 1, v & 1);
        g_lcd.on_port_change(old, v);
    }
};

static Pcf g_pcf;

/* ===================== I2C slave (PCF8574) state machine ================= */
enum Phase
{
    PH_IDLE = 0,
    PH_ADDR,
    PH_ADDR_ACK_WAIT,
    PH_ADDR_ACK_HIGH,
    PH_WR,
    PH_WR_ACK_WAIT,
    PH_WR_ACK_HIGH,
    PH_RD,
    PH_RD_ACK_WAIT,
    PH_RD_ACK_HIGH,
    PH_WAIT_STOP
};

struct Slave
{
    bool    active = false;
    Phase   ph = PH_IDLE;
    int     bitcount = 0;
    uint8_t sh = 0;
    uint8_t addr = 0;
    bool    rw = false;
    bool    matched = false;
    bool    master_ack = false;
    uint8_t rd_byte = 0;

    int txn = 0, starts = 0, stops = 0, restarts = 0;
    int bytes_write = 0, bytes_read = 0;
    int acks = 0, nacks = 0, addr_nacks = 0;
    int read_txns = 0;      /* matched READ address (0x4F) transactions */

    bool addressed_to_me(void) const { return matched; }

    void do_start(void)
    {
        if (!active)
        {
            txn++;
            starts++;
            ev("I2C START (transaction %d)", txn);
        }
        else
        {
            restarts++;
            if (ph == PH_ADDR_ACK_WAIT || ph == PH_WR_ACK_WAIT)
            {
                violf("I2C",
                      "repeated START before the ACK clock: 8 data bits (0x%02X) "
                      "were clocked where a single ACK/NACK bit belongs -> the "
                      "extra clocks are interpreted as data by the PCF8574",
                      sh);
            }
            else if (ph == PH_ADDR || ph == PH_WR)
            {
                if (bitcount >= 2)
                {
                    violf("I2C",
                          "repeated START in the middle of a byte (%d of 8 bits "
                          "clocked, partial value 0x%02X): missing STOP / missing "
                          "ACK, the slave sees a truncated byte",
                          bitcount, sh);
                }
            }
            else if (ph == PH_RD || ph == PH_RD_ACK_WAIT)
            {
                violf("I2C", "repeated START in the middle of a READ byte (%d of 8 bits)",
                      bitcount);
            }
            ev("I2C repeated START");
        }
        active = true;
        ph = PH_ADDR;
        bitcount = 0;
        sh = 0;
        matched = false;
        rw = false;
        g_slave_pull = 0;
    }

    void do_stop(void)
    {
        if (!active)
        {
            return;   /* STOP pattern on an already idle bus (e.g. bus recovery) */
        }
        if (ph == PH_ADDR_ACK_WAIT || ph == PH_WR_ACK_WAIT)
        {
            violf("I2C",
                  "STOP before the ACK clock: 8 data bits (0x%02X) were clocked "
                  "where the ACK/NACK bit belongs", sh);
        }
        else if ((ph == PH_ADDR || ph == PH_WR) && bitcount >= 2)
        {
            violf("I2C",
                  "STOP in the middle of a byte (%d of 8 bits, partial 0x%02X): "
                  "truncated transfer", bitcount, sh);
        }
        stops++;
        ev("I2C STOP");
        active = false;
        ph = PH_IDLE;
        bitcount = 0;
        g_slave_pull = 0;
    }

    void on_rising(void)
    {
        if (!active)
        {
            return;
        }
        int sda = line_sda();

        switch (ph)
        {
        case PH_ADDR:
            sh = (uint8_t)((sh << 1) | (sda ? 1 : 0));
            bitcount++;
            if (bitcount == 8)
            {
                ph = PH_ADDR_ACK_WAIT;
            }
            break;

        case PH_ADDR_ACK_WAIT:
            addr = sh;
            matched = ((addr >> 1) == 0x27);   /* PCF8574 at 0x27 */
            if (matched)
            {
                g_slave_pull = 1;
                acks++;
            }
            else
            {
                g_slave_pull = 0;
                addr_nacks++;
            }
            ph = PH_ADDR_ACK_HIGH;
            ev("  %s 0x%02X  %s", (addr & 1) ? "READ  addr" : "WRITE addr",
               addr, matched ? "ACK" : "NACK (no device at this address)");
            break;

        case PH_ADDR_ACK_HIGH:
            break;

        case PH_WR:
            sh = (uint8_t)((sh << 1) | (sda ? 1 : 0));
            bitcount++;
            if (bitcount == 8)
            {
                ph = PH_WR_ACK_WAIT;
            }
            break;

        case PH_WR_ACK_WAIT:
            g_slave_pull = 1;          /* slave ACKs the received byte */
            ph = PH_WR_ACK_HIGH;
            break;

        case PH_WR_ACK_HIGH:
            break;

        case PH_RD:
        {
            uint8_t v = g_pcf.read_port();
            rd_byte = (uint8_t)((rd_byte << 1) | ((v >> (7 - bitcount)) & 1));
            g_slave_pull = ((v >> (7 - bitcount)) & 1) ? 0 : 1;
            bitcount++;
            if (bitcount == 8)
            {
                ph = PH_RD_ACK_WAIT;
            }
            break;
        }

        case PH_RD_ACK_WAIT:
            g_slave_pull = 0;
            master_ack = (sda == 0);
            ph = PH_RD_ACK_HIGH;
            ev("  0x%02X  (slave -> master)  %s", rd_byte,
               master_ack ? "master ACK" : "master NACK");
            break;

        case PH_RD_ACK_HIGH:
            break;

        default:
            break;
        }
    }

    void on_falling(void)
    {
        if (!active)
        {
            return;
        }
        switch (ph)
        {
        case PH_ADDR_ACK_WAIT:
            /* falling edge of the 8th address bit: wait for the ACK clock */
            break;

        case PH_ADDR_ACK_HIGH:
            g_slave_pull = 0;
            if (matched)
            {
                bytes_write += 0;
                rw = (addr & 1) != 0;
                if (rw)
                {
                    ph = PH_RD;
                    bitcount = 0;
                    rd_byte = 0;
                    read_txns++;
                    if ((g_pcf.latch & 0x02) == 0)
                    {
                        violf("I2C",
                              "read transaction while PCF8574 latch has RW(P1)=0: "
                              "the LCD does not drive the data bus (read would return "
                              "the latch itself)");
                    }
                    if ((g_pcf.latch & 0xF0) != 0xF0)
                    {
                        violf("I2C",
                              "read transaction while P4..P7 are not released "
                              "(latch=0x%02X): the PCF8574 drives those pins low, "
                              "the LCD cannot drive the busy flag",
                              g_pcf.latch);
                    }
                }
                else
                {
                    ph = PH_WR;
                    bitcount = 0;
                }
            }
            else
            {
                ph = PH_WAIT_STOP;
            }
            break;

        case PH_WR_ACK_WAIT:
            /* falling edge of the 8th data bit: wait for the ACK clock */
            break;

        case PH_WR_ACK_HIGH:
            g_slave_pull = 0;
            bytes_write++;
            acks++;
            ev("  0x%02X  (master -> slave)  ACK", sh);
            if (sh == 0xFF)
            {
                ev("     note: 0xFF means SDA was never driven low by anyone; "
                   "in a write transaction the PCF8574 does not output data");
                if (g_pcf.latch & 0x02)
                {
                    violf("I2C",
                          "busy-flag read attempted with the WRITE address: the "
                          "PCF8574 latch has RW(P1)=1 and the master clocked 8 "
                          "bits of 0xFF - a write transaction never drives SDA, "
                          "so the read returns 0xFF and the busy flag is lost "
                          "(a real read needs the READ address 0x4F and P4..P7 "
                          "released to 1)");
                }
            }
            g_pcf.write_port(sh);
            ph = PH_WR;
            bitcount = 0;
            sh = 0;
            break;

        case PH_RD_ACK_WAIT:
            g_slave_pull = 0;
            break;

        case PH_RD_ACK_HIGH:
            bytes_read++;
            if (master_ack)
            {
                ph = PH_RD;
                bitcount = 0;
                rd_byte = 0;
            }
            else
            {
                ph = PH_WAIT_STOP;
            }
            break;

        default:
            break;
        }
    }
};

static Slave g_slave;

/* ==================== master-side GPIO / bus plumbing ==================== */
static void master_set_sda(int level)
{
    if (level == g_m_sda)
    {
        return;
    }
    g_m_sda = level;
    if (line_scl() == 1)
    {
        if (level == 0)
        {
            g_slave.do_start();
        }
        else
        {
            g_slave.do_stop();
        }
    }
}

static void master_set_scl(int level)
{
    if (level == g_m_scl)
    {
        return;
    }
    g_m_scl = level;
    if (level == 1)
    {
        g_slave.on_rising();
    }
    else
    {
        g_slave.on_falling();
    }
}

/* ========================= SysTick model ================================= */
static uint64_t g_syst_load  = 0;
static uint64_t g_delay_end  = 0;
static bool     g_syst_on    = false;

static void syst_ctrl_write(uint32_t v)
{
    if (v & 0x0001)
    {
        uint64_t us = g_syst_load / 72;   /* Delay.c: LOAD = 72 * xus @72MHz */
        g_delay_end = g_now + us;
        g_syst_on = true;
        if (us >= 1000)
        {
            ev("DELAY %lu us (%.2f ms) requested", (ulong)us, (double)us / 1000.0);
        }
    }
    else
    {
        g_syst_on = false;
    }
}

static uint32_t syst_ctrl_read(void)
{
    if (g_syst_on)
    {
        if (g_now < g_delay_end)
        {
            g_now = g_delay_end;          /* jump to the end of the delay */
        }
        return 0x00010005u;               /* COUNTFLAG | CLKSOURCE | ENABLE */
    }
    return 0x00000005u;
}

/* ==================== register proxy implementation ====================== */
RegProxy::RegProxy() : kind_(0), port_(0) {}
void RegProxy::bind(int kind, int port) { kind_ = kind; port_ = port; }

static void gpio_bsrr(int port, uint32_t v)
{
    if (port != 1) { return; }                     /* only GPIOB is wired */
    uint32_t lo = v & 0x0000FFFFu;
    uint32_t hi = (v >> 16) & 0x0000FFFFu;
    if (lo & 0x0040u) { master_set_scl(1); }
    if (lo & 0x0080u) { master_set_sda(1); }
    if (hi & 0x0040u) { master_set_scl(0); }
    if (hi & 0x0080u) { master_set_sda(0); }
}

static void gpio_brr(int port, uint32_t v)
{
    if (port != 1) { return; }
    if (v & 0x0040u) { master_set_scl(0); }
    if (v & 0x0080u) { master_set_sda(0); }
}

static void gpio_odr(int port, uint32_t v)
{
    if (port != 1) { return; }
    master_set_scl((v & 0x0040u) ? 1 : 0);
    master_set_sda((v & 0x0080u) ? 1 : 0);
}

static void gpio_config(int port, uint32_t v, int high)
{
    if (port != 1) { return; }
    ev("GPIOB %s <= 0x%08lX (pin configuration)", high ? "CRH" : "CRL", (ulong)v);
}

uint32_t RegProxy::operator=(uint32_t v)
{
    switch (kind_)
    {
    case K_GPIO_BSRR: gpio_bsrr(port_, v); break;
    case K_GPIO_BRR:  gpio_brr(port_, v);  break;
    case K_GPIO_ODR:  gpio_odr(port_, v);  break;
    case K_GPIO_CRL:  gpio_config(port_, v, 0); break;
    case K_GPIO_CRH:  gpio_config(port_, v, 1); break;
    case K_GPIO_IDR:  break;
    case K_RCC_APB2ENR:
        ev("RCC APB2ENR <= 0x%08lX (peripheral clock enable)", (ulong)v);
        break;
    case K_RCC_APB1ENR:
        ev("RCC APB1ENR <= 0x%08lX", (ulong)v);
        break;
    case K_RCC_AHBENR:
        ev("RCC AHBENR <= 0x%08lX", (ulong)v);
        break;
    case K_SYST_LOAD: g_syst_load = v; break;
    case K_SYST_VAL:  break;
    case K_SYST_CTRL: syst_ctrl_write(v); break;
    default: break;
    }
    return v;
}

uint32_t RegProxy::operator|=(uint32_t v)
{
    uint32_t cur = 0;
    switch (kind_)
    {
    case K_GPIO_IDR:  cur = (uint32_t)((line_sda() ? 1u : 0u) << 7) |
                              (uint32_t)((line_scl() ? 1u : 0u) << 6); break;
    case K_GPIO_ODR:  cur = 0; break;
    case K_SYST_CTRL: cur = syst_ctrl_read(); break;
    default: cur = 0; break;
    }
    return (*this = (cur | v));
}

uint32_t RegProxy::operator&=(uint32_t v)
{
    return (*this = (value() & v));
}

uint32_t RegProxy::operator&(uint32_t mask) const
{
    return (uint32_t)(value() & mask);
}

uint32_t RegProxy::operator&(int mask) const
{
    return (uint32_t)(value() & (uint32_t)mask);
}

RegProxy::operator uint32_t() const
{
    return value();
}

uint32_t RegProxy::value() const
{
    switch (kind_)
    {
    case K_GPIO_IDR:
        return (uint32_t)((line_sda() ? 1u : 0u) << 7) |
               (uint32_t)((line_scl() ? 1u : 0u) << 6);
    case K_GPIO_ODR:
        return (uint32_t)((g_m_scl ? 1u : 0u) << 6) |
               (uint32_t)((g_m_sda ? 1u : 0u) << 7);
    case K_GPIO_BSRR:
    case K_GPIO_BRR:
    case K_GPIO_CRL:
    case K_GPIO_CRH:
    case K_RCC_APB2ENR:
    case K_RCC_APB1ENR:
    case K_RCC_AHBENR:
        return 0;
    case K_SYST_CTRL: return syst_ctrl_read();
    case K_SYST_LOAD: return (uint32_t)g_syst_load;
    case K_SYST_VAL:  return 0;
    case K_SYST_CALIB: return 0;
    default: return 0;
    }
}

/* ======================= fake peripheral objects ========================= */
GPIO_TypeDef::GPIO_TypeDef()
{
    /* port id is patched by the global definitions below */
}

RCC_TypeDef::RCC_TypeDef()
{
    CR.bind(RegProxy::K_GPIO_CRL, 9);          /* unused placeholders */
    CFGR.bind(RegProxy::K_GPIO_CRL, 9);
    APB2ENR.bind(RegProxy::K_RCC_APB2ENR, 9);
    APB1ENR.bind(RegProxy::K_RCC_APB1ENR, 9);
    AHBENR.bind(RegProxy::K_RCC_AHBENR, 9);
}

SysTick_TypeDef::SysTick_TypeDef()
{
    CTRL.bind(RegProxy::K_SYST_CTRL, 9);
    LOAD.bind(RegProxy::K_SYST_LOAD, 9);
    VAL.bind(RegProxy::K_SYST_VAL, 9);
    CALIB.bind(RegProxy::K_SYST_CALIB, 9);
}

/* GPIO_TypeDef members must be bound to a port id.  The constructors cannot
 * know the id, so bind explicitly after the globals are constructed. */
static void bind_gpio(GPIO_TypeDef &g, int port)
{
    g.CRL.bind(RegProxy::K_GPIO_CRL, port);
    g.CRH.bind(RegProxy::K_GPIO_CRH, port);
    g.IDR.bind(RegProxy::K_GPIO_IDR, port);
    g.ODR.bind(RegProxy::K_GPIO_ODR, port);
    g.BSRR.bind(RegProxy::K_GPIO_BSRR, port);
    g.BRR.bind(RegProxy::K_GPIO_BRR, port);
}

GPIO_TypeDef g_GPIOA;
GPIO_TypeDef g_GPIOB;
GPIO_TypeDef g_GPIOC;
RCC_TypeDef  g_RCC;
SysTick_TypeDef g_SysTick;

struct GpioBinder
{
    GpioBinder()
    {
        bind_gpio(g_GPIOA, 0);
        bind_gpio(g_GPIOB, 1);
        bind_gpio(g_GPIOC, 2);
    }
};
static GpioBinder g_gpio_binder;

/* ========================= StdPeriph API stubs =========================== */
void RCC_APB2PeriphClockCmd(uint32_t periph, FunctionalState state)
{
    ev("RCC_APB2PeriphClockCmd(0x%08lX, %s)", (ulong)periph,
       state == ENABLE ? "ENABLE" : "DISABLE");
}
void RCC_APB1PeriphClockCmd(uint32_t periph, FunctionalState state)
{
    ev("RCC_APB1PeriphClockCmd(0x%08lX, %s)", (ulong)periph,
       state == ENABLE ? "ENABLE" : "DISABLE");
}
void RCC_AHBPeriphClockCmd(uint32_t periph, FunctionalState state)
{
    ev("RCC_AHBPeriphClockCmd(0x%08lX, %s)", (ulong)periph,
       state == ENABLE ? "ENABLE" : "DISABLE");
}

void GPIO_Init(GPIO_TypeDef *port, GPIO_InitTypeDef *init)
{
    (void)port;
    ev("GPIO_Init(pins=0x%04X, mode=0x%02X, speed=%d)",
       init->GPIO_Pin, (unsigned)init->GPIO_Mode, (int)init->GPIO_Speed);
}

void GPIO_SetBits(GPIO_TypeDef *port, uint16_t pins)
{
    (void)port;
    if (pins & GPIO_Pin_6) { master_set_scl(1); }
    if (pins & GPIO_Pin_7) { master_set_sda(1); }
}
void GPIO_ResetBits(GPIO_TypeDef *port, uint16_t pins)
{
    (void)port;
    if (pins & GPIO_Pin_6) { master_set_scl(0); }
    if (pins & GPIO_Pin_7) { master_set_sda(0); }
}
void GPIO_WriteBit(GPIO_TypeDef *port, uint16_t pin, BitAction value)
{
    (void)port;
    if (pin & GPIO_Pin_6) { master_set_scl(value != Bit_RESET); }
    if (pin & GPIO_Pin_7) { master_set_sda(value != Bit_RESET); }
}
uint8_t GPIO_ReadInputDataBit(GPIO_TypeDef *port, uint16_t pin)
{
    (void)port;
    if (pin & GPIO_Pin_7) { return (uint8_t)(line_sda() ? 1 : 0); }
    if (pin & GPIO_Pin_6) { return (uint8_t)(line_scl() ? 1 : 0); }
    return 0;
}
uint16_t GPIO_ReadInputData(GPIO_TypeDef *port)
{
    (void)port;
    return (uint16_t)(((line_sda() ? 1u : 0u) << 7) |
                      ((line_scl() ? 1u : 0u) << 6));
}
uint16_t GPIO_ReadOutputData(GPIO_TypeDef *port)
{
    (void)port;
    return (uint16_t)(((g_m_sda ? 1u : 0u) << 7) |
                      ((g_m_scl ? 1u : 0u) << 6));
}
void GPIO_Write(GPIO_TypeDef *port, uint16_t value)
{
    (void)port;
    master_set_scl((value & GPIO_Pin_6) ? 1 : 0);
    master_set_sda((value & GPIO_Pin_7) ? 1 : 0);
}

/* ============================ report helpers ============================= */
static bool has_instr(int code, int rs)
{
    for (size_t i = 0; i < g_lcd.instrs.size(); i++)
    {
        if (g_lcd.instrs[i].code == code && g_lcd.instrs[i].rs == rs)
        {
            return true;
        }
    }
    return false;
}

static int first_instr_index(int code, int rs)
{
    for (size_t i = 0; i < g_lcd.instrs.size(); i++)
    {
        if (g_lcd.instrs[i].code == code && g_lcd.instrs[i].rs == rs)
        {
            return (int)i;
        }
    }
    return -1;
}

static void print_snapshot(const char *tag, const Snapshot &s)
{
    printf("SNAP %-11s mode=%s  lines=%d  display_on=%d  AC=0x%02X\n",
           tag, s.four_bit ? "4-bit" : "8-bit", s.two_line ? 2 : 1,
           s.display_on ? 1 : 0, s.ac);
    printf("SNAP %-11s row1=\"%s\"\n", tag, s.row1.c_str());
    printf("SNAP %-11s row2=\"%s\"\n", tag, s.row2.c_str());
    printf("SNAP %-11s row1_hex=%s\n", tag, s.row1_hex.c_str());
}

static void print_latch_table(void)
{
    printf("\n--- LCD latch / instruction table (%d latches, %d instructions executed) ---\n",
           (int)g_lcd.latches.size(), (int)g_lcd.instrs.size());
    printf("%12s | %-6s | %-2s | %-2s | %-4s | %-46s | %s\n",
           "time(us)", "mode", "RS", "Ew", "byte", "decoded", "note");
    for (size_t i = 0; i < g_lcd.latches.size(); i++)
    {
        const LatchRec &r = g_lcd.latches[i];
        const char *note = "";
        if (r.kind == 1)      { note = "half byte (waiting low nibble)"; }
        else if (!r.clean)    { note = "AMBIGUOUS edge (data changed with E)"; }
        if (!r.accepted && r.busy_at_latch) { note = "IGNORED (busy)"; }
        printf("%12lu | %-6s | %-2d | %-2lu | 0x%02X | %-46.46s | %s\n",
               (ulong)r.t, r.mode4 ? "4-bit" : "8-bit", r.rs,
               (ulong)r.e_width_us, (unsigned)r.byte,
               r.kind == 1 ? "(high nibble)" : r.name.c_str(), note);
    }

    printf("\n--- accepted instructions (in execution order) ---\n");
    for (size_t i = 0; i < g_lcd.instrs.size(); i++)
    {
        const InstrRec &r = g_lcd.instrs[i];
        printf("CMD %12lu us  0x%02X  RS=%d  %s\n",
               (ulong)r.t, (unsigned)r.code, r.rs, r.name.c_str());
    }
}

static uint64_t clear_wait_us(bool *found)
{
    *found = false;
    for (size_t i = 0; i < g_lcd.instrs.size(); i++)
    {
        if (g_lcd.instrs[i].code == 0x01 && g_lcd.instrs[i].rs == 0)
        {
            uint64_t t = g_lcd.instrs[i].t;
            uint64_t next = 0;
            bool have_next = false;
            for (size_t j = 0; j < g_lcd.instrs.size(); j++)
            {
                if (g_lcd.instrs[j].t > t)
                {
                    next = g_lcd.instrs[j].t;
                    have_next = true;
                    break;
                }
            }
            *found = true;
            return have_next ? (next - t) : (g_now - t);
        }
    }
    return 0;
}

/* ================================ scenario =============================== */
static int run_all(void)
{
    printf("=============================================================================\n");
    printf("  LCD1602A / PCF8574 host simulation - label: %s\n", g_label.c_str());
    printf("  undefined DB3..DB0 in 8-bit mode : 0x%02X\n", g_undef_nibble);
    printf("  HD44780 power-on BUSY             : %lu us\n", (ulong)g_power_on_busy_us);
    printf("=============================================================================\n");

    g_lcd.power_on();

    /* Replicates User/main.c (baseline):  LCD1602A_Init();
     *                                     ShowLCDChars(1,"Password:");
     * The string is a 17-byte char array exactly like main.c's. */
    char line[17];
    memset(line, 0, sizeof(line));
    strcpy(line, "Password:");

    LCD1602A_Init();
    Snapshot after_init = g_lcd.snapshot();
    ev("=== LCD1602A_Init() returned at t=%lu us ===", (ulong)g_now);
    print_snapshot("AFTER_INIT", after_init);

    ShowLCDChars(1, line);
    Snapshot final_state = g_lcd.snapshot();
    ev("=== ShowLCDChars(1,\"Password:\") returned at t=%lu us ===", (ulong)g_now);
    print_snapshot("FINAL", final_state);

    /* ---------------- derived checks ---------------- */
    bool clear_found = false;
    uint64_t cwait = clear_wait_us(&clear_found);
    bool display_on_ever = false;
    for (size_t i = 0; i < g_lcd.instrs.size(); i++)
    {
        const InstrRec &r = g_lcd.instrs[i];
        if (r.rs == 0 && (r.code & 0xF8) == 0x08 && (r.code & 0x04))
        {
            display_on_ever = true;
        }
    }
    std::string row1_trim = g_lcd.trim_right(final_state.row1);

    int i28 = first_instr_index(0x28, 0);
    int i01 = first_instr_index(0x01, 0);
    int i06 = first_instr_index(0x06, 0);
    int i0C = first_instr_index(0x0C, 0);
    bool ok_order = (i28 >= 0) && (i01 > i28) && (i06 > i01) && (i0C > i06);

    printf("\n--- protocol violations ($ %d) ---\n", (int)g_viol.size());
    for (size_t i = 0; i < g_viol.size(); i++)
    {
        printf("V%03d %10lu us [%-8s] %s\n", (int)i + 1, (ulong)g_viol[i].t,
               g_viol[i].who.c_str(), g_viol[i].what.c_str());
    }

    print_latch_table();

    /* ---------------- machine-readable summary ---------------- */
    printf("\n=== SUMMARY ===\n");
    printf("LABEL=%s\n", g_label.c_str());
    printf("UNDEF_NIBBLE=0x%02X\n", g_undef_nibble);
    printf("POWER_ON_BUSY_US=%lu\n", (ulong)g_power_on_busy_us);
    printf("I2C_TRANSACTIONS=%d START_COUNT=%d STOP_COUNT=%d RESTARTS=%d\n",
           g_slave.txn, g_slave.starts, g_slave.stops, g_slave.restarts);
    printf("I2C_BYTES_WRITE=%d I2C_BYTES_READ=%d ACKS=%d NACKS=%d ADDR_NACKS=%d READ_TXNS_0x4F=%d\n",
           g_slave.bytes_write, g_slave.bytes_read, g_slave.acks,
           g_slave.nacks, g_slave.addr_nacks, g_slave.read_txns);
    printf("LCD_MODE_AFTER_INIT=%s\n", after_init.four_bit ? "4-bit" : "8-bit");
    printf("LCD_TWO_LINE_AFTER_INIT=%d\n", after_init.two_line ? 1 : 0);
    printf("LCD_DISPLAY_ON_AFTER_INIT=%d\n", after_init.display_on ? 1 : 0);
    printf("LCD_DISPLAY_ON_EVER=%d\n", display_on_ever ? 1 : 0);
    printf("LCD_MODE_FINAL=%s\n", final_state.four_bit ? "4-bit" : "8-bit");
    printf("LCD_TWO_LINE_FINAL=%d\n", final_state.two_line ? 1 : 0);
    printf("LCD_DISPLAY_ON_FINAL=%d\n", final_state.display_on ? 1 : 0);
    printf("HAS_INSTR_0x28=%d\n", has_instr(0x28, 0) ? 1 : 0);
    printf("HAS_INSTR_0x0C=%d\n", has_instr(0x0C, 0) ? 1 : 0);
    printf("HAS_INSTR_0x01=%d\n", has_instr(0x01, 0) ? 1 : 0);
    printf("HAS_INSTR_0x06=%d\n", has_instr(0x06, 0) ? 1 : 0);
    printf("HAS_INSTR_0x0F=%d\n", has_instr(0x0F, 0) ? 1 : 0);
    printf("CMD_ORDER_INDEX_28_01_06_0C=%d,%d,%d,%d\n", i28, i01, i06, i0C);
    printf("ACCEPTED_INSTRUCTIONS=%d IGNORED_BUSY=%d\n",
           (int)g_lcd.instrs.size(), g_lcd.ignored_busy);
    printf("E_PULSES=%d E_MIN_US=%lu E_MAX_US=%lu E_BELOW_450NS=%d\n",
           g_lcd.e_pulses,
           g_lcd.e_pulses ? (ulong)g_lcd.e_min_us : 0UL,
           (ulong)g_lcd.e_max_us,
           (g_lcd.e_pulses && g_lcd.e_min_us < 1) ? 1 : 0);
    printf("CLEAR_FOUND=%d CLEAR_WAIT_US=%lu CLEAR_WAIT_OK=%d\n",
           clear_found ? 1 : 0, (ulong)cwait,
           (clear_found && cwait >= 1520) ? 1 : 0);
    printf("DDRAM_ROW1=%s\n", final_state.row1.c_str());
    printf("DDRAM_ROW1_TRIMMED=%s\n", row1_trim.c_str());
    printf("DDRAM_ROW2=%s\n", final_state.row2.c_str());
    printf("VIOLATION_COUNT=%d\n", (int)g_viol.size());

    bool ok_mode   = final_state.four_bit && final_state.two_line;
    bool ok_cmds   = has_instr(0x28, 0) && has_instr(0x0C, 0) &&
                     has_instr(0x01, 0) && has_instr(0x06, 0);
    bool ok_disp   = final_state.display_on;
    bool ok_row1   = (row1_trim == "Password:");
    bool ok_e      = g_lcd.e_pulses > 0 && g_lcd.e_min_us >= 1;
    bool ok_clear  = clear_found && cwait >= 1520;
    bool ok_viol   = g_viol.empty();

    printf("CHECK_4BIT_2LINE=%s\n",  ok_mode  ? "PASS" : "FAIL");
    printf("CHECK_CMDS_28_0C_01_06=%s\n", ok_cmds ? "PASS" : "FAIL");
    printf("CHECK_CMD_ORDER_28_01_06_0C=%s\n", ok_order ? "PASS" : "FAIL");
    printf("CHECK_DISPLAY_ON=%s\n", ok_disp  ? "PASS" : "FAIL");
    printf("CHECK_DDRAM_ROW1_PASSWORD=%s\n", ok_row1 ? "PASS" : "FAIL");
    printf("CHECK_E_PULSE_WIDTH=%s\n", ok_e ? "PASS" : "FAIL");
    printf("CHECK_CLEAR_WAIT_1_52MS=%s\n", ok_clear ? "PASS" : "FAIL");
    printf("CHECK_NO_PROTOCOL_VIOLATIONS=%s\n", ok_viol ? "PASS" : "FAIL");

    bool pass = ok_mode && ok_cmds && ok_order && ok_disp && ok_row1 &&
                ok_e && ok_clear && ok_viol;
    printf("RESULT=%s\n", pass ? "PASS" : "FAIL");
    printf("=== END SUMMARY ===\n");
    return pass ? 0 : 1;
}

/* ================================== main ================================= */
int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++)
    {
        if ((strcmp(argv[i], "--undef") == 0) && (i + 1 < argc))
        {
            g_undef_nibble = (uint8_t)strtoul(argv[++i], NULL, 0);
        }
        else if ((strcmp(argv[i], "--power-on-busy-us") == 0) && (i + 1 < argc))
        {
            g_power_on_busy_us = (uint64_t)strtoul(argv[++i], NULL, 0);
        }
        else if ((strcmp(argv[i], "--label") == 0) && (i + 1 < argc))
        {
            g_label = argv[++i];
        }
        else if (strcmp(argv[i], "--quiet-latches") == 0)
        {
            g_latch_verbose = false;
        }
        else
        {
            fprintf(stderr, "unknown argument: %s\n", argv[i]);
            return 2;
        }
    }
    return run_all();
}
