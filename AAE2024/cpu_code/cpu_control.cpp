
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
#include "starcpu.h"
#include "aae_mame_driver.h"
#include "cpu_6809.h"
#include "cpu_i8080.h"
#include "ccpu.h"
#include "timer.h"


static int cpu_configured = 0;
//static int num_cpus = 0;
//static int running_cpu = 0;
static int cyclecount[4];
static int addcycles[4];
static int reset_cpu_status[4];
//Time counters (To Be Removed)
static int hrzcounter = 0; //Only on CPU 0
static int hertzflip = 0;
static int tickcount[4];
static int eternaticks[4];
static int vid_tickcount;
//Interrupt Variables
static int enable_interrupt[4];
static int interrupt_vector[4] = { 0xff,0xff,0xff,0xff };
static int interrupt_count[4];
static int interrupt_pending[4];
static int framecnt = 0;
static int intcnt = 0;

//New for the antique style mame cpu scheduling that I added for Multicore CPU games.
//
static int cpurunning[MAX_CPU];
static int totalcycles[MAX_CPU];
static int ran_this_frame[MAX_CPU];
static int current_slice;
static int running;	/* number of cycles that the CPU emulation was requested to run */
/* (needed by cpu_getfcount) */
static int next_interrupt;	/* cycle count (relative to start of frame) when next interrupt will happen */
static int iloops[MAX_CPU];
//
// End of new code.

//New
static int active_cpu = 0;
static int totalcpu = 0;
static int watchdog_timer = 0;

// New CPU Contexts
cpu_6809* m_cpu_6809[MAX_CPU];
cpu_i8080* m_cpu_i8080[MAX_CPU];
cpu_z80* m_cpu_z80[MAX_CPU];
cpu_6502* m_cpu_6502[MAX_CPU];


// CPU Context
struct S68000CONTEXT c68k[MAX_CPU];


void init_z80(struct MemoryReadByte* read, struct MemoryWriteByte* write, struct z80PortRead* portread, struct z80PortWrite* portwrite, int cpunum)
{
	wrlog("Z80 Init Started");
	m_cpu_z80[cpunum] = new cpu_z80(GI[cpunum], read, write, portread, portwrite, 0xffff, cpunum);
	m_cpu_z80[cpunum]->mz80reset();
	wrlog("Z80 Init Ended");
}


void init_6809(struct MemoryReadByte* read, struct MemoryWriteByte* write, int cpunum)
{
	wrlog("Start Configuring CPU %d", cpunum);
	m_cpu_6809[cpunum] = new cpu_6809(GI[cpunum], read, write, 0xffff, cpunum);
	m_cpu_6809[cpunum]->reset6809();
	wrlog("Finished Configuring CPU %d", cpunum);
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

void init6502(struct MemoryReadByte* read, struct MemoryWriteByte* write, int cpunum)
{
	m_cpu_6502[cpunum] = new cpu_6502(GI[cpunum], read, write, 0xffff, cpunum);
	m_cpu_6502[cpunum]->reset6502();
	wrlog("Finished Configuring CPU");
}

void init6809(struct MemoryReadByte* read, struct MemoryWriteByte* write, int cpunum)
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

	switch (driver[gamenum].cpu_type[active_cpu])
	{
	case CPU_MZ80:
		m_cpu_z80[active_cpu]->mz80GetElapsedTicks(0xff);
		break;

	case CPU_M6502:
		if (reset) val = m_cpu_6502[active_cpu]->get6502ticks(0xff);
		else val = m_cpu_6502[active_cpu]->get6502ticks(0);
		break;


	case CPU_M6809:
		if (reset) val = m_cpu_6809[active_cpu]->get6809ticks(0xff);
		val = m_cpu_6809[active_cpu]->get6809ticks(0);
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
		case CPU_MZ80:  vid_tickcount -=  m_cpu_z80[active_cpu]->mz80GetElapsedTicks(0); break;  //Make vid_tickcount a negative number to check for reset later;
		case CPU_M6502: vid_tickcount -= m_cpu_6502[active_cpu]->get6502ticks(0); break;
		case CPU_68000: vid_tickcount -= s68000controlOdometer(0); break;
		case CPU_M6809: vid_tickcount -= m_cpu_6809[get_current_cpu()]->get6809ticks(0); break;
		}
		return 0;
	}

	v = vid_tickcount;
	switch (driver[gamenum].cpu_type[0])
	{
	case CPU_MZ80:  temp = m_cpu_z80[active_cpu]->mz80GetElapsedTicks(0); break;  //Make vid_tickcount a negative number to check for reset later;
	case CPU_M6502: temp = m_cpu_6502[active_cpu]->get6502ticks(0); break;
	case CPU_68000: temp = s68000readOdometer(); break;
	case CPU_M6809: temp = m_cpu_6809[get_current_cpu()]->get6809ticks(0); break;
	}

	//if (temp <= cyclecount[running_cpu]) temp = cyclecount[running_cpu]-m6502zpGetElapsedTicks(0);
	//else wrlog("Video CYCLE Count ERROR occured, check code.");
	v += temp;
	return v;
}


