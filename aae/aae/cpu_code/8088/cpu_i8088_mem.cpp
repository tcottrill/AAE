
// cpu_i8088_mem.cpp - memory and I/O access for the 8088 core, routed through
// AAE's Neil Bradley-style handler tables (MemoryReadByte / MemoryWriteByte /
// z80PortRead / z80PortWrite). This replaces Fake86's direct RAM[]/VGA access
// with the same handler-scanning scheme used by cpu_i8080 / cpu_i8085, extended
// to the 8088's 20-bit (1 MB) physical address space.

#include "cpu_i8088.h"
#include "sys_log.h"


#define I8088_ADDR_MASK 0xFFFFF /* 1 MB physical address wrap */

uint8_t cpu_i8088::read86(uint32_t addr32)
{
	addr32 &= I8088_ADDR_MASK;

	uint8_t temp = 0;
	bool handled = false;

	// Pointer to beginning of our read handler table
	MemoryReadByte* MemRead = memory_read;

	while (MemRead && MemRead->lowAddr != 0xffffffff)
	{
		if ((addr32 >= MemRead->lowAddr) && (addr32 <= MemRead->highAddr))
		{
			if (MemRead->memoryCall)
			{
				temp = MemRead->memoryCall(addr32 - MemRead->lowAddr, MemRead);
			}
			else
			{
				temp = *((uint8_t*)MemRead->pUserArea + (addr32 - MemRead->lowAddr));
			}
			handled = true;
			break;
		}
		++MemRead;
	}

	// Unhandled access: fall through to flat memory unless MAME-style blocking.
	if (!handled && !mmem && MEM)
	{
		temp = MEM[addr32];
	}
	if (!handled && mmem)
	{
		if (log_debug_rw) LOG_INFO("Warning! Unhandled Read at %05X", addr32);
	}

	return temp;
}

void cpu_i8088::write86(uint32_t addr32, uint8_t value)
{
	addr32 &= I8088_ADDR_MASK;

	bool handled = false;

	// Pointer to beginning of our write handler table
	MemoryWriteByte* MemWrite = memory_write;

	while (MemWrite && MemWrite->lowAddr != 0xffffffff)
	{
		if ((addr32 >= MemWrite->lowAddr) && (addr32 <= MemWrite->highAddr))
		{
			if (MemWrite->memoryCall)
			{
				MemWrite->memoryCall(addr32 - MemWrite->lowAddr, value, MemWrite);
			}
			else
			{
				*((uint8_t*)MemWrite->pUserArea + (addr32 - MemWrite->lowAddr)) = value;
			}
			handled = true;
			break;
		}
		++MemWrite;
	}

	// Unhandled access: fall through to flat memory unless MAME-style blocking.
	if (!handled && !mmem && MEM)
	{
		MEM[addr32] = (uint8_t)value;
	}
	if (!handled && mmem)
	{
		if (log_debug_rw) LOG_INFO("Warning! Unhandled Write at %05X data: %02X", addr32, value);
	}
}

uint16_t cpu_i8088::readw86(uint32_t addr32)
{
	return ((uint16_t)read86(addr32) | ((uint16_t)read86(addr32 + 1) << 8));
}

void cpu_i8088::writew86(uint32_t addr32, uint16_t value)
{
	write86(addr32, (uint8_t)value);
	write86(addr32 + 1, (uint8_t)(value >> 8));
}

// ---------------------------------------------------------------------------
// port I/O - scanned against the z80-style 16-bit port handler tables.
// ---------------------------------------------------------------------------
uint8_t cpu_i8088::portin(uint16_t portnum)
{
	struct z80PortRead* mr = z80IoRead;

	while (mr && mr->lowIoAddr != 0xffff)
	{
		if (portnum >= mr->lowIoAddr && portnum <= mr->highIoAddr)
		{
			return (uint8_t)mr->IOCall(portnum, mr);
		}
		mr++;
	}

	return 0;
}

uint16_t cpu_i8088::portin16(uint16_t portnum)
{
	return (uint16_t)portin(portnum) | ((uint16_t)portin(portnum + 1) << 8);
}

void cpu_i8088::portout(uint16_t portnum, uint8_t value)
{
	struct z80PortWrite* mr = z80IoWrite;

	while (mr && mr->lowIoAddr != 0xffff)
	{
		if (portnum >= mr->lowIoAddr && portnum <= mr->highIoAddr)
		{
			mr->IOCall(portnum, value, mr);
			return;
		}
		mr++;
	}
}

void cpu_i8088::portout16(uint16_t portnum, uint16_t value)
{
	portout(portnum, (uint8_t)value);
	portout(portnum + 1, (uint8_t)(value >> 8));
}
