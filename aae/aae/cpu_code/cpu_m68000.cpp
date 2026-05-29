// -----------------------------------------------------------------------------
// Motorola MC68000 CPU Core (wrapper) — implementation
//
// Wraps the Musashi engine. The class layer is single-instance; the file-static
// s_instance pointer below is how Musashi's extern "C" memory bridges find the
// live class. This is the ONLY .cpp in the project (outside cpu_code/68000/)
// that references any m68k_* symbol — that's the encapsulation invariant.
// -----------------------------------------------------------------------------

#include "cpu_m68000.h"
#include "./68000/m68k.h"
#include "cpu_control.h"   // READ_BYTE / READ_WORD / WRITE_BYTE / WRITE_WORD macros
#include "aae_mame_driver.h"

#include <cstdlib>         // abort

// ---------------------------------------------------------------------------
// Single live instance. Musashi keeps engine state in C globals; we're declared
// single-instance, so this static pointer bridges the engine's extern "C"
// callbacks to our class. The ctor enforces single-instance with abort().
// ---------------------------------------------------------------------------
static cpu_m68000* s_instance = nullptr;

// ---------------------------------------------------------------------------
// Construction
// Order is load-bearing: s_instance must be set BEFORE m68k_pulse_reset(),
// because reset fetches SP from $000000 and PC from $000004 via the memory
// bridges below, which deref s_instance.
// ---------------------------------------------------------------------------
cpu_m68000::cpu_m68000(MemoryReadByte* r8,  MemoryWriteByte* w8,
                       MemoryReadWord* r16, MemoryWriteWord* w16,
                       int cpu_num)
    : m_read8(r8), m_write8(w8), m_read16(r16), m_write16(w16),
      m_cpu_num(cpu_num)
{
    if (s_instance != nullptr) {
        LOG_INFO("FATAL: second cpu_m68000 created; design is single-instance");
        abort();
    }
    s_instance = this;
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_pulse_reset();
    LOG_INFO("cpu_m68000 init: PC:%08X  SP:%08X", GetPC(), GetSP());
}

cpu_m68000::~cpu_m68000()
{
    if (s_instance == this) s_instance = nullptr;
}

// ---------------------------------------------------------------------------
// Class method wrappers over Musashi
// ---------------------------------------------------------------------------
void cpu_m68000::reset()         { m68k_pulse_reset(); }
int  cpu_m68000::exec(int c)     { return m68k_execute(c); }
int  cpu_m68000::get_ticks(int)  { return 0; }   // Musashi has no mid-slice peek
void cpu_m68000::end_timeslice() { m68k_end_timeslice(); }

void cpu_m68000::irq_line(int level)
{
    if (level < 0) level = 0;
    if (level > 7) level = 7;
    m68k_set_irq(level);
}

void cpu_m68000::set_irq_ack_callback(int (*cb)(int))
{
    m68k_set_int_ack_callback(cb);
}

uint32_t cpu_m68000::GetPC() const { return m68k_get_reg(nullptr, M68K_REG_PC); }
uint32_t cpu_m68000::GetSP() const { return m68k_get_reg(nullptr, M68K_REG_SP); }
uint16_t cpu_m68000::GetSR() const { return (uint16_t)m68k_get_reg(nullptr, M68K_REG_SR); }

// ===========================================================================
// Free-function bridges Musashi calls.
// These have to be free functions with these exact names; Musashi references
// them by symbol (prototypes live in 68000/mem68k.h, included by m68kcpu.h).
// Moved verbatim from cpu_control.cpp and rewired through s_instance.
// ===========================================================================

/*--------------------------------------------------------------------------*/
/* Bus / unused / lockup stubs                                              */
/*--------------------------------------------------------------------------*/

unsigned int m68k_read_bus_8(unsigned int /*address*/)  { return 0; }
unsigned int m68k_read_bus_16(unsigned int /*address*/) { return 0; }

