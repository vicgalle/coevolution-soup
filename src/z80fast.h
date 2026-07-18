// z80fast.h — fast instruction-stepped Z80 emulator for the 64-byte sandbox.
//
// Replaces the cycle-accurate chips z80.h reference (floooh/chips, MIT) for
// the ALife soup simulation. Semantics (registers, flags including the
// undocumented X/Y bits, WZ/MEMPTR, R refresh counter, undocumented DD/FD/CB/ED
// behavior) are mirrored 1:1 from chips' decoder so that differential testing
// against sb_run() produces bit-identical results.
//
// Exposes:
//   int zf_run(uint8_t* mem /*64 bytes, modified in place*/, uint8_t d_init,
//              int budget, uint8_t* out_e, int* halted);
//
// Reentrant and thread-safe: all mutable state lives in a local struct; the
// only globals are const lookup tables.
#pragma once
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// flag bits (identical to chips z80.h)
#define ZF_CF (1<<0)
#define ZF_NF (1<<1)
#define ZF_VF (1<<2)
#define ZF_PF ZF_VF
#define ZF_XF (1<<3)
#define ZF_HF (1<<4)
#define ZF_YF (1<<5)
#define ZF_ZF (1<<6)
#define ZF_SF (1<<7)

// CPU state (register pair layout assumes little-endian, same as chips z80.h)
typedef struct {
    union { struct { uint8_t f; uint8_t a; }; uint16_t af; };
    union { struct { uint8_t c; uint8_t b; }; uint16_t bc; };
    union { struct { uint8_t e; uint8_t d; }; uint16_t de; };
    union {
        struct {
            union { struct { uint8_t l; uint8_t h; }; uint16_t hl; };
            union { struct { uint8_t ixl; uint8_t ixh; }; uint16_t ix; };
            union { struct { uint8_t iyl; uint8_t iyh; }; uint16_t iy; };
        };
        struct { union { struct { uint8_t l; uint8_t h; }; uint16_t hl; }; } hlx[3];
    };
    union { struct { uint8_t wzl; uint8_t wzh; }; uint16_t wz; };
    union { struct { uint8_t spl; uint8_t sph; }; uint16_t sp; };
    union { struct { uint8_t pcl; uint8_t pch; }; uint16_t pc; };
    union { struct { uint8_t r; uint8_t i; }; uint16_t ir; };
    uint16_t af2, bc2, de2, hl2;
    uint8_t im;
    uint8_t iff1, iff2;
} zf_t;

// sign+zero+parity lookup table (identical to chips _z80_szp_flags)
static const uint8_t _zf_szp_flags[256] = {
  0x44,0x00,0x00,0x04,0x00,0x04,0x04,0x00,0x08,0x0c,0x0c,0x08,0x0c,0x08,0x08,0x0c,
  0x00,0x04,0x04,0x00,0x04,0x00,0x00,0x04,0x0c,0x08,0x08,0x0c,0x08,0x0c,0x0c,0x08,
  0x20,0x24,0x24,0x20,0x24,0x20,0x20,0x24,0x2c,0x28,0x28,0x2c,0x28,0x2c,0x2c,0x28,
  0x24,0x20,0x20,0x24,0x20,0x24,0x24,0x20,0x28,0x2c,0x2c,0x28,0x2c,0x28,0x28,0x2c,
  0x00,0x04,0x04,0x00,0x04,0x00,0x00,0x04,0x0c,0x08,0x08,0x0c,0x08,0x0c,0x0c,0x08,
  0x04,0x00,0x00,0x04,0x00,0x04,0x04,0x00,0x08,0x0c,0x0c,0x08,0x0c,0x08,0x08,0x0c,
  0x24,0x20,0x20,0x24,0x20,0x24,0x24,0x20,0x28,0x2c,0x2c,0x28,0x2c,0x28,0x28,0x2c,
  0x20,0x24,0x24,0x20,0x24,0x20,0x20,0x24,0x2c,0x28,0x28,0x2c,0x28,0x2c,0x2c,0x28,
  0x80,0x84,0x84,0x80,0x84,0x80,0x80,0x84,0x8c,0x88,0x88,0x8c,0x88,0x8c,0x8c,0x88,
  0x84,0x80,0x80,0x84,0x80,0x84,0x84,0x80,0x88,0x8c,0x8c,0x88,0x8c,0x88,0x88,0x8c,
  0xa4,0xa0,0xa0,0xa4,0xa0,0xa4,0xa4,0xa0,0xa8,0xac,0xac,0xa8,0xac,0xa8,0xa8,0xac,
  0xa0,0xa4,0xa4,0xa0,0xa4,0xa0,0xa0,0xa4,0xac,0xa8,0xa8,0xac,0xa8,0xac,0xac,0xa8,
  0x84,0x80,0x80,0x84,0x80,0x84,0x84,0x80,0x88,0x8c,0x8c,0x88,0x8c,0x88,0x88,0x8c,
  0x80,0x84,0x84,0x80,0x84,0x80,0x80,0x84,0x8c,0x88,0x88,0x8c,0x88,0x8c,0x8c,0x88,
  0xa0,0xa4,0xa4,0xa0,0xa4,0xa0,0xa0,0xa4,0xac,0xa8,0xa8,0xac,0xa8,0xac,0xac,0xa8,
  0xa4,0xa0,0xa0,0xa4,0xa0,0xa4,0xa4,0xa0,0xa8,0xac,0xac,0xa8,0xac,0xa8,0xa8,0xac,
};

// opcodes that access (HL)/(IX+d)/(IY+d) and need a displacement byte when
// DD/FD-prefixed (identical to chips _z80_indirect_table)
static const uint8_t _zf_indirect_table[256] = {
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,1,1,1,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,1,0,0,0,0,0,0,0,1,0,
    0,0,0,0,0,0,1,0,0,0,0,0,0,0,1,0,
    0,0,0,0,0,0,1,0,0,0,0,0,0,0,1,0,
    1,1,1,1,1,1,0,1,0,0,0,0,0,0,1,0,
    0,0,0,0,0,0,1,0,0,0,0,0,0,0,1,0,
    0,0,0,0,0,0,1,0,0,0,0,0,0,0,1,0,
    0,0,0,0,0,0,1,0,0,0,0,0,0,0,1,0,
    0,0,0,0,0,0,1,0,0,0,0,0,0,0,1,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
};

// ---------------------------------------------------------------------------
// flag helpers — verbatim ports of chips' _z80_* helpers

static inline uint8_t _zf_sz_flags(uint8_t val) {
    return (val != 0) ? (val & ZF_SF) : ZF_ZF;
}

static inline uint8_t _zf_szyxch_flags(uint8_t acc, uint8_t val, uint32_t res) {
    return _zf_sz_flags((uint8_t)res) |
        (res & (ZF_YF|ZF_XF)) |
        ((res >> 8) & ZF_CF) |
        ((acc ^ val ^ res) & ZF_HF);
}

static inline uint8_t _zf_add_flags(uint8_t acc, uint8_t val, uint32_t res) {
    return _zf_szyxch_flags(acc, val, res) | ((((val ^ acc ^ 0x80) & (val ^ res)) >> 5) & ZF_VF);
}

static inline uint8_t _zf_sub_flags(uint8_t acc, uint8_t val, uint32_t res) {
    return ZF_NF | _zf_szyxch_flags(acc, val, res) | ((((val ^ acc) & (res ^ acc)) >> 5) & ZF_VF);
}

static inline uint8_t _zf_cp_flags(uint8_t acc, uint8_t val, uint32_t res) {
    return ZF_NF |
        _zf_sz_flags((uint8_t)res) |
        (val & (ZF_YF|ZF_XF)) |
        ((res >> 8) & ZF_CF) |
        ((acc ^ val ^ res) & ZF_HF) |
        ((((val ^ acc) & (res ^ acc)) >> 5) & ZF_VF);
}

