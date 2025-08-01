// aae_pokey.h
#ifndef POKEY_H
#define POKEY_H

#include <cstdint>
#include <cstddef>
#include "aae_mame_driver.h"

// Maximum number of POKEY chips
constexpr int MAXPOKEYS = 4;

// --- POKEY core register definitions ---
constexpr uint8_t AUDF1_C  = 0x00;
constexpr uint8_t AUDC1_C  = 0x01;
constexpr uint8_t AUDF2_C  = 0x02;
constexpr uint8_t AUDC2_C  = 0x03;
constexpr uint8_t AUDF3_C  = 0x04;
constexpr uint8_t AUDC3_C  = 0x05;
constexpr uint8_t AUDF4_C  = 0x06;
constexpr uint8_t AUDC4_C  = 0x07;
constexpr uint8_t AUDCTL_C = 0x08;
constexpr uint8_t STIMER_C = 0x09;
constexpr uint8_t SKREST_C = 0x0A;
constexpr uint8_t POTGO_C  = 0x0B;
constexpr uint8_t SEROUT_C = 0x0D;
constexpr uint8_t IRQEN_C  = 0x0E;
constexpr uint8_t SKCTL_C  = 0x0F;

constexpr uint8_t POT0_C   = 0x00;
constexpr uint8_t POT1_C   = 0x01;
constexpr uint8_t POT2_C   = 0x02;
constexpr uint8_t POT3_C   = 0x03;
constexpr uint8_t POT4_C   = 0x04;
constexpr uint8_t POT5_C   = 0x05;
constexpr uint8_t POT6_C   = 0x06;
constexpr uint8_t POT7_C   = 0x07;
constexpr uint8_t ALLPOT_C = 0x08;
constexpr uint8_t KBCODE_C = 0x09;
constexpr uint8_t RANDOM_C = 0x0A;
constexpr uint8_t SERIN_C  = 0x0D;
constexpr uint8_t IRQST_C  = 0x0E;
constexpr uint8_t SKSTAT_C = 0x0F;

// Core clock frequency options (not currently used)
//constexpr uint32_t FREQ_17_EXACT  = 1789790;
//constexpr uint32_t FREQ_17_APPROX = 1787520;

// -----------------------------------------------------------------------------
// POKEY core API
// -----------------------------------------------------------------------------
int  Pokey_sound_init(uint32_t freq17, uint16_t playback_freq, uint8_t num_pokeys);
void Update_pokey_sound(uint16_t addr, uint8_t val, uint8_t chip, uint8_t gain);
void Pokey_process(short* buffer, uint16_t n);
void pokey_sound_stop();

// -----------------------------------------------------------------------------
// Interface layer 
// -----------------------------------------------------------------------------

// Clip settings (no longer used)
constexpr int NO_CLIP   = 0;
constexpr int USE_CLIP  = 1;
constexpr int POKEY_DEFAULT_GAIN = 16;


// POKEY interface configuration
struct POKEYinterface {
    int num;    // number of pokey chips
    int clock;  // main clock
    int volume;
    int gain;
    int clip;
    // pot handlers
    int (*pot0_r[MAXPOKEYS]) (int offset);
    int (*pot1_r[MAXPOKEYS]) (int offset);
    int (*pot2_r[MAXPOKEYS]) (int offset);
    int (*pot3_r[MAXPOKEYS]) (int offset);
    int (*pot4_r[MAXPOKEYS]) (int offset);
    int (*pot5_r[MAXPOKEYS]) (int offset);
    int (*pot6_r[MAXPOKEYS]) (int offset);
    int (*pot7_r[MAXPOKEYS]) (int offset);
    int (*allpot_r[MAXPOKEYS]) (int offset);
};

// Start/stop
int  pokey_sh_start(POKEYinterface* intf);
void pokey_sh_stop(void);

// Low-level register access
int  Read_pokey_regs(uint16_t addr, uint8_t chip);
int  pokey1_r(int offset);
int  pokey2_r(int offset);
int  pokey3_r(int offset);
int  pokey4_r(int offset);
int  quad_pokey_r(int offset);

// Older Address write
void pokey1_w(int offset, int data);
void pokey2_w(int offset, int data);
void pokey3_w(int offset, int data);
void pokey4_w(int offset, int data);

// AAE callbacks (MemoryReadByte/WriteByte)
uint8_t pokey_1_r(uint32_t address, MemoryReadByte* psMemRead);
uint8_t pokey_2_r(uint32_t address, MemoryReadByte* psMemRead);
uint8_t pokey_3_r(uint32_t address, MemoryReadByte* psMemRead);
uint8_t pokey_4_r(uint32_t address, MemoryReadByte* psMemRead);
uint8_t quadpokey_r(uint32_t address, MemoryReadByte* psMemRead);

void pokey_1_w(uint32_t address, uint8_t data, MemoryWriteByte* psMemWrite);
void pokey_2_w(uint32_t address, uint8_t data, MemoryWriteByte* psMemWrite);
void pokey_3_w(uint32_t address, uint8_t data, MemoryWriteByte* psMemWrite);
void pokey_4_w(uint32_t address, uint8_t data, MemoryWriteByte* psMemWrite);
void quadpokey_w(uint32_t address, uint8_t data, MemoryWriteByte* psMemWrite);

// Update any pending samples
void pokey_sh_update(void);

void test_tempest_rng_pattern();

#endif // POKEY_H
