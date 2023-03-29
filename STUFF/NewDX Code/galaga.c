/* Galaga Emu */
#include "galaga.h"
#include "globals.h"
#include "samples.h"
#include "vector.h"
#include "glcode.h"
#include "input.h"
#include "cpuintfaae.h"
#include "earom.h"
#include "pokey.h"
#include "sndhrdw/pokyintf.h"
#include "fonts.h"
#include "raster.h"

	
	
	  
	

/*
void galaga_int()
{
  static int count=0;
  count ++;
  if (count==1) { if (!testsw ){m6502zpnmi();} count=0;} 
}
*/






void run_galagas()
{
	static int k=0;
	
	//if (KeyCheck(config.kreset))  {cpu_needs_reset(0);}
	if (getport(0) & 0x80) {cpu_disable_interrupts(0,0);} else {cpu_disable_interrupts(0,1);}
	if (psound && !paused) {pokey_sh_update();}	
	
    
	//log_it("Watchdog this frame %x",WATCHDOG);
	if (WATCHDOG==0) k++;
	if (k > 60) {cpu_needs_reset(0);k=0;}

    WATCHDOG=0;
}
/////////////////END MAIN LOOP/////////////////////////////////////////////




MEM_WRITE(GalagaWrite)
    //MEM_ADDR(0x2400, 0x2407, NoWrite)
	//MEM_ADDR(0x2000, 0x2007, NoWrite)
	MEM_ADDR(0x2c00, 0x2c0f, pokey_1_w)
	MEM_ADDR(0x3000, 0x3000, BWVectorGeneratorInternal)
	MEM_ADDR(0x3c03, 0x3c03, Thrust)
	MEM_ADDR(0x3c04, 0x3c04, DeluxeSwapRam)
	MEM_ADDR(0x3600, 0x3600, DxExplosions)
	MEM_ADDR(0x3400, 0x3400, Watchdog_reset_w)
	MEM_ADDR(0x3200, 0x323f, EaromWrite)
	MEM_ADDR(0x3a00, 0x3a00, EaromCtrl)
	MEM_ADDR(0x3c00, 0x3c01, DeluxeLedWrite)
	MEM_ADDR(0x4800, 0x7fff, NoWrite)
MEM_END

MEM_READ(GalagaRead)
	MEM_ADDR(0x2c40, 0x2c7f, EaromRead)
	MEM_ADDR(0x2c00, 0x2c0f, pokey_1_r)
	MEM_ADDR(0x2400, 0x2407, AstPIA2Read)
	MEM_ADDR(0x2000, 0x2007, AstPIA1Read)
MEM_END

/////////////////// MAIN() for program ///////////////////////////////////////////////////
void end_galaga()
{
 
}

int init_galaga(void)
{
    
	
	return 0;   
}