static inline uint8_t _zf_sziff2_flags(zf_t* c, uint8_t val) {
    return (c->f & ZF_CF) | _zf_sz_flags(val) | (val & (ZF_YF|ZF_XF)) | (c->iff2 ? ZF_PF : 0);
}

static inline void _zf_add8(zf_t* c, uint8_t val) {
    uint32_t res = c->a + val;
    c->f = _zf_add_flags(c->a, val, res);
    c->a = (uint8_t)res;
}

static inline void _zf_adc8(zf_t* c, uint8_t val) {
    uint32_t res = c->a + val + (c->f & ZF_CF);
    c->f = _zf_add_flags(c->a, val, res);
    c->a = (uint8_t)res;
}

static inline void _zf_sub8(zf_t* c, uint8_t val) {
    uint32_t res = (uint32_t)((int)c->a - (int)val);
    c->f = _zf_sub_flags(c->a, val, res);
    c->a = (uint8_t)res;
}

static inline void _zf_sbc8(zf_t* c, uint8_t val) {
    uint32_t res = (uint32_t)((int)c->a - (int)val - (c->f & ZF_CF));
    c->f = _zf_sub_flags(c->a, val, res);
    c->a = (uint8_t)res;
}

static inline void _zf_and8(zf_t* c, uint8_t val) {
    c->a &= val;
    c->f = _zf_szp_flags[c->a] | ZF_HF;
}

static inline void _zf_xor8(zf_t* c, uint8_t val) {
    c->a ^= val;
    c->f = _zf_szp_flags[c->a];
}

static inline void _zf_or8(zf_t* c, uint8_t val) {
    c->a |= val;
    c->f = _zf_szp_flags[c->a];
}

static inline void _zf_cp8(zf_t* c, uint8_t val) {
    uint32_t res = (uint32_t)((int)c->a - (int)val);
    c->f = _zf_cp_flags(c->a, val, res);
}

static inline void _zf_neg8(zf_t* c) {
    uint32_t res = (uint32_t)(0 - (int)c->a);
    c->f = _zf_sub_flags(0, c->a, res);
    c->a = (uint8_t)res;
}

static inline uint8_t _zf_inc8(zf_t* c, uint8_t val) {
    uint8_t res = val + 1;
    uint8_t f = _zf_sz_flags(res) | (res & (ZF_XF|ZF_YF)) | ((res ^ val) & ZF_HF);
    if (res == 0x80) { f |= ZF_VF; }
    c->f = f | (c->f & ZF_CF);
    return res;
}

static inline uint8_t _zf_dec8(zf_t* c, uint8_t val) {
    uint8_t res = val - 1;
    uint8_t f = ZF_NF | _zf_sz_flags(res) | (res & (ZF_XF|ZF_YF)) | ((res ^ val) & ZF_HF);
    if (res == 0x7F) { f |= ZF_VF; }
    c->f = f | (c->f & ZF_CF);
    return res;
}

static inline void _zf_rlca(zf_t* c) {
    uint8_t res = (uint8_t)((c->a << 1) | (c->a >> 7));
    c->f = ((c->a >> 7) & ZF_CF) | (c->f & (ZF_SF|ZF_ZF|ZF_PF)) | (res & (ZF_YF|ZF_XF));
    c->a = res;
}

static inline void _zf_rrca(zf_t* c) {
    uint8_t res = (uint8_t)((c->a >> 1) | (c->a << 7));
    c->f = (c->a & ZF_CF) | (c->f & (ZF_SF|ZF_ZF|ZF_PF)) | (res & (ZF_YF|ZF_XF));
    c->a = res;
}

static inline void _zf_rla(zf_t* c) {
    uint8_t res = (uint8_t)((c->a << 1) | (c->f & ZF_CF));
    c->f = ((c->a >> 7) & ZF_CF) | (c->f & (ZF_SF|ZF_ZF|ZF_PF)) | (res & (ZF_YF|ZF_XF));
    c->a = res;
}

static inline void _zf_rra(zf_t* c) {
    uint8_t res = (uint8_t)((c->a >> 1) | ((c->f & ZF_CF) << 7));
    c->f = (c->a & ZF_CF) | (c->f & (ZF_SF|ZF_ZF|ZF_PF)) | (res & (ZF_YF|ZF_XF));
    c->a = res;
}

static inline void _zf_daa(zf_t* c) {
    uint8_t res = c->a;
    if (c->f & ZF_NF) {
        if (((c->a & 0xF) > 0x9) || (c->f & ZF_HF)) { res -= 0x06; }
        if ((c->a > 0x99) || (c->f & ZF_CF)) { res -= 0x60; }
    }
    else {
        if (((c->a & 0xF) > 0x9) || (c->f & ZF_HF)) { res += 0x06; }
        if ((c->a > 0x99) || (c->f & ZF_CF)) { res += 0x60; }
    }
    c->f &= ZF_CF|ZF_NF;
    c->f |= (c->a > 0x99) ? ZF_CF : 0;
    c->f |= (c->a ^ res) & ZF_HF;
    c->f |= _zf_szp_flags[res];
    c->a = res;
}

static inline void _zf_cpl(zf_t* c) {
    c->a ^= 0xFF;
    c->f = (c->f & (ZF_SF|ZF_ZF|ZF_PF|ZF_CF)) | ZF_HF | ZF_NF | (c->a & (ZF_YF|ZF_XF));
}

static inline void _zf_scf(zf_t* c) {
    c->f = (c->f & (ZF_SF|ZF_ZF|ZF_PF|ZF_CF)) | ZF_CF | (c->a & (ZF_YF|ZF_XF));
}

static inline void _zf_ccf(zf_t* c) {
    c->f = ((c->f & (ZF_SF|ZF_ZF|ZF_PF|ZF_CF)) | ((c->f & ZF_CF) << 4) | (c->a & (ZF_YF|ZF_XF))) ^ ZF_CF;
}

static inline void _zf_add16(zf_t* c, unsigned idx, uint16_t val) {
    const uint16_t acc = c->hlx[idx].hl;
    c->wz = acc + 1;
    const uint32_t res = acc + val;
    c->hlx[idx].hl = (uint16_t)res;
    c->f = (c->f & (ZF_SF|ZF_ZF|ZF_VF)) |
           (((acc ^ res ^ val) >> 8) & ZF_HF) |
           ((res >> 16) & ZF_CF) |
           ((res >> 8) & (ZF_YF|ZF_XF));
}

static inline void _zf_adc16(zf_t* c, uint16_t val) {
    const uint16_t acc = c->hl;
    c->wz = acc + 1;
    const uint32_t res = acc + val + (c->f & ZF_CF);
    c->hl = (uint16_t)res;
    c->f = (uint8_t)((((val ^ acc ^ 0x8000) & (val ^ res) & 0x8000) >> 13) |
           (((acc ^ res ^ val) >> 8) & ZF_HF) |
           ((res >> 16) & ZF_CF) |
           ((res >> 8) & (ZF_SF|ZF_YF|ZF_XF)) |
           ((res & 0xFFFF) ? 0 : ZF_ZF));
}

static inline void _zf_sbc16(zf_t* c, uint16_t val) {
    const uint16_t acc = c->hl;
    c->wz = acc + 1;
    const uint32_t res = acc - val - (c->f & ZF_CF);
    c->hl = (uint16_t)res;
    c->f = (uint8_t)((ZF_NF | (((val ^ acc) & (acc ^ res) & 0x8000) >> 13)) |
           (((acc ^ res ^ val) >> 8) & ZF_HF) |
           ((res >> 16) & ZF_CF) |
           ((res >> 8) & (ZF_SF|ZF_YF|ZF_XF)) |
           ((res & 0xFFFF) ? 0 : ZF_ZF));
}

