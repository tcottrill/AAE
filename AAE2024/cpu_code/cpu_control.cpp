
//============================================================================
// AAE is a poorly written M.A.M.E (TM) derivitave based on early MAME 
// code, 0.29 through .90 mixed with code of my own. This emulator was 
// created solely for my amusement and learning and is provided only 
// as an archival experience. 
// 
// All MAME code used and abused in this emulator remains the copyright 
// of the dedicated people who spend countless hours creating it. All
// MAME code should be annotated as belonging to the MAME TEAM.
// 
// SOME CODE BELOW IS FROM MAME and COPYRIGHT the MAME TEAM.  
//============================================================================
//Note to self, replace this abomination with correct from other emulator next.

#include "cpu_control.h"

//#include "m6502.h"
//#include "mz80.h"
#include "starcpu.h"
#include "../aae_mame_driver.h"
#include "cpu_6809.h"
#include "cpu_i8080.h"

extern void get_ccpu_ticks();

static int cpu_configured = 0;
static int num_cpus = 0;
static int running_cpu = 0;
static int cyclecount[4];
static int addcycles[4];
static int reset_cpu_status[4];
////Time counters
static int hrzcounter = 0; //Only on CPU 0
static int hertzflip = 0;
static int tickcount[4];
static int eternaticks[4];
static int vid_tickcount;
////Interrupt Variables
static int enable_interrupt[4];
static int interrupt_vector[4] = { 0xff,0xff,0xff,0xff };
static int interrupt_count[4];
static int interrupt_pending[4];
static int framecnt = 0;
static int intcnt = 0;
//CONTEXTM6502 cont6502[4];

cpu_6809* m_cpu_6809[MAX_CPU];
cpu_i8080* m_cpu_i8080[MAX_CPU];


void initz80N(struct MemoryReadByte* read, struct MemoryWriteByte* write, struct z80PortRead* portread, struct z80PortWrite* portwrite, int cpunum)
{
	memset(&cMZ80[cpunum], 0, sizeof(struct mz80context));
	cMZ80[cpunum].z80Base = GI[cpunum];
	cMZ80[cpunum].z80IoRead = portread;
	cMZ80[cpunum].z80IoWrite = portwrite;
	cMZ80[cpunum].z80MemRead = read;
	cMZ80[cpunum].z80MemWrite = write;
	cMZ80[cpunum].z80intAddr = 0x38;
	cMZ80[cpunum].z80nmiAddr = 0x66;
	mz80SetContext(&cMZ80[cpunum]);
	mz80reset();
	mz80GetContext(&cMZ80[cpunum]);

	//CONTEXTMZ80 *temp;

	//cMZ80[cpunum] = malloc(mz80GetContextSize());

	//temp = cMZ80[cpunum];
	//memset( &temp, 0, mz80GetContextSize());

   // cMZ80[cpunum]->z80Base=GI[cpunum];
	//cMZ80[cpunum]->z80IoRead=portread;
	//cMZ80[cpunum]->z80IoWrite=portwrite;
	//cMZ80[cpunum]->z80MemRead=read;
	//cMZ80[cpunum]->z80MemWrite=write;
	//mz80SetContext(cMZ80[cpunum]);
	//mz80reset();
	//mz80GetContext(cMZ80[cpunum]);
}

void initz80(struct MemoryReadByte* read, struct MemoryWriteByte* write, struct z80PortRead* portread, struct z80PortWrite* portwrite, int cpunum)
{
	/*
	memset(&z80, 0, sizeof(struct mz80context));
	z80.z80Base=GI[cpunum];
	z80.z80IoRead=portread;
	z80.z80IoWrite=portwrite;
	z80.z80MemRead=read;
	z80.z80MemWrite=write;
	mz80SetContext(&z80);
	mz80reset();
	mz80GetContext(&z80);
	z80.z80intAddr=0x38;
	z80.z80nmiAddr=0x66;
	mz80SetContext(&z80);
	mz80reset();
	mz80GetContext(&z80);
	*/
}

void init8080(struct MemoryReadByte* read, struct MemoryWriteByte* write, struct z80PortRead* portread, struct z80PortWrite* portwrite, int cpunum)
{
	m_cpu_i8080[cpunum] = new cpu_i8080(GI[cpunum],
		read,
		write,
		portread,
		portwrite,
		0xffff);
	m_cpu_i8080[cpunum]->reset();
}