int cpu_getpc()
{
	//Run cycles depending on which cpu
	switch (driver[gamenum].cpu_type[active_cpu])
	{
	case CPU_8080:
		return m_cpu_i8080[active_cpu]->reg_PC;
		break;

	case CPU_MZ80:
		return m_cpu_z80[active_cpu]->GetPC();
		break;

	case CPU_M6502:
		return m_cpu_6502[active_cpu]->get_pc();
		break;

	case CPU_M6809:
		return m_cpu_6809[active_cpu]->get_pc();
		break;

	case CPU_68000:
		break;
	}
	return 0;
}


int get_current_cpu()
{
	return active_cpu;
}

int cpu_getcycles(int reset) //Only returns cycles from current context cpu
{
	int ticks = 0;

	switch (driver[gamenum].cpu_type[active_cpu])
	{
	case CPU_MZ80:  ticks = m_cpu_z80[active_cpu]->mz80GetElapsedTicks(reset); break;
	case CPU_M6502: ticks = m_cpu_6502[active_cpu]->get6502ticks(reset); break;
	case CPU_68000: ticks = s68000controlOdometer(reset); break;
	case CPU_M6809: ticks = m_cpu_6809[active_cpu]->get6809ticks(reset); break;
	}
	return ticks;
}

void cpu_setcontext(int cpunum)
{
	switch (driver[gamenum].cpu_type[cpunum])
	{
	//case CPU_MZ80:  mz80SetContext(&cMZ80[active_cpu]); break;
	//case CPU_M6502: m6502zpSetContext(c6502[cpunum]); break;
	case CPU_68000: s68000SetContext(&c68k[active_cpu]); break;
	case CPU_M6809: break;
	}
}

void cpu_getcontext(int cpunum)
{
	switch (driver[gamenum].cpu_type[cpunum])
	{
	//case CPU_MZ80:  mz80GetContext(&cMZ80[active_cpu]); break;
	//case CPU_M6502: m6502zpGetContext(c6502[cpunum]); break;
	case CPU_68000: s68000GetContext(&c68k[active_cpu]); break;
	case CPU_M6809: break;
	}
}


void cpu_disable_interrupts(int cpunum, int val)
{
	enable_interrupt[cpunum] = val;
}


void cpu_clear_pending_interrupts(int cpunum)
{
	interrupt_pending[cpunum] = 0;
}

void set_interrupt_vector(int data)
{
	int cpunum = get_current_cpu();
	if (interrupt_vector[cpunum] != data)
	{
		interrupt_vector[cpunum] = data;

		// make sure there are no queued interrupts
		cpu_clear_pending_interrupts(cpunum);
	}
}