static inline int _zf_ldi_ldd(zf_t* c, uint8_t val) {
    const uint8_t res = c->a + val;
    c->bc -= 1;
    c->f = (c->f & (ZF_SF|ZF_ZF|ZF_CF)) |
           ((res & 2) ? ZF_YF : 0) |
           ((res & 8) ? ZF_XF : 0) |
           (c->bc ? ZF_VF : 0);
    return c->bc != 0;
}

static inline int _zf_cpi_cpd(zf_t* c, uint8_t val) {
    uint32_t res = (uint32_t)((int)c->a - (int)val);
    c->bc -= 1;
    uint8_t f = (c->f & ZF_CF) | ZF_NF | _zf_sz_flags((uint8_t)res);
    if ((res & 0xF) > ((uint32_t)c->a & 0xF)) {
        f |= ZF_HF;
        res--;
    }
    if (res & 2) { f |= ZF_YF; }
    if (res & 8) { f |= ZF_XF; }
    if (c->bc) { f |= ZF_VF; }
    c->f = f;
    return (c->bc != 0) && !(f & ZF_ZF);
}

static inline int _zf_ini_ind(zf_t* c, uint8_t val, uint8_t cval) {
    const uint8_t b = c->b;
    uint8_t f = _zf_sz_flags(b) | (b & (ZF_XF|ZF_YF));
    if (val & ZF_SF) { f |= ZF_NF; }
    uint32_t t = (uint32_t)cval + val;
    if (t & 0x100) { f |= ZF_HF|ZF_CF; }
    f |= _zf_szp_flags[((uint8_t)(t & 7)) ^ b] & ZF_PF;
    c->f = f;
    return (b != 0);
}

static inline int _zf_outi_outd(zf_t* c, uint8_t val) {
    const uint8_t b = c->b;
    uint8_t f = _zf_sz_flags(b) | (b & (ZF_XF|ZF_YF));
    if (val & ZF_SF) { f |= ZF_NF; }
    uint32_t t = (uint32_t)c->l + val;
    if (t & 0x0100) { f |= ZF_HF|ZF_CF; }
    f |= _zf_szp_flags[((uint8_t)(t & 7)) ^ b] & ZF_PF;
    c->f = f;
    return (b != 0);
}

static inline uint8_t _zf_in(zf_t* c, uint8_t val) {
    c->f = (c->f & ZF_CF) | _zf_szp_flags[val];
    return val;
}

static inline uint8_t _zf_rrd(zf_t* c, uint8_t val) {
    const uint8_t lo = c->a & 0x0F;
    c->a = (c->a & 0xF0) | (val & 0x0F);
    val = (uint8_t)((val >> 4) | (lo << 4));
    c->f = (c->f & ZF_CF) | _zf_szp_flags[c->a];
    return val;
}

static inline uint8_t _zf_rld(zf_t* c, uint8_t val) {
    const uint8_t lo = c->a & 0x0F;
    c->a = (c->a & 0xF0) | (val >> 4);
    val = (uint8_t)((val << 4) | lo);
    c->f = (c->f & ZF_CF) | _zf_szp_flags[c->a];
    return val;
}

static inline uint8_t _zf_rlc(zf_t* c, uint8_t val) {
    uint8_t res = (uint8_t)((val << 1) | (val >> 7));
    c->f = _zf_szp_flags[res] | ((val >> 7) & ZF_CF);
    return res;
}

static inline uint8_t _zf_rrc(zf_t* c, uint8_t val) {
    uint8_t res = (uint8_t)((val >> 1) | (val << 7));
    c->f = _zf_szp_flags[res] | (val & ZF_CF);
    return res;
}

static inline uint8_t _zf_rl(zf_t* c, uint8_t val) {
    uint8_t res = (uint8_t)((val << 1) | (c->f & ZF_CF));
    c->f = _zf_szp_flags[res] | ((val >> 7) & ZF_CF);
    return res;
}

static inline uint8_t _zf_rr(zf_t* c, uint8_t val) {
    uint8_t res = (uint8_t)((val >> 1) | ((c->f & ZF_CF) << 7));
    c->f = _zf_szp_flags[res] | (val & ZF_CF);
    return res;
}

static inline uint8_t _zf_sla(zf_t* c, uint8_t val) {
    uint8_t res = (uint8_t)(val << 1);
    c->f = _zf_szp_flags[res] | ((val >> 7) & ZF_CF);
    return res;
}

static inline uint8_t _zf_sra(zf_t* c, uint8_t val) {
    uint8_t res = (uint8_t)((val >> 1) | (val & 0x80));
    c->f = _zf_szp_flags[res] | (val & ZF_CF);
    return res;
}

static inline uint8_t _zf_sll(zf_t* c, uint8_t val) {
    uint8_t res = (uint8_t)((val << 1) | 1);
    c->f = _zf_szp_flags[res] | ((val >> 7) & ZF_CF);
    return res;
}

static inline uint8_t _zf_srl(zf_t* c, uint8_t val) {
    uint8_t res = (uint8_t)(val >> 1);
    c->f = _zf_szp_flags[res] | (val & ZF_CF);
    return res;
}

static inline void _zf_ex_de_hl(zf_t* c) {
    uint16_t tmp = c->hl; c->hl = c->de; c->de = tmp;
}

static inline void _zf_ex_af_af2(zf_t* c) {
    uint16_t tmp = c->af2; c->af2 = c->af; c->af = tmp;
}

static inline void _zf_exx(zf_t* c) {
    uint16_t tmp;
    tmp = c->bc; c->bc = c->bc2; c->bc2 = tmp;
    tmp = c->de; c->de = c->de2; c->de2 = tmp;
    tmp = c->hl; c->hl = c->hl2; c->hl2 = tmp;
}

// ---------------------------------------------------------------------------
// register accessors for CB block (never rewired to IX/IY, matching chips)
static inline uint8_t _zf_get_r8(zf_t* c, uint8_t z) {
    switch (z & 7) {
        case 0: return c->b;
        case 1: return c->c;
        case 2: return c->d;
        case 3: return c->e;
        case 4: return c->h;
        case 5: return c->l;
        case 6: return 0; // (HL): caller supplies memory value
        default: return c->a;
    }
}

static inline void _zf_set_r8(zf_t* c, uint8_t z, uint8_t v) {
    switch (z & 7) {
        case 0: c->b = v; break;
        case 1: c->c = v; break;
        case 2: c->d = v; break;
        case 3: c->e = v; break;
        case 4: c->h = v; break;
        case 5: c->l = v; break;
        case 6: break;    // (HL): caller writes memory
        default: c->a = v; break;
    }
}

// CB-prefix action, mirroring chips _z80_cb_action.
// val: input value; is_mem: value came from memory (affects BIT X/Y flags via WZ)
// returns 1 if *out holds a result that must be written back (i.e. not BIT)
static inline int _zf_cb_op(zf_t* c, uint8_t opcode, uint8_t val, int is_mem, uint8_t* out) {
    const uint8_t x = opcode >> 6;
    const uint8_t y = (opcode >> 3) & 7;
    uint8_t res;
    switch (x) {
        case 0: // rot/shift
            switch (y) {
                case 0: res = _zf_rlc(c, val); break;
                case 1: res = _zf_rrc(c, val); break;
                case 2: res = _zf_rl(c, val); break;
                case 3: res = _zf_rr(c, val); break;
                case 4: res = _zf_sla(c, val); break;
                case 5: res = _zf_sra(c, val); break;
                case 6: res = _zf_sll(c, val); break;
                default: res = _zf_srl(c, val); break;
            }
            break;
        case 1: // bit
            res = val & (uint8_t)(1 << y);
            c->f = (c->f & ZF_CF) | ZF_HF | (res ? (res & ZF_SF) : (ZF_ZF|ZF_PF));
            if (is_mem) { c->f |= (c->wz >> 8) & (ZF_YF|ZF_XF); }
            else        { c->f |= val & (ZF_YF|ZF_XF); }
            return 0;
        case 2: // res
            res = val & (uint8_t)~(1 << y);
            break;
        default: // set
            res = val | (uint8_t)(1 << y);
            break;
    }
    *out = res;
    return 1;
}