void m68k_unused_w   (unsigned int /*address*/, unsigned int /*value*/) {}
void m68k_unused_8_w (unsigned int /*address*/, unsigned int /*value*/) {}
void m68k_unused_16_w(unsigned int /*address*/, unsigned int /*value*/) {}

void m68k_lockup_w_8 (unsigned int /*address*/, unsigned int /*value*/) { m68k_end_timeslice(); }
void m68k_lockup_w_16(unsigned int /*address*/, unsigned int /*value*/) { m68k_end_timeslice(); }
unsigned int m68k_lockup_r_8 (unsigned int /*address*/) { m68k_end_timeslice(); return (unsigned int)-1; }
unsigned int m68k_lockup_r_16(unsigned int /*address*/) { m68k_end_timeslice(); return (unsigned int)-1; }

/*--------------------------------------------------------------------------*/
/* 68000 memory handlers                                                    */
/*--------------------------------------------------------------------------*/

unsigned int m68k_read_memory_8(unsigned int address)
{
    MemoryReadByte* MemRead = s_instance->read8_table();

    while (MemRead->lowAddr != 0xffffffff)
    {
        if (address >= MemRead->lowAddr && address <= MemRead->highAddr)
        {
            if (MemRead->memoryCall)
            {
                return (UINT8)(MemRead->memoryCall(address - MemRead->lowAddr, MemRead));
            }
            else
            {
                return (UINT8)READ_BYTE((unsigned char*)MemRead->pUserArea, address - MemRead->lowAddr);
            }
        }
        ++MemRead;
    }

    LOG_INFO("Unhandled Memory 8 Read: addr: %x", address);
    return 0;
}

void m68k_write_memory_8(unsigned int address, unsigned int value)
{
    MemoryWriteByte* MemWrite = s_instance->write8_table();

    while (MemWrite->lowAddr != 0xffffffff)
    {
        if (address >= MemWrite->lowAddr && address <= MemWrite->highAddr)
        {
            if (MemWrite->memoryCall)
            {
                MemWrite->memoryCall(address - MemWrite->lowAddr, (UINT8)value, MemWrite);
            }
            else
            {
                WRITE_BYTE((unsigned char*)MemWrite->pUserArea, address - MemWrite->lowAddr, (UINT8)value);
            }
        }
        MemWrite++;
    }
}

unsigned int m68k_read_memory_16(unsigned int address)
{
    MemoryReadWord* MemRead = s_instance->read16_table();

    while (MemRead->lowAddr != 0xffffffff)
    {
        if (address >= MemRead->lowAddr && address <= MemRead->highAddr)
        {
            if (MemRead->memoryCall)
            {
                return (UINT16)(MemRead->memoryCall(address - MemRead->lowAddr, MemRead));
            }
            else
            {
                return (UINT16)READ_WORD((unsigned char*)MemRead->pUserArea, address - MemRead->lowAddr);
            }
        }
        ++MemRead;
    }

    LOG_INFO("Unhandled Read 16: %x ", address);
    return 0;
}

void m68k_write_memory_16(unsigned int address, unsigned int value)
{
    MemoryWriteWord* MemWrite = s_instance->write16_table();

    while (MemWrite->lowAddr != 0xffffffff)
    {
        if (address >= MemWrite->lowAddr && address <= MemWrite->highAddr)
        {
            if (MemWrite->memoryCall)
            {
                MemWrite->memoryCall(address - MemWrite->lowAddr, (UINT16)value, MemWrite);
            }
            else
            {
                WRITE_WORD((unsigned char*)MemWrite->pUserArea, address - MemWrite->lowAddr, (UINT16)value);
            }
        }
        MemWrite++;
    }
}

unsigned int m68k_read_memory_32(unsigned int address)
{
    return (UINT32)(m68k_read_memory_16(address + 0) << 16 | m68k_read_memory_16(address + 2));
}

void m68k_write_memory_32(unsigned int address, unsigned int value)
{
    m68k_write_memory_16(address,     (value >> 16) & 0xFFFF);
    m68k_write_memory_16(address + 2,  value        & 0xFFFF);
}
