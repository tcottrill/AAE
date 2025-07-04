#ifndef tms5220_h
#define tms5220_h

struct TMS5220interface
{
    int clock;               // clock rate = 80 * output sample rate
    int volume;              // output volume
    void (*irq)(void);       // IRQ callback function
};

// Emulator control
void tms5220_reset(void);
void tms5220_set_irq(void (*func)(void));

// Data access
void tms5220_data_write(int data);
int  tms5220_status_read(void);
int  tms5220_ready_read(void);
int  tms5220_int_read(void);

// Sample processing
void tms5220_process(short* buffer, unsigned int size);

// Host interface control
int  tms5220_sh_start(struct TMS5220interface* intf);
void tms5220_sh_stop(void);
void tms5220_sh_update(void);

void tms5220_data_w(int offset, int data);
int  tms5220_status_r(int offset);
int  tms5220_ready_r(void);
int  tms5220_int_r(void);

#endif