// ---------------------------------------------------------------------------
// sandbox runner

#define _ZF_MASK 63

// memory helpers: 16-bit address computed normally, masked at the array index
#define _ZF_RD(addr)      (mem[(uint16_t)(addr) & _ZF_MASK])
#define _ZF_WR(addr, v)   (mem[(uint16_t)(addr) & _ZF_MASK] = (uint8_t)(v))
#define _ZF_IMM8()        (mem[(c->pc++) & _ZF_MASK])
#define _ZF_RINC()        (c->r = (uint8_t)((c->r & 0x80) | ((c->r + 1) & 0x7F)))

// Runs the 64-byte sandbox; returns instruction count (steps); sets *out_e and *halted.
static int zf_run(uint8_t* mem, uint8_t d_init, int budget, uint8_t* out_e, int* halted) {
    zf_t cpu;
    zf_t* c = &cpu;
    memset(c, 0, sizeof(zf_t));
    c->a  = 0xFF;
    c->f  = 0x00;
    c->de = (uint16_t)d_init << 8;   // D = d_init, E = 0
    c->sp = 0x00FF;
    // bc/hl/ix/iy/wz/ir/af2/bc2/de2/hl2/im/iff/pc all zero from memset

    // NOTE: the reference protocol (sb_run on top of chips z80.h) counts the
    // initial overlapped-NOP M1 fetch as the first "step" (z80_opdone fires on
    // the very first tick, before any real instruction completes). To be
    // bit-identical we replicate that: steps starts at 1 and at most budget-1
    // real instructions execute.
    int steps = (budget > 0) ? 1 : 0;
    int hlt = 0;

    while (steps < budget) {
        uint8_t op = _ZF_IMM8();
        _ZF_RINC();
        unsigned idx = 0; // 0: HL, 1: IX, 2: IY

        // DD/FD prefix chain: only the last prefix matters; each prefix byte
        // is an extra M1 (R increment) but the whole chain + opcode counts as
        // a single instruction (chips z80_opdone stays false during prefixes).
        if (op == 0xDD || op == 0xFD) {
            int chain = 0;
            do {
                idx = (op == 0xDD) ? 1u : 2u;
                op = _ZF_IMM8();
                _ZF_RINC();
                // If >64 consecutive prefix bytes were fetched, PC has wrapped
                // the whole 64-byte memory and every byte is DD/FD: the CPU
                // livelocks in prefix fetches and never completes another
                // instruction (the reference exhausts its tick cap the same
                // way). Stop with the current architectural state.
                if (++chain > 64) { goto done; }
            } while (op == 0xDD || op == 0xFD);
        }

        // effective address for (HL) / (IX+d) / (IY+d) ops
        uint16_t addr = c->hlx[idx].hl;
        if (idx != 0 && _zf_indirect_table[op]) {
            int8_t d = (int8_t)_ZF_IMM8();
            addr = (uint16_t)(c->hlx[idx].hl + d);
            c->wz = addr;
        }

        switch (op) {
            // --- 0x00..0x3F
            case 0x00: break; // NOP
            case 0x01: c->c = _ZF_IMM8(); c->b = _ZF_IMM8(); break; // LD BC,nn
            case 0x02: _ZF_WR(c->bc, c->a); c->wzl = (uint8_t)(c->c + 1); c->wzh = c->a; break; // LD (BC),A
            case 0x03: c->bc++; break;
            case 0x04: c->b = _zf_inc8(c, c->b); break;
            case 0x05: c->b = _zf_dec8(c, c->b); break;
            case 0x06: c->b = _ZF_IMM8(); break;
            case 0x07: _zf_rlca(c); break;
            case 0x08: _zf_ex_af_af2(c); break;
            case 0x09: _zf_add16(c, idx, c->bc); break;
            case 0x0A: c->a = _ZF_RD(c->bc); c->wz = c->bc + 1; break; // LD A,(BC)
            case 0x0B: c->bc--; break;
            case 0x0C: c->c = _zf_inc8(c, c->c); break;
            case 0x0D: c->c = _zf_dec8(c, c->c); break;
            case 0x0E: c->c = _ZF_IMM8(); break;
            case 0x0F: _zf_rrca(c); break;
            case 0x10: { // DJNZ d
                int8_t d = (int8_t)_ZF_IMM8();
                if (--c->b != 0) { c->pc += d; c->wz = c->pc; }
            } break;
            case 0x11: c->e = _ZF_IMM8(); c->d = _ZF_IMM8(); break;
            case 0x12: _ZF_WR(c->de, c->a); c->wzl = (uint8_t)(c->e + 1); c->wzh = c->a; break; // LD (DE),A
            case 0x13: c->de++; break;
            case 0x14: c->d = _zf_inc8(c, c->d); break;
            case 0x15: c->d = _zf_dec8(c, c->d); break;
            case 0x16: c->d = _ZF_IMM8(); break;
            case 0x17: _zf_rla(c); break;
            case 0x18: { // JR d
                int8_t d = (int8_t)_ZF_IMM8();
                c->pc += d; c->wz = c->pc;
            } break;
            case 0x19: _zf_add16(c, idx, c->de); break;
            case 0x1A: c->a = _ZF_RD(c->de); c->wz = c->de + 1; break; // LD A,(DE)
            case 0x1B: c->de--; break;
            case 0x1C: c->e = _zf_inc8(c, c->e); break;
            case 0x1D: c->e = _zf_dec8(c, c->e); break;
            case 0x1E: c->e = _ZF_IMM8(); break;
            case 0x1F: _zf_rra(c); break;
            case 0x20: { // JR NZ,d
                int8_t d = (int8_t)_ZF_IMM8();
                if (!(c->f & ZF_ZF)) { c->pc += d; c->wz = c->pc; }
            } break;
            case 0x21: c->hlx[idx].l = _ZF_IMM8(); c->hlx[idx].h = _ZF_IMM8(); break; // LD HL,nn
            case 0x22: { // LD (nn),HL
                c->wzl = _ZF_IMM8(); c->wzh = _ZF_IMM8();
                _ZF_WR(c->wz, c->hlx[idx].l); c->wz++;
                _ZF_WR(c->wz, c->hlx[idx].h);
            } break;
            case 0x23: c->hlx[idx].hl++; break;
            case 0x24: c->hlx[idx].h = _zf_inc8(c, c->hlx[idx].h); break;
            case 0x25: c->hlx[idx].h = _zf_dec8(c, c->hlx[idx].h); break;
            case 0x26: c->hlx[idx].h = _ZF_IMM8(); break;
            case 0x27: _zf_daa(c); break;
            case 0x28: { // JR Z,d
                int8_t d = (int8_t)_ZF_IMM8();
                if (c->f & ZF_ZF) { c->pc += d; c->wz = c->pc; }
            } break;
            case 0x29: _zf_add16(c, idx, c->hlx[idx].hl); break;
            case 0x2A: { // LD HL,(nn)
                c->wzl = _ZF_IMM8(); c->wzh = _ZF_IMM8();
                c->hlx[idx].l = _ZF_RD(c->wz); c->wz++;
                c->hlx[idx].h = _ZF_RD(c->wz);
            } break;
            case 0x2B: c->hlx[idx].hl--; break;
            case 0x2C: c->hlx[idx].l = _zf_inc8(c, c->hlx[idx].l); break;
            case 0x2D: c->hlx[idx].l = _zf_dec8(c, c->hlx[idx].l); break;
            case 0x2E: c->hlx[idx].l = _ZF_IMM8(); break;
            case 0x2F: _zf_cpl(c); break;
            case 0x30: { // JR NC,d
                int8_t d = (int8_t)_ZF_IMM8();
                if (!(c->f & ZF_CF)) { c->pc += d; c->wz = c->pc; }
            } break;
            case 0x31: c->spl = _ZF_IMM8(); c->sph = _ZF_IMM8(); break; // LD SP,nn
            case 0x32: { // LD (nn),A
                c->wzl = _ZF_IMM8(); c->wzh = _ZF_IMM8();
                _ZF_WR(c->wz, c->a); c->wz++; c->wzh = c->a;
            } break;
            case 0x33: c->sp++; break;
            case 0x34: { uint8_t v = _zf_inc8(c, _ZF_RD(addr)); _ZF_WR(addr, v); } break; // INC (HL)
            case 0x35: { uint8_t v = _zf_dec8(c, _ZF_RD(addr)); _ZF_WR(addr, v); } break; // DEC (HL)
            case 0x36: { uint8_t n = _ZF_IMM8(); _ZF_WR(addr, n); } break; // LD (HL),n / LD (IX+d),n
            case 0x37: _zf_scf(c); break;
            case 0x38: { // JR C,d
                int8_t d = (int8_t)_ZF_IMM8();
                if (c->f & ZF_CF) { c->pc += d; c->wz = c->pc; }
            } break;
            case 0x39: _zf_add16(c, idx, c->sp); break;
            case 0x3A: { // LD A,(nn)
                c->wzl = _ZF_IMM8(); c->wzh = _ZF_IMM8();
                c->a = _ZF_RD(c->wz); c->wz++;
            } break;
            case 0x3B: c->sp--; break;
            case 0x3C: c->a = _zf_inc8(c, c->a); break;
            case 0x3D: c->a = _zf_dec8(c, c->a); break;
            case 0x3E: c->a = _ZF_IMM8(); break;
            case 0x3F: _zf_ccf(c); break;

            // --- 0x40..0x7F: LD r,r' (H/L rewired to IXH/IXL etc., but not
            //     when the other operand is (HL): LD H,(IX+d) loads real H)
            case 0x40: break; // LD B,B
            case 0x41: c->b = c->c; break;
            case 0x42: c->b = c->d; break;
            case 0x43: c->b = c->e; break;
            case 0x44: c->b = c->hlx[idx].h; break;
            case 0x45: c->b = c->hlx[idx].l; break;
            case 0x46: c->b = _ZF_RD(addr); break;
            case 0x47: c->b = c->a; break;
            case 0x48: c->c = c->b; break;
            case 0x49: break; // LD C,C
            case 0x4A: c->c = c->d; break;
            case 0x4B: c->c = c->e; break;
            case 0x4C: c->c = c->hlx[idx].h; break;
            case 0x4D: c->c = c->hlx[idx].l; break;
            case 0x4E: c->c = _ZF_RD(addr); break;
            case 0x4F: c->c = c->a; break;
            case 0x50: c->d = c->b; break;
            case 0x51: c->d = c->c; break;
            case 0x52: break; // LD D,D
            case 0x53: c->d = c->e; break;
            case 0x54: c->d = c->hlx[idx].h; break;
            case 0x55: c->d = c->hlx[idx].l; break;
            case 0x56: c->d = _ZF_RD(addr); break;
            case 0x57: c->d = c->a; break;
            case 0x58: c->e = c->b; break;
            case 0x59: c->e = c->c; break;
            case 0x5A: c->e = c->d; break;
            case 0x5B: break; // LD E,E
            case 0x5C: c->e = c->hlx[idx].h; break;
            case 0x5D: c->e = c->hlx[idx].l; break;
            case 0x5E: c->e = _ZF_RD(addr); break;
            case 0x5F: c->e = c->a; break;
            case 0x60: c->hlx[idx].h = c->b; break;
            case 0x61: c->hlx[idx].h = c->c; break;
            case 0x62: c->hlx[idx].h = c->d; break;
            case 0x63: c->hlx[idx].h = c->e; break;
            case 0x64: break; // LD H,H
            case 0x65: c->hlx[idx].h = c->hlx[idx].l; break;
            case 0x66: c->h = _ZF_RD(addr); break; // LD H,(HL)/(IX+d): real H
            case 0x67: c->hlx[idx].h = c->a; break;
            case 0x68: c->hlx[idx].l = c->b; break;
            case 0x69: c->hlx[idx].l = c->c; break;
            case 0x6A: c->hlx[idx].l = c->d; break;
            case 0x6B: c->hlx[idx].l = c->e; break;
            case 0x6C: c->hlx[idx].l = c->hlx[idx].h; break;
            case 0x6D: break; // LD L,L
            case 0x6E: c->l = _ZF_RD(addr); break; // LD L,(HL)/(IX+d): real L
            case 0x6F: c->hlx[idx].l = c->a; break;
            case 0x70: _ZF_WR(addr, c->b); break;
            case 0x71: _ZF_WR(addr, c->c); break;
            case 0x72: _ZF_WR(addr, c->d); break;
            case 0x73: _ZF_WR(addr, c->e); break;
            case 0x74: _ZF_WR(addr, c->h); break; // LD (HL)/(IX+d),H: real H
            case 0x75: _ZF_WR(addr, c->l); break; // LD (HL)/(IX+d),L: real L
            case 0x76: c->pc--; hlt = 1; break;   // HALT
            case 0x77: _ZF_WR(addr, c->a); break;
            case 0x78: c->a = c->b; break;
            case 0x79: c->a = c->c; break;
            case 0x7A: c->a = c->d; break;
            case 0x7B: c->a = c->e; break;
            case 0x7C: c->a = c->hlx[idx].h; break;
            case 0x7D: c->a = c->hlx[idx].l; break;
            case 0x7E: c->a = _ZF_RD(addr); break;
            case 0x7F: break; // LD A,A

            // --- 0x80..0xBF: ALU A,r
            case 0x80: _zf_add8(c, c->b); break;
            case 0x81: _zf_add8(c, c->c); break;
            case 0x82: _zf_add8(c, c->d); break;
            case 0x83: _zf_add8(c, c->e); break;
            case 0x84: _zf_add8(c, c->hlx[idx].h); break;
            case 0x85: _zf_add8(c, c->hlx[idx].l); break;
            case 0x86: _zf_add8(c, _ZF_RD(addr)); break;
            case 0x87: _zf_add8(c, c->a); break;
            case 0x88: _zf_adc8(c, c->b); break;
            case 0x89: _zf_adc8(c, c->c); break;
            case 0x8A: _zf_adc8(c, c->d); break;
            case 0x8B: _zf_adc8(c, c->e); break;
            case 0x8C: _zf_adc8(c, c->hlx[idx].h); break;
            case 0x8D: _zf_adc8(c, c->hlx[idx].l); break;
            case 0x8E: _zf_adc8(c, _ZF_RD(addr)); break;
            case 0x8F: _zf_adc8(c, c->a); break;
            case 0x90: _zf_sub8(c, c->b); break;
            case 0x91: _zf_sub8(c, c->c); break;
            case 0x92: _zf_sub8(c, c->d); break;
            case 0x93: _zf_sub8(c, c->e); break;
            case 0x94: _zf_sub8(c, c->hlx[idx].h); break;
            case 0x95: _zf_sub8(c, c->hlx[idx].l); break;
            case 0x96: _zf_sub8(c, _ZF_RD(addr)); break;
            case 0x97: _zf_sub8(c, c->a); break;
            case 0x98: _zf_sbc8(c, c->b); break;
            case 0x99: _zf_sbc8(c, c->c); break;
            case 0x9A: _zf_sbc8(c, c->d); break;
            case 0x9B: _zf_sbc8(c, c->e); break;
            case 0x9C: _zf_sbc8(c, c->hlx[idx].h); break;
            case 0x9D: _zf_sbc8(c, c->hlx[idx].l); break;
            case 0x9E: _zf_sbc8(c, _ZF_RD(addr)); break;
            case 0x9F: _zf_sbc8(c, c->a); break;
            case 0xA0: _zf_and8(c, c->b); break;
            case 0xA1: _zf_and8(c, c->c); break;
            case 0xA2: _zf_and8(c, c->d); break;
            case 0xA3: _zf_and8(c, c->e); break;
            case 0xA4: _zf_and8(c, c->hlx[idx].h); break;
            case 0xA5: _zf_and8(c, c->hlx[idx].l); break;
            case 0xA6: _zf_and8(c, _ZF_RD(addr)); break;
            case 0xA7: _zf_and8(c, c->a); break;
            case 0xA8: _zf_xor8(c, c->b); break;
            case 0xA9: _zf_xor8(c, c->c); break;
            case 0xAA: _zf_xor8(c, c->d); break;
            case 0xAB: _zf_xor8(c, c->e); break;
            case 0xAC: _zf_xor8(c, c->hlx[idx].h); break;
            case 0xAD: _zf_xor8(c, c->hlx[idx].l); break;
            case 0xAE: _zf_xor8(c, _ZF_RD(addr)); break;
            case 0xAF: _zf_xor8(c, c->a); break;
            case 0xB0: _zf_or8(c, c->b); break;
            case 0xB1: _zf_or8(c, c->c); break;
            case 0xB2: _zf_or8(c, c->d); break;
            case 0xB3: _zf_or8(c, c->e); break;
            case 0xB4: _zf_or8(c, c->hlx[idx].h); break;
            case 0xB5: _zf_or8(c, c->hlx[idx].l); break;
            case 0xB6: _zf_or8(c, _ZF_RD(addr)); break;
            case 0xB7: _zf_or8(c, c->a); break;
            case 0xB8: _zf_cp8(c, c->b); break;
            case 0xB9: _zf_cp8(c, c->c); break;
            case 0xBA: _zf_cp8(c, c->d); break;
            case 0xBB: _zf_cp8(c, c->e); break;
            case 0xBC: _zf_cp8(c, c->hlx[idx].h); break;
            case 0xBD: _zf_cp8(c, c->hlx[idx].l); break;
            case 0xBE: _zf_cp8(c, _ZF_RD(addr)); break;
            case 0xBF: _zf_cp8(c, c->a); break;

            // --- 0xC0..0xFF
            case 0xC0: if (!(c->f & ZF_ZF)) { goto do_ret; } break; // RET NZ
            case 0xC1: c->c = _ZF_RD(c->sp); c->sp++; c->b = _ZF_RD(c->sp); c->sp++; break; // POP BC
            case 0xC2: { // JP NZ,nn
                c->wzl = _ZF_IMM8(); c->wzh = _ZF_IMM8();
                if (!(c->f & ZF_ZF)) { c->pc = c->wz; }
            } break;
            case 0xC3: c->wzl = _ZF_IMM8(); c->wzh = _ZF_IMM8(); c->pc = c->wz; break; // JP nn
            case 0xC4: { // CALL NZ,nn
                c->wzl = _ZF_IMM8(); c->wzh = _ZF_IMM8();
                if (!(c->f & ZF_ZF)) { goto do_call; }
            } break;
            case 0xC5: c->sp--; _ZF_WR(c->sp, c->b); c->sp--; _ZF_WR(c->sp, c->c); break; // PUSH BC
            case 0xC6: _zf_add8(c, _ZF_IMM8()); break;
            case 0xC7: c->wz = 0x00; goto do_rst;
            case 0xC8: if (c->f & ZF_ZF) { goto do_ret; } break; // RET Z
            case 0xC9: goto do_ret; // RET
            case 0xCA: { // JP Z,nn
                c->wzl = _ZF_IMM8(); c->wzh = _ZF_IMM8();
                if (c->f & ZF_ZF) { c->pc = c->wz; }
            } break;
            case 0xCB: { // CB prefix
                if (idx == 0) {
                    uint8_t opc = _ZF_IMM8();
                    _ZF_RINC();
                    uint8_t z = opc & 7;
                    uint8_t res;
                    if (z == 6) { // (HL)
                        uint8_t val = _ZF_RD(c->hl);
                        if (_zf_cb_op(c, opc, val, 1, &res)) { _ZF_WR(c->hl, res); }
                    } else {
                        uint8_t val = _zf_get_r8(c, z);
                        if (_zf_cb_op(c, opc, val, 0, &res)) { _zf_set_r8(c, z, res); }
                    }
                } else { // DDCB/FDCB: d then opcode, both plain reads (no R inc)
                    int8_t d = (int8_t)_ZF_IMM8();
                    uint16_t a2 = (uint16_t)(c->hlx[idx].hl + d);
                    c->wz = a2;
                    uint8_t opc = _ZF_IMM8();
                    uint8_t val = _ZF_RD(a2);
                    uint8_t res;
                    if (_zf_cb_op(c, opc, val, 1, &res)) {
                        _ZF_WR(a2, res);
                        _zf_set_r8(c, opc & 7, res); // undocumented: also to register
                    }
                }
            } break;
            case 0xCC: { // CALL Z,nn
                c->wzl = _ZF_IMM8(); c->wzh = _ZF_IMM8();
                if (c->f & ZF_ZF) { goto do_call; }
            } break;
            case 0xCD: c->wzl = _ZF_IMM8(); c->wzh = _ZF_IMM8(); goto do_call; // CALL nn
            case 0xCE: _zf_adc8(c, _ZF_IMM8()); break;
            case 0xCF: c->wz = 0x08; goto do_rst;
            case 0xD0: if (!(c->f & ZF_CF)) { goto do_ret; } break; // RET NC
            case 0xD1: c->e = _ZF_RD(c->sp); c->sp++; c->d = _ZF_RD(c->sp); c->sp++; break; // POP DE
            case 0xD2: { // JP NC,nn
                c->wzl = _ZF_IMM8(); c->wzh = _ZF_IMM8();
                if (!(c->f & ZF_CF)) { c->pc = c->wz; }
            } break;
            case 0xD3: { // OUT (n),A: no I/O devices; only WZ is affected
                uint8_t n = _ZF_IMM8();
                c->wzl = n; c->wzh = c->a; c->wzl++;
            } break;
            case 0xD4: { // CALL NC,nn
                c->wzl = _ZF_IMM8(); c->wzh = _ZF_IMM8();
                if (!(c->f & ZF_CF)) { goto do_call; }
            } break;
            case 0xD5: c->sp--; _ZF_WR(c->sp, c->d); c->sp--; _ZF_WR(c->sp, c->e); break; // PUSH DE
            case 0xD6: _zf_sub8(c, _ZF_IMM8()); break;
            case 0xD7: c->wz = 0x10; goto do_rst;
            case 0xD8: if (c->f & ZF_CF) { goto do_ret; } break; // RET C
            case 0xD9: _zf_exx(c); break;
            case 0xDA: { // JP C,nn
                c->wzl = _ZF_IMM8(); c->wzh = _ZF_IMM8();
                if (c->f & ZF_CF) { c->pc = c->wz; }
            } break;
            case 0xDB: { // IN A,(n): open bus 0xFF, flags unchanged
                uint8_t n = _ZF_IMM8();
                c->wzl = n; c->wzh = c->a; c->wz++;
                c->a = 0xFF;
            } break;
            case 0xDC: { // CALL C,nn
                c->wzl = _ZF_IMM8(); c->wzh = _ZF_IMM8();
                if (c->f & ZF_CF) { goto do_call; }
            } break;
            case 0xDE: _zf_sbc8(c, _ZF_IMM8()); break;
            case 0xDF: c->wz = 0x18; goto do_rst;
            case 0xE0: if (!(c->f & ZF_PF)) { goto do_ret; } break; // RET PO
            case 0xE1: c->hlx[idx].l = _ZF_RD(c->sp); c->sp++; c->hlx[idx].h = _ZF_RD(c->sp); c->sp++; break; // POP HL
            case 0xE2: { // JP PO,nn
                c->wzl = _ZF_IMM8(); c->wzh = _ZF_IMM8();
                if (!(c->f & ZF_PF)) { c->pc = c->wz; }
            } break;
            case 0xE3: { // EX (SP),HL
                c->wzl = _ZF_RD(c->sp);
                c->wzh = _ZF_RD(c->sp + 1);
                _ZF_WR(c->sp + 1, c->hlx[idx].h);
                _ZF_WR(c->sp, c->hlx[idx].l);
                c->hlx[idx].hl = c->wz;
            } break;
            case 0xE4: { // CALL PO,nn
                c->wzl = _ZF_IMM8(); c->wzh = _ZF_IMM8();
                if (!(c->f & ZF_PF)) { goto do_call; }
            } break;
            case 0xE5: c->sp--; _ZF_WR(c->sp, c->hlx[idx].h); c->sp--; _ZF_WR(c->sp, c->hlx[idx].l); break; // PUSH HL
            case 0xE6: _zf_and8(c, _ZF_IMM8()); break;
            case 0xE7: c->wz = 0x20; goto do_rst;
            case 0xE8: if (c->f & ZF_PF) { goto do_ret; } break; // RET PE
            case 0xE9: c->pc = c->hlx[idx].hl; break; // JP HL
            case 0xEA: { // JP PE,nn
                c->wzl = _ZF_IMM8(); c->wzh = _ZF_IMM8();
                if (c->f & ZF_PF) { c->pc = c->wz; }
            } break;
            case 0xEB: _zf_ex_de_hl(c); break; // EX DE,HL (never rewired)
            case 0xEC: { // CALL PE,nn
                c->wzl = _ZF_IMM8(); c->wzh = _ZF_IMM8();
                if (c->f & ZF_PF) { goto do_call; }
            } break;
            case 0xED: { // ED prefix (DD/FD before ED is ignored)
                uint8_t e = _ZF_IMM8();
                _ZF_RINC();
                switch (e) {
                    // --- 0x40..0x7F grid
                    case 0x40: case 0x48: case 0x50: case 0x58:
                    case 0x60: case 0x68: case 0x70: case 0x78: { // IN r,(C)
                        c->wz = c->bc + 1;
                        uint8_t v = _zf_in(c, 0xFF); // open bus
                        switch ((e >> 3) & 7) {
                            case 0: c->b = v; break;
                            case 1: c->c = v; break;
                            case 2: c->d = v; break;
                            case 3: c->e = v; break;
                            case 4: c->h = v; break;
                            case 5: c->l = v; break;
                            case 6: break; // IN (C): flags only
                            default: c->a = v; break;
                        }
                    } break;
                    case 0x41: case 0x49: case 0x51: case 0x59:
                    case 0x61: case 0x69: case 0x71: case 0x79: // OUT (C),r / OUT (C),0
                        c->wz = c->bc + 1;
                        break;
                    case 0x42: _zf_sbc16(c, c->bc); break;
                    case 0x52: _zf_sbc16(c, c->de); break;
                    case 0x62: _zf_sbc16(c, c->hl); break;
                    case 0x72: _zf_sbc16(c, c->sp); break;
                    case 0x4A: _zf_adc16(c, c->bc); break;
                    case 0x5A: _zf_adc16(c, c->de); break;
                    case 0x6A: _zf_adc16(c, c->hl); break;
                    case 0x7A: _zf_adc16(c, c->sp); break;
                    case 0x43: { // LD (nn),BC
                        c->wzl = _ZF_IMM8(); c->wzh = _ZF_IMM8();
                        _ZF_WR(c->wz, c->c); c->wz++; _ZF_WR(c->wz, c->b);
                    } break;
                    case 0x53: { // LD (nn),DE
                        c->wzl = _ZF_IMM8(); c->wzh = _ZF_IMM8();
                        _ZF_WR(c->wz, c->e); c->wz++; _ZF_WR(c->wz, c->d);
                    } break;
                    case 0x63: { // LD (nn),HL (ED variant)
                        c->wzl = _ZF_IMM8(); c->wzh = _ZF_IMM8();
                        _ZF_WR(c->wz, c->l); c->wz++; _ZF_WR(c->wz, c->h);
                    } break;
                    case 0x73: { // LD (nn),SP
                        c->wzl = _ZF_IMM8(); c->wzh = _ZF_IMM8();
                        _ZF_WR(c->wz, c->spl); c->wz++; _ZF_WR(c->wz, c->sph);
                    } break;
                    case 0x4B: { // LD BC,(nn)
                        c->wzl = _ZF_IMM8(); c->wzh = _ZF_IMM8();
                        c->c = _ZF_RD(c->wz); c->wz++; c->b = _ZF_RD(c->wz);
                    } break;
                    case 0x5B: { // LD DE,(nn)
                        c->wzl = _ZF_IMM8(); c->wzh = _ZF_IMM8();
                        c->e = _ZF_RD(c->wz); c->wz++; c->d = _ZF_RD(c->wz);
                    } break;
                    case 0x6B: { // LD HL,(nn) (ED variant)
                        c->wzl = _ZF_IMM8(); c->wzh = _ZF_IMM8();
                        c->l = _ZF_RD(c->wz); c->wz++; c->h = _ZF_RD(c->wz);
                    } break;
                    case 0x7B: { // LD SP,(nn)
                        c->wzl = _ZF_IMM8(); c->wzh = _ZF_IMM8();
                        c->spl = _ZF_RD(c->wz); c->wz++; c->sph = _ZF_RD(c->wz);
                    } break;
                    case 0x44: case 0x4C: case 0x54: case 0x5C:
                    case 0x64: case 0x6C: case 0x74: case 0x7C:
                        _zf_neg8(c); break;
                    case 0x45: case 0x4D: case 0x55: case 0x5D:
                    case 0x65: case 0x6D: case 0x75: case 0x7D: { // RETN/RETI
                        c->wzl = _ZF_RD(c->sp); c->sp++;
                        c->wzh = _ZF_RD(c->sp); c->sp++;
                        c->pc = c->wz;
                        c->iff1 = c->iff2;
                    } break;
                    case 0x46: case 0x4E: case 0x66: case 0x6E: c->im = 0; break;
                    case 0x56: case 0x76: c->im = 1; break;
                    case 0x5E: case 0x7E: c->im = 2; break;
                    case 0x47: c->i = c->a; break; // LD I,A
                    case 0x4F: c->r = c->a; break; // LD R,A
                    case 0x57: c->a = c->i; c->f = _zf_sziff2_flags(c, c->i); break; // LD A,I
                    case 0x5F: c->a = c->r; c->f = _zf_sziff2_flags(c, c->r); break; // LD A,R
                    case 0x67: { // RRD
                        uint8_t v = _zf_rrd(c, _ZF_RD(c->hl));
                        _ZF_WR(c->hl, v);
                        c->wz = c->hl + 1;
                    } break;
                    case 0x6F: { // RLD
                        uint8_t v = _zf_rld(c, _ZF_RD(c->hl));
                        _ZF_WR(c->hl, v);
                        c->wz = c->hl + 1;
                    } break;
                    // --- block ops
                    case 0xA0: { // LDI
                        uint8_t v = _ZF_RD(c->hl); c->hl++;
                        _ZF_WR(c->de, v); c->de++;
                        _zf_ldi_ldd(c, v);
                    } break;
                    case 0xA8: { // LDD
                        uint8_t v = _ZF_RD(c->hl); c->hl--;
                        _ZF_WR(c->de, v); c->de--;
                        _zf_ldi_ldd(c, v);
                    } break;
                    case 0xA1: { // CPI
                        uint8_t v = _ZF_RD(c->hl); c->hl++;
                        c->wz++;
                        _zf_cpi_cpd(c, v);
                    } break;
                    case 0xA9: { // CPD
                        uint8_t v = _ZF_RD(c->hl); c->hl--;
                        c->wz--;
                        _zf_cpi_cpd(c, v);
                    } break;
                    case 0xA2: { // INI (I/O read: open bus 0xFF)
                        uint8_t v = 0xFF;
                        c->wz = c->bc + 1; c->b--;
                        _ZF_WR(c->hl, v); c->hl++;
                        _zf_ini_ind(c, v, (uint8_t)(c->c + 1));
                    } break;
                    case 0xAA: { // IND
                        uint8_t v = 0xFF;
                        c->wz = c->bc - 1; c->b--;
                        _ZF_WR(c->hl, v); c->hl--;
                        _zf_ini_ind(c, v, (uint8_t)(c->c - 1));
                    } break;
                    case 0xA3: { // OUTI
                        uint8_t v = _ZF_RD(c->hl); c->hl++;
                        c->b--;
                        c->wz = c->bc + 1;
                        _zf_outi_outd(c, v);
                    } break;
                    case 0xAB: { // OUTD
                        uint8_t v = _ZF_RD(c->hl); c->hl--;
                        c->b--;
                        c->wz = c->bc - 1;
                        _zf_outi_outd(c, v);
                    } break;
                    case 0xB0: { // LDIR (each iteration = one completed instruction)
                        uint8_t v = _ZF_RD(c->hl); c->hl++;
                        _ZF_WR(c->de, v); c->de++;
                        if (_zf_ldi_ldd(c, v)) { c->wz = --c->pc; --c->pc; }
                    } break;
                    case 0xB8: { // LDDR
                        uint8_t v = _ZF_RD(c->hl); c->hl--;
                        _ZF_WR(c->de, v); c->de--;
                        if (_zf_ldi_ldd(c, v)) { c->wz = --c->pc; --c->pc; }
                    } break;
                    case 0xB1: { // CPIR
                        uint8_t v = _ZF_RD(c->hl); c->hl++;
                        c->wz++;
                        if (_zf_cpi_cpd(c, v)) { c->wz = --c->pc; --c->pc; }
                    } break;
                    case 0xB9: { // CPDR
                        uint8_t v = _ZF_RD(c->hl); c->hl--;
                        c->wz--;
                        if (_zf_cpi_cpd(c, v)) { c->wz = --c->pc; --c->pc; }
                    } break;
                    case 0xB2: { // INIR
                        uint8_t v = 0xFF;
                        c->wz = c->bc + 1; c->b--;
                        _ZF_WR(c->hl, v); c->hl++;
                        if (_zf_ini_ind(c, v, (uint8_t)(c->c + 1))) { c->wz = --c->pc; --c->pc; }
                    } break;
                    case 0xBA: { // INDR
                        uint8_t v = 0xFF;
                        c->wz = c->bc - 1; c->b--;
                        _ZF_WR(c->hl, v); c->hl--;
                        if (_zf_ini_ind(c, v, (uint8_t)(c->c - 1))) { c->wz = --c->pc; --c->pc; }
                    } break;
                    case 0xB3: { // OTIR
                        uint8_t v = _ZF_RD(c->hl); c->hl++;
                        c->b--;
                        c->wz = c->bc + 1;
                        if (_zf_outi_outd(c, v)) { c->wz = --c->pc; --c->pc; }
                    } break;
                    case 0xBB: { // OTDR
                        uint8_t v = _ZF_RD(c->hl); c->hl--;
                        c->b--;
                        c->wz = c->bc - 1;
                        if (_zf_outi_outd(c, v)) { c->wz = --c->pc; --c->pc; }
                    } break;
                    default: break; // all other ED opcodes: 2-byte NOP
                }
            } break;
            case 0xEE: _zf_xor8(c, _ZF_IMM8()); break;
            case 0xEF: c->wz = 0x28; goto do_rst;
            case 0xF0: if (!(c->f & ZF_SF)) { goto do_ret; } break; // RET P
            case 0xF1: c->f = _ZF_RD(c->sp); c->sp++; c->a = _ZF_RD(c->sp); c->sp++; break; // POP AF
            case 0xF2: { // JP P,nn
                c->wzl = _ZF_IMM8(); c->wzh = _ZF_IMM8();
                if (!(c->f & ZF_SF)) { c->pc = c->wz; }
            } break;
            case 0xF3: c->iff1 = c->iff2 = 0; break; // DI
            case 0xF4: { // CALL P,nn
                c->wzl = _ZF_IMM8(); c->wzh = _ZF_IMM8();
                if (!(c->f & ZF_SF)) { goto do_call; }
            } break;
            case 0xF5: c->sp--; _ZF_WR(c->sp, c->a); c->sp--; _ZF_WR(c->sp, c->f); break; // PUSH AF
            case 0xF6: _zf_or8(c, _ZF_IMM8()); break;
            case 0xF7: c->wz = 0x30; goto do_rst;
            case 0xF8: if (c->f & ZF_SF) { goto do_ret; } break; // RET M
            case 0xF9: c->sp = c->hlx[idx].hl; break; // LD SP,HL
            case 0xFA: { // JP M,nn
                c->wzl = _ZF_IMM8(); c->wzh = _ZF_IMM8();
                if (c->f & ZF_SF) { c->pc = c->wz; }
            } break;
            case 0xFB: c->iff1 = c->iff2 = 1; break; // EI (no interrupts in sandbox)
            case 0xFC: { // CALL M,nn
                c->wzl = _ZF_IMM8(); c->wzh = _ZF_IMM8();
                if (c->f & ZF_SF) { goto do_call; }
            } break;
            case 0xFE: _zf_cp8(c, _ZF_IMM8()); break;
            case 0xFF: c->wz = 0x38; goto do_rst;
            // 0xDD/0xFD handled by prefix loop above
            default: break;

        do_ret:
            c->wzl = _ZF_RD(c->sp); c->sp++;
            c->wzh = _ZF_RD(c->sp); c->sp++;
            c->pc = c->wz;
            break;
        do_call:
            c->sp--; _ZF_WR(c->sp, c->pch);
            c->sp--; _ZF_WR(c->sp, c->pcl);
            c->pc = c->wz;
            break;
        do_rst:
            c->sp--; _ZF_WR(c->sp, c->pch);
            c->sp--; _ZF_WR(c->sp, c->pcl);
            c->pc = c->wz;
            break;
        }

        steps++;
        if (hlt) break;
    }

done:
    *out_e = c->e;
    *halted = hlt;
    return steps;
}

#undef _ZF_RD
#undef _ZF_WR
#undef _ZF_IMM8
#undef _ZF_RINC

#ifdef __cplusplus
} // extern "C"
#endif
