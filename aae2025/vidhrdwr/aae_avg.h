#include "aae_mame_driver.h"
#include "deftypes.h"

#ifndef AAE_AVG_H
#define AAE_AVG_H

#define VCTR 0
#define HALT 1
#define SVEC 2
#define STAT 3
#define CNTR 4
#define JSRL 5
#define RTSL 6
#define JMPL 7
#define SCAL 8


#define USE_DVG             1
#define USE_DVG_ASTEROID    2
#define USE_AVG_RBARON      3
#define USE_AVG_BZONE       4
#define USE_AVG             5
#define USE_AVG_TEMPEST     6
#define USE_AVG_MHAVOC      7
#define USE_AVG_SWARS       8
#define USE_AVG_QUANTUM     9
#define USE_AVG_ALPHAONE    10


//Please fix this. 
#define memrdwd_flip(address) ((Machine->memory_region[CPU0][pc+1]) | (Machine->memory_region[CPU0][pc]<<8))
#define memrdwd(address) ((Machine->memory_region[CPU0][pc]) | (Machine->memory_region[CPU0][pc+1]<<8)) /* LBO 062797 */
#define memrdwdf(address) ((vec_ram[pc]) | (vec_ram[pc+1]<<8))
#define MAXSTACK 8
#define VEC_SHIFT  16

int vector_timer(int deltax, int deltay);
//void set_new_frame();
void avg_init();
void AVG_RUN();
int avg_go();
int avg_clear();
int avg_check();
void set_bw_colors();

void advdvg_go_w(UINT32 address, UINT8 data, struct MemoryWriteByte* psMemWrite);
void avgdvg_go_word_w(UINT32 address, UINT16 data, struct MemoryWriteWord* psMemWrite);
void avgdvg_reset_w(UINT32 address, UINT8 data, struct MemoryWriteByte* psMemWrite);

//int dvg_start(void);
//int dvg_start_asteroid(void);
int avg_start(void);
int avg_start_tempest(void);
int avg_start_mhavoc(void);
int avg_start_alphaone(void);
int avg_start_starwars(void);
int avg_start_quantum(void);
int avg_start_bzone(void);
int avg_start_redbaron(void);
//void dvg_stop(void);
void avg_stop(void);


#endif