void cpu_do_int_imm(int cpunum, int int_type)
{
	switch (driver[gamenum].cpu_type[cpunum])
	{
	case CPU_8080:
		if (int_type == INT_TYPE_NMI) {
		}
		else {
			m_cpu_i8080[cpunum]->interrupt(interrupt_vector[cpunum]);
		}
		break;

	case CPU_MZ80:
		if (int_type == INT_TYPE_NMI) {
			m_cpu_z80[cpunum]->mz80nmi();
		}
		else {
			m_cpu_z80[cpunum]->mz80int(interrupt_vector[cpunum]);
		}
		break;

	case CPU_M6502:
		if (int_type == INT_TYPE_NMI) {
			m_cpu_6502[cpunum]->nmi6502();
			//m6502zpnmi();
		}
		else {
			//m6502zpint(1);
			m_cpu_6502[cpunum]->irq6502();
		}
		break;

	case CPU_M6809:
		if (int_type == INT_TYPE_NMI) {
			m_cpu_6809[cpunum]->nmi6809();
		}
		else {
			m_cpu_6809[cpunum]->irq6809();
			//if (debug) wrlog("6809 IRQ Called on CPU %d", cpunum);
		}
		break;

	case CPU_68000: s68000interrupt(int_type, -1); //add interrupt num here
		s68000flushInterrupts();
		//wrlog("68000 IRQ Called on CPU %d", cpunum);
		break;
	}
}


void cpu_do_interrupt(int int_type, int cpunum)
{
	if (enable_interrupt[cpunum] == 0) { wrlog("Interrupts Disabled"); return; }

	interrupt_count[cpunum]++;
	//wrlog("Interrupt count %d", interrupt_count[cpunum]);
	if (interrupt_count[cpunum] == driver[gamenum].cpu_intpass_per_frame[active_cpu])
	{
		intcnt++;
		// wrlog("Interrupt count %d", interrupt_count[cpunum]);
		switch (driver[gamenum].cpu_type[active_cpu])
		{
		case CPU_MZ80:
			if (int_type == INT_TYPE_NMI) {
				m_cpu_z80[cpunum]->mz80nmi();
				//wrlog("NMI Taken");
			}
			else {
				m_cpu_z80[cpunum]->mz80int(interrupt_vector[cpunum]);
				//wrlog("INT Taken");
			}
			break;

		case CPU_M6502:
			if (int_type == INT_TYPE_NMI) {
				m_cpu_6502[cpunum]->nmi6502();
				//m6502zpnmi();
				//wrlog("NMI Taken");
			}
			else {
				m_cpu_6502[cpunum]->irq6502();
				//m6502zpint(1);
				//wrlog("INT Taken");
			}
			break;
		case CPU_M6809:
			if (int_type == INT_TYPE_NMI) {
				m_cpu_6809[cpunum]->nmi6809();
				//wrlog("NMI Taken");
			}
			else {
				m_cpu_6809[cpunum]->irq6809();
				//wrlog("INT Taken 6809");
			}
			break;
	
		case CPU_68000: s68000interrupt(driver[gamenum].cpu_int_type[active_cpu], -1);
			s68000flushInterrupts();
			//wrlog("INT Taken 68000");

			break;
		}

		interrupt_count[cpunum] = 0;
	}
}

void exec_cpu()
{
	UINT32 dwResult = 0;

	switch (driver[gamenum].cpu_type[get_current_cpu()])
	{
	case CPU_MZ80:   dwResult = m_cpu_z80[active_cpu]->mz80exec(cyclecount[active_cpu]); break;
	case CPU_M6502:  dwResult = m_cpu_6502[active_cpu]->exec6502(cyclecount[active_cpu]); break;
	case CPU_68000:  dwResult = s68000exec(cyclecount[active_cpu]); 
		//wrlog("68000 exec called");
		              break;
	case CPU_M6809:
			{   dwResult = m_cpu_6809[active_cpu]->exec6809(cyclecount[active_cpu]); break;	}
	}

	if (0x80000000 != dwResult)
	{
		wrlog("Invalid instruction at %.2x\n");
		exit(1);
	}
}

