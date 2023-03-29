/* Multi-6809 32 Bit emulator */

#include "types.h"

#ifndef	_M6809_H_
#define	_M6809_H_

/* Some bitwise defines for the m6809GetInfo call */

#define		MULTI_CPU			0x00010000	/* Set if multi CPU support */
#define		TIMING_INFO			0x00020000	/* Set if timing info present */
#define		SYNC				0x00040000	/* Set if SYNC support present */
#define		BOUNDS_CHECK		        0x00080000	/* Set if bounds checking on */
#define		EXEC_METHOD			0x00100000	/* Set if we're counting cycles */

struct m6809MemoryWrite
{
	UINT32 lowAddr;
	UINT32 highAddr;
	UINT16 (*memoryCall)(UINT16, UINT8);
	void *pUserArea;
};	

struct m6809MemoryRead
{
	UINT32 lowAddr;
	UINT32 highAddr;
	UINT16 (*memoryCall)(UINT16);
	void *pUserArea;
};	

extern struct m6809MemoryRead *m6809MemoryRead;
extern struct m6809MemoryWrite *m6809MemoryWrite;

/* WARNING: Do not use a sizeof() operator on this structure. The assembly
 * code will pack other context relevant data after this space. Use the
 * GetContextSize() call to obtain the specifics of the context.
 *
 * Creating a context is not necessary if you are only running a single CPU
 * emulation.
 */

struct m6809context
{
	UINT8 *m6809Base;				/* 32 Bit pointer to base memory address */

	/* 32 Bit pointers to user trap address structure array */

	struct m6809MemoryRead *m6809MemoryRead;
	struct m6809MemoryWrite *m6809MemoryWrite;

	/* This context's register dump */

	UINT16 m6809pc;			/* Program counter */
	UINT16 m6809u;				/* U stack pointer */
	UINT16 m6809s;				/* S stack pointer */
	UINT16 m6809x;				/* X register */
	UINT16 m6809y;				/* Y register */
	UINT8 m6809b;				/* B register */
	UINT8 m6809a;				/* A register */
	UINT8 m6809cc;				/* Status flags */
	UINT8 m6809dp;				/* Direct page register */
	UINT8 inNmi;				/* Non-zero if we're in an NMI */
};
	
typedef struct m6809context CONTEXTM6809;

extern void m6809SetContext(CONTEXTM6809 *);
extern void m6809GetContext(CONTEXTM6809 *);
extern UINT32 m6809GetContextSize(void);
extern UINT32 m6809exec(UINT32);
extern UINT32 m6809nmi(void);
extern UINT32 m6809int(UINT32);
extern void m6809reset(void);

extern UINT8 *m6809Base;

#endif // _M6809_H_

