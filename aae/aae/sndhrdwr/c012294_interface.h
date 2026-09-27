// c012294_interface.h -- AAE engine adapter for the c012294 POKEY core.
//
// The chip itself lives in c012294.c / c012294.h (a byte-identical copy of
// the Atari 800 tree's portable core; never edit it here, fix it there and
// recopy). This header presents the same driver-facing API AAE has always
// had -- POKEYinterface, pokey_sh_start/stop/update, pokey_N_r/w, the quad
// POKEY decode, Read_pokey_regs -- so every existing driver compiles
// unchanged. The implementation in c012294_interface.cpp feeds the core machine
// time by catch-up from the scheduler's cycle counters; see the design in
// docs/superpowers/specs/2026-09-10-pokey-c012294-adapter-design.md.
#ifndef C012294_INTERFACE_H
#define C012294_INTERFACE_H

#include <cstdint>
#include "c012294.h"       // register offsets, AUDC/AUDCTL/IRQ/SKCTL/SKSTAT bits, ad_pokey API
#include "aae_mame_driver.h"

// ---------------------------------------------------------------------------
// Chip count limits
// ---------------------------------------------------------------------------
constexpr int MAXPOKEYS = 4;
constexpr int POKEY_MAX = 4;   // alias used inside the adapter

// ---------------------------------------------------------------------------
// POKEY write-register address constants (used by drivers directly)
// ---------------------------------------------------------------------------
constexpr uint8_t AUDF1_C = 0x00;
constexpr uint8_t AUDC1_C = 0x01;
constexpr uint8_t AUDF2_C = 0x02;
constexpr uint8_t AUDC2_C = 0x03;
constexpr uint8_t AUDF3_C = 0x04;
constexpr uint8_t AUDC3_C = 0x05;
constexpr uint8_t AUDF4_C = 0x06;
constexpr uint8_t AUDC4_C = 0x07;
constexpr uint8_t AUDCTL_C = 0x08;
constexpr uint8_t STIMER_C = 0x09;
constexpr uint8_t SKREST_C = 0x0A;
constexpr uint8_t POTGO_C = 0x0B;
constexpr uint8_t SEROUT_C = 0x0D;
constexpr uint8_t IRQEN_C = 0x0E;
constexpr uint8_t SKCTL_C = 0x0F;

// POKEY read-register address constants
constexpr uint8_t POT0_C = 0x00;
constexpr uint8_t POT1_C = 0x01;
constexpr uint8_t POT2_C = 0x02;
constexpr uint8_t POT3_C = 0x03;
constexpr uint8_t POT4_C = 0x04;
constexpr uint8_t POT5_C = 0x05;
constexpr uint8_t POT6_C = 0x06;
constexpr uint8_t POT7_C = 0x07;
constexpr uint8_t ALLPOT_C = 0x08;
constexpr uint8_t KBCODE_C = 0x09;
constexpr uint8_t RANDOM_C = 0x0A;
constexpr uint8_t SERIN_C = 0x0D;
constexpr uint8_t IRQST_C = 0x0E;
constexpr uint8_t SKSTAT_C = 0x0F;

// ---------------------------------------------------------------------------
// POKEYinterface
// The legacy fields come first so existing positional brace-inits keep
// compiling; the extended hooks are appended and default to null.
// ---------------------------------------------------------------------------
struct POKEYinterface {
	int num;
	int clock;
	int mixing_level[POKEY_MAX];
	int (*pot0_r[POKEY_MAX])(int);
	int (*pot1_r[POKEY_MAX])(int);
	int (*pot2_r[POKEY_MAX])(int);
	int (*pot3_r[POKEY_MAX])(int);
	int (*pot4_r[POKEY_MAX])(int);
	int (*pot5_r[POKEY_MAX])(int);
	int (*pot6_r[POKEY_MAX])(int);
	int (*pot7_r[POKEY_MAX])(int);
	int (*allpot_r[POKEY_MAX])(int);
	// --- extended (appended) ---
	void (*interrupt_cb[POKEY_MAX])(int mask);
	// Called at each keyboard scan code (0..63): bit 0 = KR1 active,
	// bit 1 = KR2 active. NULL means no closed contacts. Replaces the
	// unused instant-key event callback; debounce and IRQs belong to POKEY.
	int  (*key_lines_r[POKEY_MAX])(uint8_t code);
	int  (*serin_r[POKEY_MAX])(void);
	void (*serout_w[POKEY_MAX])(uint8_t data);
};

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
int  pokey_sh_start(POKEYinterface* intf);
void pokey_sh_stop(void);
void pokey_sh_update(void);
// Host output gate only: does not stop or reset the emulated chip.
void pokey_set_muted(int chip, bool muted);

// ---------------------------------------------------------------------------
// External clock (for a future cycle-by-cycle 6502 lockstep mode).
// While external clocking is on for a chip the adapter's catch-up does
// nothing for it; the caller pushes POKEY clocks with pokey_advance_external
// instead. No driver uses this yet.
// ---------------------------------------------------------------------------
void pokey_set_external_clock(int chip, bool on);
void pokey_advance_external(int chip, uint32_t pokey_clocks);

// ---------------------------------------------------------------------------
// Low-level register access (used by missile.cpp and others)
// ---------------------------------------------------------------------------
int Read_pokey_regs(uint16_t addr, uint8_t chip);

// ---------------------------------------------------------------------------
// Per-chip read/write trampolines (int-indexed API)
// ---------------------------------------------------------------------------
int  pokey1_r(int offset); int  pokey2_r(int offset);
int  pokey3_r(int offset); int  pokey4_r(int offset);
void pokey1_w(int offset, int data); void pokey2_w(int offset, int data);
void pokey3_w(int offset, int data); void pokey4_w(int offset, int data);
int  quad_pokey_r(int offset); void quad_pokey_w(int offset, int data);

// ---------------------------------------------------------------------------
// AAE MEM callbacks (MemoryReadByte / MemoryWriteByte)
// ---------------------------------------------------------------------------
uint8_t pokey_1_r(uint32_t address, struct MemoryReadByte*);
uint8_t pokey_2_r(uint32_t address, struct MemoryReadByte*);
uint8_t pokey_3_r(uint32_t address, struct MemoryReadByte*);
uint8_t pokey_4_r(uint32_t address, struct MemoryReadByte*);
uint8_t quadpokey_r(uint32_t address, struct MemoryReadByte*);
void pokey_1_w(uint32_t address, uint8_t data, struct MemoryWriteByte*);
void pokey_2_w(uint32_t address, uint8_t data, struct MemoryWriteByte*);
void pokey_3_w(uint32_t address, uint8_t data, struct MemoryWriteByte*);
void pokey_4_w(uint32_t address, uint8_t data, struct MemoryWriteByte*);
void quadpokey_w(uint32_t address, uint8_t data, struct MemoryWriteByte*);

#endif // C012294_INTERFACE_H