void init6502Z(struct MemoryReadByte* read, struct MemoryWriteByte* write, int cpunum)
{
	CONTEXTM6502* temp;

	wrlog("Configuring CPU");

	c6502[cpunum] = (CONTEXTM6502*)malloc(m6502zpGetContextSize());
	temp = c6502[cpunum];
	memset(&temp, 0, m6502zpGetContextSize());
	c6502[cpunum]->m6502Base = GI[cpunum];
	c6502[cpunum]->m6502MemoryRead = read;
	c6502[cpunum]->m6502MemoryWrite = write;
	m6502zpSetContext(c6502[cpunum]);
	m6502zpreset();

	m6502zpGetContext(c6502[cpunum]);
	wrlog("Finished Configuring CPU");
}

void init6502(struct MemoryReadByte* read, struct MemoryWriteByte* write, int cpunum)
{
	CONTEXTM6502* temp;

	wrlog("Configuring CPU");

	c6502[cpunum] = (CONTEXTM6502*)malloc(m6502GetContextSize());
	temp = c6502[cpunum];
	memset(&temp, 0, m6502GetContextSize());
	c6502[cpunum]->m6502Base = GI[cpunum];
	c6502[cpunum]->m6502MemoryRead = read;
	c6502[cpunum]->m6502MemoryWrite = write;
	m6502SetContext(c6502[cpunum]);
	m6502reset();
	m6502GetContext(c6502[cpunum]);
	wrlog("Finished Configuring CPU");
}

void init_6809(struct MemoryReadByte* read, struct MemoryWriteByte* write, int cpunum)
{
	wrlog("Start Configuring CPU %d", cpunum);
	m_cpu_6809[cpunum] = new cpu_6809(GI[cpunum], read, write, 0xffff, cpunum);
	m_cpu_6809[cpunum]->reset6809();
	wrlog("Finished Configuring CPU %d", cpunum);
}

void init68k(struct STARSCREAM_PROGRAMREGION* fetch, struct STARSCREAM_DATAREGION* readbyte, struct STARSCREAM_DATAREGION* readword, struct STARSCREAM_DATAREGION* writebyte, struct STARSCREAM_DATAREGION* writeword)
{
	s68000init();
	s68000context.fetch = fetch;
	s68000context.s_fetch = fetch;
	s68000context.u_fetch = fetch;
	s68000context.s_readbyte = readbyte;
	s68000context.u_readbyte = readbyte;
	s68000context.s_readword = readword;
	s68000context.u_readword = readword;
	s68000context.s_writebyte = writebyte;
	s68000context.u_writebyte = writebyte;
	s68000context.s_writeword = writeword;
	s68000context.u_writeword = writeword;
	s68000SetContext(&s68000context);
	s68000reset();
	s68000exec(100);
	s68000reset();
	wrlog("68000 Initialized: Initial PC is %06X\n", s68000context.pc);
}

int return_tickcount(int reset)
{
	int val;

	switch (driver[gamenum].cpu_type[running_cpu])
	{
	case CPU_MZ80:

		break;

	case CPU_6502Z:
		if (reset) val = m6502zpGetElapsedTicks(0xff);
		else val = m6502zpGetElapsedTicks(0);
		break;
	}

	return val;
}

void add_hertz_ticks(int cpunum, int ticks)
{
	if (cpunum > 0) return;
	hrzcounter += ticks;
	//wrlog("HRTZ COunter %d",hrzcounter);
	if (hrzcounter > 100) { hrzcounter -= 100; hertzflip ^= 1; }
}

int get_hertz_counter()
{
	return hertzflip;
}

void add_eterna_ticks(int cpunum, int ticks)
{
	eternaticks[cpunum] += ticks;
	if (eternaticks[cpunum] > 0xffffff) eternaticks[cpunum] = 0;
}

int get_eterna_ticks(int cpunum)
{
	return eternaticks[cpunum];
}

int get_elapsed_ticks(int cpunum)
{
	return tickcount[cpunum];
}