void run_cpus_to_cycles() // This is the jankiest code ever.
{
	UINT32 dwElapsedTicks = 0;
	UINT32 dwResult = 0;
	int  x;
	int cycles_ran = 0;
	
	tickcount[0] = 0;
	tickcount[1] = 0;
	tickcount[2] = 0;
	tickcount[3] = 0;

	//wrlog("Starting cpu run %d",cyclecount[active_cpu]);
	
	for (x = 0; x < driver[gamenum].cpu_divisions[0]; x++)
	{
		for (int i = 0; i < totalcpu; i++)
		{
			active_cpu = i;

			if (totalcpu > 1) { cpu_setcontext(i); }

			dwElapsedTicks = cpu_getcycles(0xff);

			cpu_resetter(i); //Check for CPU Reset
			process_pending_interrupts(i); //Check and see if there is a pending interrupt request outstanding
			exec_cpu();
			cycles_ran = cpu_getcycles(0);
			//timer_update(cycles_ran, active_cpu);

			tickcount[i] += cycles_ran; //Add cycles this pass to frame cycle counter;
			add_eterna_ticks(i, cycles_ran);

			if (i == 0)
			{
				if (vid_tickcount < 1) vid_tickcount = cycles_ran - vid_tickcount; //Play catchup after reset
				vid_tickcount += cycles_ran;
				add_hertz_ticks(0, cycles_ran);
			}

			if (driver[gamenum].int_cpu[i]) { driver[gamenum].int_cpu[i](); }
			else {
				if (driver[gamenum].cpu_int_type[i])//Is there an int to run?
				{
					// wrlog("WARNING --- CALLING STANDARD INTERRUPT HANDLER!!!!");
					cpu_do_interrupt(driver[gamenum].cpu_int_type[i], i);
				}
			}
			if (totalcpu > 1) { cpu_getcontext(i); }
		}
	}

	framecnt++;
	if (framecnt == driver[gamenum].fps)
	{
		//wrlog("-------------------------------------------------INTERRUPTS PER FRAME %d",intcnt);
		framecnt = 0; intcnt = 0;
	}

	 wrlog("CPU CYCLES RAN %d CPU CYCLES REQUESTED %d", tickcount[0],driver[gamenum].cpu_freq[0]/driver[gamenum].fps);
}

///////////////////////
// 
// DUPLICATE CPU CODE ADDED FOR MULTICPU SUPPORT BELOW
//
///////////////////////

//***************************************************************************
int cpu_getiloops(void)
{
	return iloops[active_cpu];
}

//TBD SOON AS POSSIBLE, add a check to make sure every scheduled interrupt per pass per cpu has been taken, if not take it at the end of the run.
int cpu_exec_now(int cpu, int cycles)
{
	int ticks = 0;

	//Run cycles depending on which cpu
	switch (driver[gamenum].cpu_type[cpu])
	{
	case CPU_MZ80:
		m_cpu_z80[cpu]->mz80exec(cycles);
		ticks = m_cpu_z80[cpu]->mz80GetElapsedTicks(0xff);
		break;

	case CPU_M6502:
		//m_cpu_6502[cpu]->exec6502(cycles);
		//ticks = m_cpu_6502[cpu]->get6502ticks(0xff);
		m_cpu_6502[cpu]->exec6502(cyclecount[cpu]);
		ticks = m_cpu_6502[active_cpu]->get6502ticks(0xff);
		break;

	case CPU_8080:
		m_cpu_i8080[cpu]->exec(cycles);
		ticks = m_cpu_i8080[cpu]->get_ticks(0xff);
		break;

	case CPU_M6809:
		m_cpu_6809[cpu]->exec6809(cycles);
		ticks = m_cpu_6809[cpu]->get6809ticks(0xff);
		break;

	case CPU_68000:
		s68000exec(cycles);
		ticks = s68000controlOdometer(0xff); break;
		break;

		//case CPU_CCPU: if (cpu_spinning) return;
		  //	ccpu_cycles = ccpu_execute(100);
		  //	break;
	}
	// Update the cyclecount and the interrupt timers.
	cyclecount[cpu] += ticks;
	timer_update(ticks, active_cpu);
	return ticks;
}

