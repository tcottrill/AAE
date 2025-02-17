/**********************************************************************************************

	 TMS5220 interface

	 Written for MAME by Frank Palazzolo
	 With help from Neill Corlett
	 Additional tweaking by Aaron Giles

***********************************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "tms5220.h"
#include "5220intf.h"
#include "aae_mame_driver.h"
#include "samples.h"

//Oh this is stupid messy
#include "mhavoc.h"

/* these describe the current state of the output buffer */
#define MIN_SLICE 100	/* minimum update step for TMS5220 */
static int sample_pos;
static int buffer_len;
static int emulation_rate;
static unsigned char* buffer;

static struct TMS5220interface* intfa;
static int channel;

/* static function prototypes */
static void tms5220_update(int force);

/**********************************************************************************************

	 tms5220_sh_start -- allocate buffers and reset the 5220

***********************************************************************************************/

int tms5220_sh_start(struct TMS5220interface* iinterface)
{
	intfa = iinterface;

	buffer_len = intfa->clock / 80 / driver[gamenum].fps;
	emulation_rate = buffer_len * driver[gamenum].fps;
	
	sample_pos = 0;
	/* allocate the buffer */
	if ((buffer = (unsigned char*)malloc(buffer_len)) == 0)
		return 1;
	memset(buffer, 0x80, buffer_len);
	//stream2 = play_audio_stream(buffer_len, 8, FALSE, emulation_rate, config.pokeyvol, 128); //450  13500
	aae_stream_init(1, emulation_rate, buffer_len, 255);
	/* reset the 5220 */
	tms5220_reset();
	tms5220_set_irq(iinterface->irq);

	/* request a sound channel */
	//channel = get_play_channels (1);

	wrlog("TMS 5250 Init completed");

	channel = 1;
	return 0;
}

/**********************************************************************************************

	 tms5220_sh_stop -- free buffers

***********************************************************************************************/

void tms5220_sh_stop(void)
{
	if (buffer)
		free(buffer);
	buffer = 0;
}

/**********************************************************************************************

	 tms5220_sh_update -- update the sound chip

***********************************************************************************************/

void tms5220_sh_update(void)
{
	//if (Machine->sample_rate == 0) return;

	/* finish filling the buffer if there's still more to go */
	if (sample_pos < buffer_len)
		tms5220_process(buffer + sample_pos, buffer_len - sample_pos);
	sample_pos = 0;

	/* play this sample */
	//aae_play_streamed_sample(1, buffer, buffer_len, emulation_rate, intfa->volume);
	aae_play_streamed_sample(1, buffer, buffer_len, emulation_rate, 255);//intfa->volume);
}

/**********************************************************************************************

	 tms5220_data_w -- write data to the sound chip

***********************************************************************************************/

void tms5220_data_w(int offset, int data)
{
	/* bring up to date first */
	tms5220_update(0);
	tms5220_data_write(data);
}

/**********************************************************************************************

	 tms5220_status_r -- read status from the sound chip

***********************************************************************************************/

int tms5220_status_r(int offset)
{
	/* bring up to date first */
	tms5220_update(1);
	return tms5220_status_read();
}

/**********************************************************************************************

	 tms5220_ready_r -- return the not ready status from the sound chip

***********************************************************************************************/

int tms5220_ready_r(void)
{
	/* bring up to date first */
	tms5220_update(0);
	return tms5220_ready_read();
}

/**********************************************************************************************

	 tms5220_int_r -- return the int status from the sound chip

***********************************************************************************************/

int tms5220_int_r(void)
{
	/* bring up to date first */
	tms5220_update(0);
	return tms5220_int_read();
}

/**********************************************************************************************

	 tms5220_update -- update the sound chip so that it is in sync with CPU execution

***********************************************************************************************/

static void tms5220_update(int force)
{
	int totcycles, leftcycles, newpos;

	//newpos = cpu_scale_by_cycles(buffer_len, intfa->clock);
	
	newpos = cpu_scale_by_cycles(buffer_len, intfa->clock);
	//WRLOG("NEW TMS Position here is %d", newpos);
	if (newpos - sample_pos < MIN_SLICE && !force)
		return;
	tms5220_process(buffer + sample_pos, newpos - sample_pos);

	sample_pos = newpos;
	
}