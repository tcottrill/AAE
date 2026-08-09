
// cpu_i8088_glue.cpp - AAE integration glue for the 8088 core.
//
// Holds the bits that bind the (Fake86-derived) instruction core to the AAE
// framework: construction, reset, externally-driven interrupts, tick
// accounting, and the In/Out convenience aliases. Kept separate from the CPU
// instruction logic (cpu_i8088.cpp) and the memory/IO access (cpu_i8088_mem.cpp).

#include "cpu_i8088.h"
#include "cpu_i8088_priv.h"


cpu_i8088::cpu_i8088(uint8_t* mem, MemoryReadByte* read_mem, MemoryWriteByte* write_mem,
                     z80PortRead* port_read, z80PortWrite* port_write, uint16_t /*addr*/)
{
	MEM          = mem;
	memory_read  = read_mem;
	memory_write = write_mem;
	z80IoRead    = port_read;
	z80IoWrite   = port_write;

	reset();
}

void cpu_i8088::reset()
{
	// Power-on/reset state: CS=FFFF, IP=0000 (first fetch at FFFF0h), flags and
	// segment registers cleared, not halted.
	reset86();
	segregs[reges] = 0;
	segregs[regss] = 0;
	segregs[regds] = 0;
	cf = pf = af = zf = sf = tf = ifl = df = of = 0;
	trap_toggle = 0;
}

// External maskable hardware interrupt (INTR). The interrupting device supplies
// the vector number (as the 8259 PIC would). Honours the interrupt-enable flag
// and wakes the CPU from HLT.
void cpu_i8088::interrupt(uint8_t vector)
{
	if (!ifl) return;
	hltstate = 0;
	intcall86(vector);
}

// Non-maskable interrupt (NMI) - always taken, vector 2, also wakes from HLT.
void cpu_i8088::interrupt_nmi()
{
	hltstate = 0;
	intcall86(2);
}

uint8_t cpu_i8088::In(uint16_t bPort)
{
	return portin(bPort);
}

void cpu_i8088::Out(uint16_t bPort, uint8_t bVal)
{
	portout(bPort, bVal);
}

int cpu_i8088::get_ticks(int reset)
{
	int tmp = clocktickstotal;
	if (reset)
	{
		clocktickstotal = 0;
	}
	return tmp;
}