int get_video_ticks(int reset)
{
	int v = 0;
	int temp = 0;
	static int startnumber = 0;

	if (reset == 0xff) //Reset Tickcount;
	{
		vid_tickcount = 0;
		switch (driver[gamenum].cpu_type[0])
		{
		case CPU_MZ80:  vid_tickcount -= mz80GetElapsedTicks(0); break;  //Make vid_tickcount a negative number to check for reset later;
		case CPU_6502Z: vid_tickcount -= m6502zpGetElapsedTicks(0); break;
		case CPU_6502:  vid_tickcount -= m6502GetElapsedTicks(0); break;
		case CPU_68000: vid_tickcount -= s68000controlOdometer(0); break;
		}
		return 0;
	}

	v = vid_tickcount;
	switch (driver[gamenum].cpu_type[0])
	{
	case CPU_MZ80:  temp = mz80GetElapsedTicks(0); break;  //Make vid_tickcount a negative number to check for reset later;
	case CPU_6502Z: temp = m6502zpGetElapsedTicks(0); break;
	case CPU_6502:  temp = m6502GetElapsedTicks(0); break;
	case CPU_68000: temp = s68000readOdometer(); break;
	}

	//if (temp <= cyclecount[running_cpu]) temp = cyclecount[running_cpu]-m6502zpGetElapsedTicks(0);
	//else wrlog("Video CYCLE Count ERROR occured, check code.");
	v += temp;
	return v;
}

void cpu_clear_pending_interrupts(int cpunum)
{
	interrupt_pending[cpunum] = 0;
}

void set_interrupt_vector(int data)
{
	int cpunum = running_cpu;
	if (interrupt_vector[cpunum] != data)
	{
		interrupt_vector[cpunum] = data;

		// make sure there are no queued interrupts
		cpu_clear_pending_interrupts(cpunum);
	}
}

void init_cpu_config()
{
	int x;

	num_cpus = 0;
	cpu_configured = 0;
	running_cpu = 0;

	//wrlog("Starting up cpu settings, defaults");

	for (x = 0; x < 4; x++)
	{
		if (driver[gamenum].cpu_type[x])  num_cpus++;
	}
	for (x = 0; x < num_cpus; x++)
	{
		cyclecount[x] = ((int)driver[gamenum].cpu_freq[x] / (int)driver[gamenum].fps) / (int)driver[gamenum].cpu_divisions[x];
	}
	for (x = 0; x < 4; x++)
	{
		addcycles[x] = 0;
		interrupt_count[x] = 0;
		interrupt_pending[x] = 0;
		enable_interrupt[x] = 1;
	}

	interrupt_vector[x] = 0xff;
	vid_tickcount = 0;//Initalize video tickcount;
	hertzflip = 0;
	wrlog("NUMBER OF CPU'S to RUN %d", num_cpus);

	//wrlog("Finished starting up cpu settings, defaults");
}

int get_current_cpu()
{
	return running_cpu;
}

void cpu_disable_interrupts(int cpunum, int val)
{
	enable_interrupt[cpunum] = val;
}

int cpu_getcycles(int reset) //Only returns cycles from current context cpu
{
	int ticks = 0;

	switch (driver[gamenum].cpu_type[running_cpu])
	{
	case CPU_MZ80:  ticks = mz80GetElapsedTicks(reset); break;
	case CPU_6502Z: ticks = m6502zpGetElapsedTicks(reset); break;
	case CPU_6502:  ticks = m6502GetElapsedTicks(reset); break;
	case CPU_68000: ticks = s68000controlOdometer(reset); break;
	}
	return ticks;
}

void cpu_setcontext(int cpunum)
{
	switch (driver[gamenum].cpu_type[cpunum])
	{
	case CPU_MZ80:  mz80SetContext(&cMZ80[running_cpu]); break;
	case CPU_6502Z: m6502zpSetContext(c6502[cpunum]); break;
	case CPU_6502:  m6502SetContext(c6502[cpunum]); break;
	case CPU_68000: s68000SetContext(&c68k[running_cpu]); break;
	}
}

void cpu_getcontext(int cpunum)
{
	switch (driver[gamenum].cpu_type[cpunum])
	{
	case CPU_MZ80:  mz80GetContext(&cMZ80[running_cpu]); break;
	case CPU_6502Z: m6502zpGetContext(c6502[cpunum]); break;
	case CPU_6502:  m6502GetContext(c6502[cpunum]); break;
	case CPU_68000: s68000GetContext(&c68k[running_cpu]); break;
	}
}

