/*** m6809: Portable 6809 emulator ******************************************

	Copyright (C) John Butler 1997

	References:

		6809 Simulator V09, By L.C. Benschop, Eidnhoven The Netherlands.

		m6809: Portable 6809 emulator, DS (6809 code in MAME, derived from
			the 6809 Simulator V09)

		6809 Microcomputer Programming & Interfacing with Experiments"
			by Andrew C. Staugaard, Jr.; Howard W. Sams & Co., Inc.

	System dependencies:	word must be 16 bit unsigned int
							byte must be 8 bit unsigned int
							long must be more than 16 bits
							arrays up to 65536 bytes must be supported
							machine must be twos complement

*****************************************************************************/

//
//This EARLY 6809 code from John Butler for M.A.M.E. (TM) has been highly modified to use as a class for multicpu support 
// for aae just for use in the starwars driver. 
// Eventually I would like to update it to a later revision for slapstic use for esb.
//

#ifndef _6809_H_
#define _6809_H_

#pragma once

#include "..\\deftypes.h"
#include <cstdint>

//#include <string>



#define INT_NONE  0			/* No interrupt required */
#define INT_IRQ	  1 		/* Standard IRQ interrupt */
#define INT_FIRQ  2			/* Fast IRQ */


class cpu_6809
{

public:

	/* 6809 Registers */
	typedef struct
	{
		uint16_t	pc;		/* Program counter */
		uint16_t	u, s;	/* Stack pointers */
		uint16_t	x, y;	/* Index registers */
		uint8_t		dp;		/* Direct Page register */
		uint8_t		a, b;	/* Accumulator */
		uint8_t		cc;     //Flags
	} m6809_Regs;

	uint16_t ppc;
	// Pointer to the game memory map (32 bit)
	uint8_t *MEM = nullptr;
	//Pointer to the handler structures
	MemoryReadByte  *memory_read = nullptr;
	MemoryWriteByte *memory_write = nullptr;

	//Constructors
	cpu_6809(uint8_t *mem, MemoryReadByte *read_mem, MemoryWriteByte *write_mem, uint16_t addr, int cpu_num);
	cpu_6809() {};

	//Destructor
	~cpu_6809() {};

	
	//Irq Handler
	void irq6809();
	//Int Handler
	void firq6809();
	//NMI Handler
	void nmi6809();
	// Sets all of the 6809 registers. 
	void reset6809();
	// Run the 6502 engine for specified number of clock cycles 
	int  exec6809(int timerTicks);
	
	//Get elapsed ticks / reset to zero
	int  get6809ticks(int reset);
	//Get /Set Register values
	void m6809_SetRegs(m6809_Regs* Regs);
	void m6809_GetRegs(m6809_Regs* Regs);
	//Get the PC
	uint16_t get_pc();
	//Force a jump to a different PC
	void set_pc(uint16_t newpc);
	//Get the previous PC
	uint16_t get_ppc();
	//Return the string value of the last instruction
	//std::string disam(uint8_t opcode);

	//For multicpu use
	int cpu_num;

	//Use Mame style memory handling, block read/writes that don't go through the handlers.
	void mame_memory_handling(bool s) { mmem = s; }
	void log_unhandled_rw(bool s) { log_debug_rw = s; }

	
	
	
	/* public globals */
	//int	m6809_IPeriod = 50000;
	//int	m6809_ICount = 50000;
	int	m6809_IRequest = INT_NONE;
	
private:
	
	//Memory handlers
	uint8_t get6809memory(uint16_t addr);
	void    put6809memory(uint16_t addr, uint8_t byte);

	// must be called first to initialise the 6809 instance, this is called in the primary constructor
	void init6809(uint16_t addrmaskval);

	// Function pointer arrays (static) 
	
	//These are signed. WHY?
	int32_t rd_slow(int32_t addr);
    void wr_slow(int32_t addr, int32_t v);
	int32_t rd_slow_wd(int32_t addr);
	void wr_slow_wd(int32_t addr, int32_t v);
	void wr_fast(int32_t addr, int32_t v);
	void wr_fast_wd(int32_t addr, int32_t v);
	int32_t  rd_fast(int32_t addr);
	int32_t  rd_fast_wd(int32_t addr);
	//These are unsigned. WHY?
	unsigned int M_RDMEM_WORD(uint32_t A);
	void M_WRMEM_WORD(uint32_t A, uint16_t V);
		uint8_t M_RDMEM(uint16_t A);
	void M_WRMEM(uint16_t A, uint8_t V);