void cpu_run_mame(void)
{
	int ran, target;
	tickcount[0] = 0;
	tickcount[1] = 0;
	tickcount[2] = 0;
	tickcount[3] = 0;

	//wrlog("CPU RUN MAME CALLED");
	update_input_ports();	/* read keyboard & update the status of the input ports */

	for (active_cpu = 0; active_cpu < totalcpu; active_cpu++)
	{
		cpurunning[active_cpu] = 1;
		totalcycles[active_cpu] = 0;
		ran_this_frame[active_cpu] = 0;

		if (cpurunning[active_cpu])
			iloops[active_cpu] = driver[gamenum].cpu_intpass_per_frame[active_cpu] - 1;
		else
			iloops[active_cpu] = -1;

		int cycles = (driver[gamenum].cpu_freq[active_cpu] / driver[gamenum].fps) / driver[gamenum].cpu_intpass_per_frame[active_cpu];
		//wrlog("Cycles are %d, iloops are %d", cycles, iloops[active_cpu]);
	}

	for (current_slice = 0; current_slice < driver[gamenum].cpu_divisions[0]; current_slice++)
	{
		//wrlog("Current slice is %d", current_slice);

		for (active_cpu = 0; active_cpu < totalcpu; active_cpu++)
		{
			if (reset_cpu_status[active_cpu])
			{
				cpu_reset(active_cpu);
			}
			//wrlog("Current cpu is %d", active_cpu);
			if (cpurunning[active_cpu])
			{
				if (iloops[active_cpu] >= 0)
				{
					//wrlog("Current iloop %d", iloops[active_cpu]);
					if (totalcpu > 1) { cpu_setcontext(active_cpu); }

					target = (driver[gamenum].cpu_freq[active_cpu] / driver[gamenum].fps) * (current_slice + 1)
						/ driver[gamenum].cpu_divisions[active_cpu];
					
					//wrlog("Target is %d", target);

					next_interrupt = (driver[gamenum].cpu_freq[active_cpu]
						/ driver[gamenum].fps) * (driver[gamenum].cpu_intpass_per_frame[active_cpu] - iloops[active_cpu])
						/ driver[gamenum].cpu_intpass_per_frame[active_cpu];

					//wrlog("Next Int is %d", next_interrupt);
					while (ran_this_frame[active_cpu] < target)
					{
						if (target <= next_interrupt)
							running = target - ran_this_frame[active_cpu];
						else
							running = next_interrupt - ran_this_frame[active_cpu];

						ran = cpu_exec_now(active_cpu, running);
						
						// For temp compatibility  ////////////////////////////////////////////
						// I still don't have a good idea of what all these timers are doing :(
						tickcount[active_cpu] += ran;
						add_eterna_ticks(active_cpu, ran);
						if (active_cpu == 0)
						{
							if (vid_tickcount < 1) vid_tickcount = ran - vid_tickcount; //Play catchup after reset
							vid_tickcount += ran;
							add_hertz_ticks(0, ran);
						}
						/////////////////////////////////////////////////////////////////////////
						ran_this_frame[active_cpu] += ran;
						totalcycles[active_cpu] += ran;

						if (ran_this_frame[active_cpu] >= next_interrupt)
						{
							// Call the interrupt handler.
							 if (driver[gamenum].int_cpu[active_cpu]) 
							 { 
								 driver[gamenum].int_cpu[active_cpu](); 
							 }
							//(*Machine->drv->cpu[active_cpu].interrupt)(0);
							//cpu_cause_interrupt(active_cpu, 0); // Original Code
							iloops[active_cpu]--;

							next_interrupt = (driver[gamenum].cpu_freq[active_cpu]
								/ driver[gamenum].fps) * (driver[gamenum].cpu_intpass_per_frame[active_cpu] - iloops[active_cpu])
								/ driver[gamenum].cpu_intpass_per_frame[active_cpu];
							//wrlog("CPU %d, Next Interrupt: %d", active_cpu, next_interrupt);
						}
					}
					if (totalcpu > 1) { cpu_getcontext(active_cpu); }
				}
			}
		}
	}
}

