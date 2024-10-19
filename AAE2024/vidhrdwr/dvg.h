#pragma once
#include "deftypes.h"


int dvg_init();

void dvg_update();

void dvg_end();

void dvg_clr_busy(int dummy);

int  dvg_done(void);
void dvg_go(int offset, int data);
void dvg_reset(int offset, int data);

void dvg_reset_w(UINT32 address, UINT8 data, struct MemoryWriteByte* psMemWrite);
void dvg_go_w(UINT32 address, UINT8 data, struct MemoryWriteByte* psMemWrite);