	uint16_t addrmask;
	// internal registers 
	uint8_t opcode;
	int clockticks6809;
	//uint32_t dwElapsedTicks;

	// help variables 

	int clocktickstotal; //Runnning, resetable total of clockticks
	bool debug = 0;
	bool mmem = 0; //Use mame style memory handling, reject unhandled read/writes
	bool log_debug_rw = 0; //Log unhandled reads and writes
	bool irg_pending = 0;

	uint8_t cc, dpreg, areg, breg;
	uint16_t xreg, yreg, ureg, sreg, pcreg;
	uint16_t eaddr; /* effective address */

	void fetch_effective_address();

	 void abx(void);
	 void adca_di(void);
	 void adca_ex(void);
	 void adca_im(void);
	 void adca_ix(void);
	 void adcb_di(void);
	 void adcb_ex(void);
	 void adcb_im(void);
	 void adcb_ix(void);
	 void adda_di(void);
	 void adda_ex(void);
	 void adda_im(void);
	 void adda_ix(void);
	 void addb_di(void);
	 void addb_ex(void);
	 void addb_im(void);
	 void addb_ix(void);
	 void addd_di(void);
	 void addd_ex(void);
	 void addd_im(void);
	 void addd_ix(void);
	 void anda_di(void);
	 void anda_ex(void);
	 void anda_im(void);
	 void anda_ix(void);
	 void andb_di(void);
	 void andb_ex(void);
	 void andb_im(void);
	 void andb_ix(void);
	 void andcc(void);
	 void asl_di(void);
	 void asl_ex(void);
	 void asl_ix(void);
	 void asla(void);
	 void aslb(void);
	 void asr_di(void);
	 void asr_ex(void);
	 void asr_ix(void);
	 void asra(void);
	 void asrb(void);
	 void bcc(void);
	 void bcs(void);
	 void beq(void);
	 void bge(void);
	 void bgt(void);
	 void bhi(void);
	 void bita_di(void);
	 void bita_ex(void);
	 void bita_im(void);
	 void bita_ix(void);
	 void bitb_di(void);
	 void bitb_ex(void);
	 void bitb_im(void);
	 void bitb_ix(void);
	 void ble(void);
	 void bls(void);
	 void blt(void);
	 void bmi(void);
	 void bne(void);
	 void bpl(void);
	 void bra(void);
	 void brn(void);
	 void bsr(void);
	 void bvc(void);
	 void bvs(void);
	 void clr_di(void);
	 void clr_ex(void);
	 void clr_ix(void);
	 void clra(void);
	 void clrb(void);
	 void cmpa_di(void);
	 void cmpa_ex(void);
	 void cmpa_im(void);
	 void cmpa_ix(void);
	 void cmpb_di(void);
	 void cmpb_ex(void);
	 void cmpb_im(void);
	 void cmpb_ix(void);
	 void cmpd_di(void);
	 void cmpd_ex(void);
	 void cmpd_im(void);
	 void cmpd_ix(void);
	 void cmps_di(void);
	 void cmps_ex(void);
	 void cmps_im(void);
	 void cmps_ix(void);
	 void cmpu_di(void);
	 void cmpu_ex(void);
	 void cmpu_im(void);
	 void cmpu_ix(void);
	 void cmpx_di(void);
	 void cmpx_ex(void);
	 void cmpx_im(void);
	 void cmpx_ix(void);
	 void cmpy_di(void);
	 void cmpy_ex(void);
	 void cmpy_im(void);
	 void cmpy_ix(void);
	 void com_di(void);
	 void com_ex(void);
	 void com_ix(void);
	 void coma(void);
	 void comb(void);
	 void cwai(void);
	 void daa(void);
	 void dec_di(void);
	 void dec_ex(void);
	 void dec_ix(void);
	 void deca(void);
	 void decb(void);
	 void eora_di(void);
	 void eora_ex(void);
	 void eora_im(void);
	 void eora_ix(void);
	 void eorb_di(void);
	 void eorb_ex(void);
	 void eorb_im(void);
	 void eorb_ix(void);
	 void exg(void);
	 void illegal(void);
	 void inc_di(void);
	 void inc_ex(void);
	 void inc_ix(void);
	 void inca(void);
	 void incb(void);
	 void jmp_di(void);
	 void jmp_ex(void);
	 void jmp_ix(void);
	 void jsr_di(void);
	 void jsr_ex(void);
	 void jsr_ix(void);
	 void lbcc(void);
	 void lbcs(void);
	 void lbeq(void);
	 void lbge(void);
	 void lbgt(void);
	 void lbhi(void);
	 void lble(void);
	 void lbls(void);
	 void lblt(void);
	 void lbmi(void);
	 void lbne(void);
	 void lbpl(void);
	 void lbra(void);
	 void lbrn(void);
	 void lbsr(void);
	 void lbvc(void);
	 void lbvs(void);
	 void lda_di(void);
	 void lda_ex(void);
	 void lda_im(void);
	 void lda_ix(void);
	 void ldb_di(void);
	 void ldb_ex(void);
	 void ldb_im(void);
	 void ldb_ix(void);
	 void ldd_di(void);
	 void ldd_ex(void);
	 void ldd_im(void);
	 void ldd_ix(void);
	 void lds_di(void);
	 void lds_ex(void);
	 void lds_im(void);
	 void lds_ix(void);
	 void ldu_di(void);
	 void ldu_ex(void);
	 void ldu_im(void);
	 void ldu_ix(void);
	 void ldx_di(void);
	 void ldx_ex(void);
	 void ldx_im(void);
	 void ldx_ix(void);
	 void ldy_di(void);
	 void ldy_ex(void);
	 void ldy_im(void);
	 void ldy_ix(void);
	 void leas(void);
	 void leau(void);
	 void leax(void);
	 void leay(void);
	 void lsr_di(void);
	 void lsr_ex(void);
	 void lsr_ix(void);
	 void lsra(void);
	 void lsrb(void);
	 void mul(void);
	 void neg_di(void);
	 void neg_ex(void);
	 void neg_ix(void);
	 void nega(void);
	 void negb(void);
	 void nop(void);
	 void ora_di(void);
	 void ora_ex(void);
	 void ora_im(void);
	 void ora_ix(void);
	 void orb_di(void);
	 void orb_ex(void);
	 void orb_im(void);
	 void orb_ix(void);
	 void orcc(void);
	 void pshs(void);
	 void pshu(void);
	 void puls(void);
	 void pulu(void);
	 void rol_di(void);
	 void rol_ex(void);
	 void rol_ix(void);
	 void rola(void);
	 void rolb(void);
	 void ror_di(void);
	 void ror_ex(void);
	 void ror_ix(void);
	 void rora(void);
	 void rorb(void);
	 void rti(void);
	 void rts(void);
	 void sbca_di(void);
	 void sbca_ex(void);
	 void sbca_im(void);
	 void sbca_ix(void);
	 void sbcb_di(void);
	 void sbcb_ex(void);
	 void sbcb_im(void);
	 void sbcb_ix(void);
	 void sex(void);
	 void sta_di(void);
	 void sta_ex(void);
	 void sta_im(void);
	 void sta_ix(void);
	 void stb_di(void);
	 void stb_ex(void);
	 void stb_im(void);
	 void stb_ix(void);
	 void std_di(void);
	 void std_ex(void);
	 void std_im(void);
	 void std_ix(void);
	 void sts_di(void);
	 void sts_ex(void);
	 void sts_im(void);
	 void sts_ix(void);
	 void stu_di(void);
	 void stu_ex(void);
	 void stu_im(void);
	 void stu_ix(void);
	 void stx_di(void);
	 void stx_ex(void);
	 void stx_im(void);
	 void stx_ix(void);
	 void sty_di(void);
	 void sty_ex(void);
	 void sty_im(void);
	 void sty_ix(void);
	 void suba_di(void);
	 void suba_ex(void);
	 void suba_im(void);
	 void suba_ix(void);
	 void subb_di(void);
	 void subb_ex(void);
	 void subb_im(void);
	 void subb_ix(void);
	 void subd_di(void);
	 void subd_ex(void);
	 void subd_im(void);
	 void subd_ix(void);
	 void swi(void);
	 void swi2(void);
	 void swi3(void);
	 void sync(void);
	 void tfr(void);
	 void tst_di(void);
	 void tst_ex(void);
	 void tst_ix(void);
	 void tsta(void);
	 void tstb(void);

};

#endif 