///////////////////////
// 
// END OF DUPLICATE CPU CODE
//
///////////////////////

void cpu_resetter(int cpunum)
{
	if (reset_cpu_status[cpunum])
	{
		wrlog("RESETTING CPU %d NOW --------------------------------------", cpunum);
		switch (driver[gamenum].cpu_type[get_current_cpu()])
		{
		case CPU_MZ80:  m_cpu_z80[cpunum]->mz80reset(); break;
		case CPU_M6502: m_cpu_6502[cpunum]->reset6502(); break;
		case CPU_68000: s68000reset(); break;
		case CPU_M6809: m_cpu_6809[cpunum]->reset6809(); break;
		case CPU_CCPU: ccpu_reset(); break;
		}

		tickcount[cpunum] = 0;
		interrupt_count[cpunum] = 0;
		reset_cpu_status[cpunum] = 0;
		if (cpunum == 0)vid_tickcount = 0;
		wrlog("RESETTING CPU %d NOW ------------------Reset CPU Status is %d--------------------", cpunum, reset_cpu_status[cpunum]);
	}
}

void cpu_reset(int cpunum)
{
	wrlog("CPU RESET CALLED!!!!!!!!!!!!!!!!!!!!!!___________________________________!!!!!!!!!!!!!!!");
	reset_cpu_status[cpunum] = 1;
}


void cpu_reset_all()
{
	for (int x = 0; x < totalcpu; x++)
	{
		reset_cpu_status[x] = 1;
	}
}

