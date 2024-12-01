
#include "m68k.h"
#include "mem68k.h"
#include "deftypes.h"


UINT8 ext_rom_bank = 0;
UINT8 ext_rom[0x400000];

UINT8 cpua_program_rom[0x40000];
UINT8 cpua_work_ram[0x40000];
UINT8 cpub_work_ram[0x40000];

int cpub_running = 0;
int cpu_which = 0;
int cpu_context_size;
UINT8* cpu_context[2];


unsigned int m68k_read_bus_8(unsigned int address)
{
    return 0;
}

unsigned int m68k_read_bus_16(unsigned int address)
{
    return 0;
}

void m68k_unused_w(unsigned int address, unsigned int value)
{
//error("Unused %08X = %08X (%08X)\n", address, value, Turbo68KReadPC());
}

void m68k_unused_8_w(unsigned int address, unsigned int value)
{
//error("Unused %08X = %02X (%08X)\n", address, value, Turbo68KReadPC());
}

void m68k_unused_16_w(unsigned int address, unsigned int value)
{
//error("Unused %08X = %04X (%08X)\n", address, value, Turbo68KReadPC());
}

/*
    Functions to handle memory accesses which cause the Genesis to halt
    either temporarily (press RESET button to restart) or unrecoverably
    (cycle power to restart).
*/

void m68k_lockup_w_8(unsigned int address, unsigned int value)
{
    m68k_end_timeslice();
}

void m68k_lockup_w_16(unsigned int address, unsigned int value)
{
    m68k_end_timeslice();
}

unsigned int m68k_lockup_r_8(unsigned int address)
{
    m68k_end_timeslice();
    return -1;
}

unsigned int m68k_lockup_r_16(unsigned int address)
{
     m68k_end_timeslice();
    return -1;
}

/*--------------------------------------------------------------------------*/
/* 68000 memory handlers                                                    */
/*--------------------------------------------------------------------------*/

unsigned int m68k_read_memory_8(unsigned int address)
{
    switch ((address >> 21) & 7)
    {
    case 0: break;
    }

    return -1;
}


unsigned int m68k_read_memory_16(unsigned int address)
{
    switch((address >> 21) & 7)
    {

    case 0: break;/* ROM */
             
    }

    return (0xA5A5);
}


unsigned int m68k_read_memory_32(unsigned int address)
{
    /* Split into 2 reads */
    return (m68k_read_memory_16(address + 0) << 16 | m68k_read_memory_16(address + 2));
}


void m68k_write_memory_8(unsigned int address, unsigned int value)
{
    switch((address >> 21) & 7)
    {
        case 0: /* ROM */
        case 1: /* ROM */
            m68k_unused_8_w(address, value);
            return;

        case 2: /* Unused */
        case 3:           
            m68k_unused_8_w(address, value);
            return;

        case 4: /* Unused */
            m68k_lockup_w_8(address, value);
            return;
    }
}




void m68k_write_memory_16(unsigned int address, unsigned int value)
{
    switch((address >> 21) & 7)
    {
    case 0: break; /* ROM */
       
    }
}


void m68k_write_memory_32(unsigned int address, unsigned int value)
{
    /* Split into 2 writes */
    m68k_write_memory_16(address, (value >> 16) & 0xFFFF);
    m68k_write_memory_16(address + 2, value & 0xFFFF);
}

void m68kcpu_init()
{
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    
    cpu_context_size = m68k_context_size();
    cpu_context[0] = malloc(cpu_context_size);
    m68k_get_context(cpu_context[0]);
    m68k_set_context(cpu_context[0]);
    m68k_pulse_reset();

}

