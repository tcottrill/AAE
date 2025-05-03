#include "aae_mame_driver.h"
#include "deftypes.h"

#ifndef AAE_AVG_H
#define AAE_AVG_H


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

#define VEC_SHIFT  16

extern UINT8* tempest_colorram;
extern UINT16 quantum_colorram[0x20];
extern UINT16* quantum_vectorram;

int vector_timer(int deltax, int deltay);
//void set_new_frame();
int avg_init(int type);
void AVG_RUN();
int avg_go();
int avg_clear();
int avg_check();


void advdvg_go_w(UINT32 address, UINT8 data, struct MemoryWriteByte* psMemWrite);
void avgdvg_go_word_w(UINT32 address, UINT16 data, struct MemoryWriteWord* psMemWrite);
void avgdvg_reset_w(UINT32 address, UINT8 data, struct MemoryWriteByte* psMemWrite);
void avgdvg_reset_word_w(UINT32 address, UINT16 data, struct MemoryWriteWord* pMemWrite);

int avg_start(void);
int avg_start_tempest(void);
int avg_start_mhavoc(void);
int avg_start_alphaone(void);
int avg_start_starwars(void);
int avg_start_quantum(void);
int avg_start_bzone(void);
int avg_start_redbaron(void);
void avg_stop(void);


#endif