void cpu_clear_pending_int(int int_type, int cpunum)
{
	switch (driver[gamenum].cpu_type[get_current_cpu()])
	{
	case CPU_MZ80:  m_cpu_z80[cpunum]->mz80ClearPendingInterrupt(); break;
	case CPU_M6502: m_cpu_6502[cpunum]->reset6502(); break;
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
				m_cpu_z80[cpunum]->mz80nmi();
				wrlog("NMI Taken");
			}
			else {
				m_cpu_z80[cpunum]->mz80int(interrupt_vector[cpunum]);
				wrlog("INT Taken");
			}
			break;

		case CPU_M6502:
			if (interrupt_pending[cpunum] == INT_TYPE_NMI) {
				m_cpu_6502[cpunum]->nmi6502();
				// wrlog("Delayed NMI SET CPU #%d",cpunum);
			}
			else {
				m_cpu_6502[cpunum]->irq6502();
				//wrlog("Delayed INT SET CPU #%d",cpunum);
			}
			break;

		case CPU_M6809:
			if (interrupt_pending[cpunum] == INT_TYPE_NMI) {
				m_cpu_6809[cpunum]->nmi6809();
				//wrlog("NMI Taken");
			}
			else {
				m_cpu_6809[cpunum]->irq6809();
				//wrlog("INT Taken");
			}
			break;

		case CPU_68000:
			if (interrupt_pending[cpunum])
			{
				s68000interrupt(interrupt_pending[cpunum], -1); //add interrupt num here
				s68000flushInterrupts();
				break;
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
	int sclock = driver[gamenum].cpu_freq[active_cpu]; //Why not active_cpu?
	
	//int current = cyclecount[active_cpu];  //totalcpu-1]; activecpu was last
	int current = tickcount[active_cpu];
	//wrlog(" Sound Update called, clock value: %d ", current);
	int max = sclock / driver[gamenum].fps;
	
	temp = ((float)current / (float)max);
	if (temp > 1) temp = (float).99;

	//wrlog(" Current %d divided by MAX: %d is equal to value: %f",current,max,temp);
	temp = val * temp;
	k = (int)temp;
	//wrlog("Sound position %d",k);
	return k;
}


void init_cpu_config()
{
	int x;

	totalcpu = 0;
	cpu_configured = 0;
	//running_cpu = 0;
	active_cpu = 0;

	//wrlog("Starting up cpu settings, defaults");

	for (x = 0; x < 4; x++)
	{
		if (driver[gamenum].cpu_type[x])  totalcpu++;
	}
	for (x = 0; x < totalcpu; x++)
	{
		cyclecount[x] = ((int)driver[gamenum].cpu_freq[x] / (int)driver[gamenum].fps) / (int)driver[gamenum].cpu_divisions[x];
		wrlog("Calculated Cycle count for CPU %d is %d", x, cyclecount[x]);
	}
	for (x = 0; x < 4; x++)
	{
		addcycles[x] = 0;
		interrupt_count[x] = 0;
		interrupt_pending[x] = 0;
		enable_interrupt[x] = 1;
		interrupt_vector[x] = 0xff;
	}

	
	vid_tickcount = 0;//Initalize video tickcount;
	hertzflip = 0;

	timer_init();
	//watchdog_timer = timer_set(TIME_IN_HZ(24), 0, watchdog_callback);
	wrlog("NUMBER OF CPU'S to RUN: %d ", totalcpu );
	wrlog("Finished starting up cpu settings, defaults");
}


/***************************************************************************

Use this function to initialize, and later maintain, the watchdog. For
convenience, when the machine is reset, the watchdog is disabled. If you
call this function, the watchdog is initialized, and from that point
onwards, if you don't call it at least once every 10 video frames, the
machine will be reset.

*************************************************************************/
void watchdog_callback(int param)
{
	//watchdog_timer = 0; Hmm, what to do, what to do. Right now I'm auto restarting.

	wrlog("warning: reset caused by the watchdog\n");
	cpu_reset_all();
}

void watchdog_reset_w(UINT32 address, UINT8 data, struct MemoryWriteByte* psMemWrite)
{
	timer_reset(watchdog_timer, TIME_IN_HZ(24));
}

READ_HANDLER(watchdog_reset_r)
{
	timer_reset(watchdog_timer, TIME_IN_HZ(24));
	return 0;
}

void NoWrite(UINT32 address, UINT8 data, struct MemoryWriteByte* psMemWrite)
{
	;
}

//Read Ram
UINT8 MRA_RAM(UINT32 address, struct MemoryReadByte* psMemRead)
{
	return GI[active_cpu][address + psMemRead->lowAddr];
}

//Write Ram
void MWA_RAM(UINT32 address, UINT8 data, struct MemoryWriteByte* pMemWrite)
{
	GI[active_cpu][address + pMemWrite->lowAddr] = data;
}

// Read Rom
UINT8 MRA_ROM(UINT32 address, struct MemoryReadByte* psMemRead)
{
	//wrlog("Address here is %x Lowaddr %x data %x", address, psMemRead->lowAddr, Machine->memory_region[active_cpu][address + psMemRead->lowAddr]);
	return GI[active_cpu][address + psMemRead->lowAddr];
}

// Write Rom
void MWA_ROM(UINT32 address, UINT8 data, struct MemoryWriteByte* pMemWrite)
{
	//If logging add here
}

void MWA_NOP(UINT32 address, UINT8 data, struct MemoryWriteByte* pMemWrite)
{
	//If logging add here
}

