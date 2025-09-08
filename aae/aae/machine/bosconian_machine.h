#pragma once

#ifndef BOSCO_MECH_H
#define BOSCO_MECH_H


void bosco_init_machine(void);
READ_HANDLER_NS(bosco_sharedram_r);
WRITE_HANDLER_NS(bosco_sharedram_w);


void bosco_nmi_generate_1(int param);
void bosco_nmi_generate_2(int param);

READ_HANDLER_NS(bosco_customio_r_1);
WRITE_HANDLER_NS(bosco_customio_w_1);

READ_HANDLER_NS(bosco_customio_data_r_1);
WRITE_HANDLER_NS(bosco_customio_data_w_1);

READ_HANDLER_NS(bosco_customio_data_2_r);
WRITE_HANDLER_NS(bosco_customio_data_w_2);

READ_HANDLER_NS(bosco_customio_2_r);
WRITE_HANDLER_NS(bosco_customio_w_2);

WRITE_HANDLER_NS(bosco_halt_w);
WRITE_HANDLER_NS(bosco_interrupt_enable_1_w);
int bosco_interrupt_1(void);
WRITE_HANDLER_NS(bosco_interrupt_enable_2_w);
int bosco_interrupt_2(void);
WRITE_HANDLER_NS(bosco_interrupt_enable_3_w);
int bosco_interrupt_3(void);

WRITE_HANDLER_NS(bosco_flipscreen_w);



#endif