void cpu_do_interrupt(int int_type, int cpunum)
{
	if (enable_interrupt[cpunum] == 0) { wrlog("Interrupts Disabled"); return; }

	interrupt_count[cpunum]++;
	//wrlog("Interrupt count %d", interrupt_count[cpunum]);
	if (interrupt_count[cpunum] == driver[gamenum].cpu_intpass_per_frame[running_cpu])
	{
		intcnt++;
		// wrlog("Interrupt count %d", interrupt_count[cpunum]);
		switch (driver[gamenum].cpu_type[running_cpu])
		{
		case CPU_MZ80:
			if (int_type == INT_TYPE_NMI) {
				mz80nmi();
				//wrlog("NMI Taken");
			}
			else {
				mz80int(interrupt_vector[cpunum]);
				//wrlog("INT Taken");
			}
			break;

		case CPU_6502Z:
			if (int_type == INT_TYPE_NMI) {
				m6502zpnmi();
				//wrlog("NMI Taken");
			}
			else {
				m6502zpint(1);
				//wrlog("INT Taken");
			}
			break;
		case CPU_6502:
			if (int_type == INT_TYPE_NMI) {
				m6502nmi();
				//wrlog("NMI Taken");
			}
			else {
				m6502int(1);
				//wrlog("INT Taken");
			}
			break;

		case CPU_68000: s68000interrupt(driver[gamenum].cpu_int_type[running_cpu], -1);
			s68000flushInterrupts();

			break;
		}

		interrupt_count[cpunum] = 0;
	}
}

void exec_cpu()
{
	UINT32 dwResult = 0;

	switch (driver[gamenum].cpu_type[running_cpu])
	{
	case CPU_MZ80:   dwResult = mz80exec(cyclecount[running_cpu]); break;
	case CPU_6502Z:  dwResult = m6502zpexec(cyclecount[running_cpu]); break;
	case CPU_6502:   dwResult = m6502exec(cyclecount[running_cpu]); break;
	case CPU_68000:  dwResult = s68000exec(cyclecount[running_cpu]); break;
	}

	if (0x80000000 != dwResult)
	{
		wrlog("Invalid instruction at %.2x\n");
		exit(1);
	}
}

void run_cpus_to_cycles()
{
	UINT32 dwElapsedTicks = 0;
	UINT32 dwResult = 0;
	int  x;
	int cycles_ran = 0;

	int adj = 0;

	tickcount[0] = 0;
	tickcount[1] = 0;
	tickcount[2] = 0;
	tickcount[3] = 0;

	//wrlog("Starting cpu run %d",cyclecount[running_cpu]);
	//wrlog("Starting cpu run %d",cyclecount[running_cpu+1]);

	for (x = 0; x < driver[gamenum].cpu_divisions[0]; x++)
	{
		for (running_cpu = 0; running_cpu < num_cpus; running_cpu++)
		{
			if (num_cpus > 1) { cpu_setcontext(running_cpu); }

			dwElapsedTicks = cpu_getcycles(0xff);

			cpu_resetter(running_cpu); //Check for CPU Reset
			process_pending_interrupts(running_cpu); //Check and see if there is a pending interrupt request outstanding
			exec_cpu();
			cycles_ran = cpu_getcycles(0);
			tickcount[running_cpu] += cycles_ran; //Add cycles this pass to frame cycle counter;
			add_eterna_ticks(running_cpu, cycles_ran);

			if (running_cpu == 0)
			{
				if (vid_tickcount < 1) vid_tickcount = cycles_ran - vid_tickcount; //Play catchup after reset
				vid_tickcount += cycles_ran;
				add_hertz_ticks(0, cycles_ran);
			}

			if (driver[gamenum].int_cpu[running_cpu]) { driver[gamenum].int_cpu[running_cpu](); }
			else {
				if (driver[gamenum].cpu_int_type[running_cpu])//Is there an int to run?
				{
					// wrlog("WARNING --- CALLING STANDARD INTERRUPT HANDLER!!!!");
					cpu_do_interrupt(driver[gamenum].cpu_int_type[running_cpu], running_cpu);
				}
			}
			if (num_cpus > 1) { cpu_getcontext(running_cpu); }
		}
		//wrlog("Starting cpu run %d",cyclecount[running_cpu+1]);
	}

	framecnt++;
	if (framecnt == driver[gamenum].fps)
	{
		//wrlog("-------------------------------------------------INTERRUPTS PER FRAME %d",intcnt);
		framecnt = 0; intcnt = 0;
	}

	// wrlog("CPU CYCLES RAN %d CPU CYCLES REQUESTED %d", tickcount[0],driver[gamenum].cpu_freq[0]/driver[gamenum].fps);
}

