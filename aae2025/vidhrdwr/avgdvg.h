#ifndef __AVGDVG__
#define __AVGDVG__

extern UINT8* tempest_colorram;
extern UINT8* mhavoc_colorram;
extern UINT16* quantum_colorram;
extern UINT16* quantum_vectorram;

int avgdvg_done(void);
//WRITE_HANDLER( avgdvg_go_w );
//WRITE_HANDLER( avgdvg_reset_w );
//WRITE16_HANDLER( avgdvg_go_word_w );
//WRITE16_HANDLER( avgdvg_reset_word_w );

int avgdvg_done(void);
void avgdvg_go(int offset, int data);
void avgdvg_go_word_w(unsigned int offset, unsigned int data);
void avgdvg_reset(int offset, int data);
void avgdvg_reset_word_w(unsigned int offset, unsigned int data);
//int avgdvg_init(int vgType);

void avgdvg_reset_w(UINT32 address, UINT8 data, struct MemoryWriteByte* psMemWrite);
void avgdvg_go_w(UINT32 address, UINT8 data, struct MemoryWriteByte* psMemWrite);

/* Tempest and Quantum use this capability */
void avg_set_flip_x(int flip);
void avg_set_flip_y(int flip);

int dvg_start();
int avg_start();
int avg_start_tempest();
int avg_start_mhavoc();
int avg_start_alphaone();
int avg_start_starwars();
int avg_start_quantum();
int avg_start_bzone();
void dvg_stop();
void avg_stop();

/*
VIDEO_START( dvg );
VIDEO_START( avg );
VIDEO_START( avg_tempest );
VIDEO_START( avg_mhavoc );
VIDEO_START( avg_starwars );
VIDEO_START( avg_quantum );
VIDEO_START( avg_bzone );

MACHINE_RESET( avgdvg );
*/
#endif