void cpu_resetter(int cpunum)
{
	if (reset_cpu_status[cpunum])
	{
		wrlog("RESETTING CPU %d NOW --------------------------------------", cpunum);
		switch (driver[gamenum].cpu_type[running_cpu])
		{
		case CPU_MZ80:  mz80reset(); break;
		case CPU_6502Z: m6502zpreset(); break;
		case CPU_6502:  m6502reset(); break;
		case CPU_68000: s68000reset(); break;
		}

		tickcount[cpunum] = 0;
		interrupt_count[cpunum] = 0;
		reset_cpu_status[cpunum] = 0;
		if (cpunum == 0)vid_tickcount = 0;
		wrlog("RESETTING CPU %d NOW ------------------Reset CPU Status is %d--------------------", cpunum, reset_cpu_status[cpunum]);
	}
}

void cpu_needs_reset(int cpunum)
{
	wrlog("CPU RESET CALLED!!!!!!!!!!!!!!!!!!!!!!___________________________________!!!!!!!!!!!!!!!");
	reset_cpu_status[cpunum] = 1;
}

void cpu_clear_pending_int(int int_type, int cpunum)
{
	switch (driver[gamenum].cpu_type[running_cpu])
	{
	case CPU_MZ80:  mz80reset(); break;
	case CPU_6502Z: m6502zpreset(); break;
	case CPU_6502:  m6502reset(); break;
	}
}

void process_pending_interrupts(int cpunum)
{
	if (interrupt_pending[cpunum])
	{
		switch (driver[gamenum].cpu_type[cpunum])
		{
		case CPU_MZ80:
			if (interrupt_pending[cpunum] == INT_TYPE_NMI) {
				mz80nmi();
				wrlog("NMI Taken");
			}
			else {
				mz80int(1);
				wrlog("INT Taken");
			}
			break;

		case CPU_6502Z:
			if (interrupt_pending[cpunum] == INT_TYPE_NMI) {
				m6502zpnmi();
				// wrlog("Delayed NMI SET CPU #%d",cpunum);
			}
			else {
				m6502zpint(1);
				//wrlog("Delayed INT SET CPU #%d",cpunum);
			}
			break;

		case CPU_6502:
			if (interrupt_pending[cpunum] == INT_TYPE_NMI)
			{
				m6502nmi();
				wrlog("NMI Taken");
			}
			else {
				m6502int(1);
				wrlog("INT Taken");
			}
			break;
		case CPU_68000:
			if (interrupt_pending[cpunum])
			{
				m6502nmi();
				wrlog("NMI Taken");
			}
			break;
		}
		interrupt_pending[cpunum] = 0;
	}
}

void set_pending_interrupt(int int_type, int cpunum) //Interrrupt to execute on next cpu cycle
{
	interrupt_pending[cpunum] = int_type;
}

//TO DO - ADD CPU SPEED TO MAIN DRIVER and below. Also Multiple CPUs
int cpu_scale_by_cycles(int val)
{
	float temp;
	int k;
	int current = 0;
	int max;
	int clock = driver[gamenum].cpu_freq[0];

	switch (driver[gamenum].cpu_type[running_cpu])
	{
	case CPU_MZ80:  current = mz80GetElapsedTicks(0);
		current += tickcount[running_cpu];
		break;
	case CPU_6502Z: current = m6502zpGetElapsedTicks(0);
		current += tickcount[running_cpu];
		break;
	case CPU_6502: current = m6502GetElapsedTicks(0);
		current += tickcount[running_cpu];
		break;
	case CPU_68000: current = s68000readOdometer();
		current += tickcount[running_cpu];
		break;
		//case CPU_CCPU: current = get_ccpu_ticks();break;
	}

	max = clock / driver[gamenum].fps;
	// wrlog(" Clock  %d divided by FPS: %d is equal to value: %d",clock,driver[gamenum].fps,max);
	//k = val * (float)((float)current / (float) max); //BUFFER_SIZE  *

	temp = ((float)current / (float)max);
	if (temp > 1) temp = .95;

	//wrlog(" Current %d divided by MAX: %d is equal to value: %f",current,max,temp);

	temp = val * temp;
	k = temp;

	if (driver[gamenum].cpu_type[0] == CPU_CCPU) return val;

	return k;
}