
#include "cpuintfaae.h"

#include "log.h"
//#include "m6502.h"
//#include "mz80.h"
#include "starcpu.h"
#include "globals.h"

static int cpu_configured=0;
static int num_cpus=0;
static int running_cpu=0;
static int cyclecount[4];
static int addcycles[4];
static int reset_cpu_status[4];
////Time counters
static int hrzcounter=0; //Only on CPU 0
static int hertzflip=0;
static int tickcount[4];
static int eternaticks[4];
static int vid_tickcount;
////Interrupt Variables
static int enable_interrupt;
static int interrupt_vec = 0xff;
static int interrupt_count[4];
static int interrupt_pending[4];
static int framecnt=0;
static int intcnt=0;
//CONTEXTM6502 cont6502[4];

//int tracker=1;

void initz80(struct MemoryReadByte *read, struct MemoryWriteByte *write, struct z80PortRead *portread, struct z80PortWrite *portwrite, int cpunum)
{
    
	memset(&z80, 0, sizeof(struct mz80context));
    z80.z80Base=gameImage[cpunum];
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
}


void init8080(struct MemoryReadByte *read, struct MemoryWriteByte *write, struct z80PortRead *portread, struct z80PortWrite *portwrite, int cpunum)
{
   
    
	memset(&z80, 0, sizeof(struct mz80context));
    z80.z80Base=gameImage[cpunum];
    z80.z80IoRead=portread;
    z80.z80IoWrite=portwrite;
    z80.z80MemRead=read;
    z80.z80MemWrite=write;
	mz80SetContext(&z80);
	mz80reset();
	mz80GetContext(&z80);
	z80.z80intAddr=0x8;
	z80.z80nmiAddr=0x10;
	mz80SetContext(&z80);
}

void init6502Z(struct MemoryReadByte *read, struct MemoryWriteByte *write, int cpunum)
{	
	CONTEXTM6502 *temp;
	
    write_to_log("Configuring CPU");

    c6502[cpunum] = malloc(m6502zpGetContextSize());
	write_to_log("Malloc is %d",m6502zpGetContextSize());
	temp = c6502[cpunum];
	memset( &temp, 0, m6502zpGetContextSize());
	c6502[cpunum]->m6502Base = gameImage[cpunum];
   	c6502[cpunum]->m6502MemoryRead = read;
	c6502[cpunum]->m6502MemoryWrite = write;
	m6502zpSetContext(c6502[cpunum]);
	m6502zpreset();
	m6502zpGetContext(c6502[cpunum]);
	write_to_log("Finished Configuring CPU");

}



void init6502(struct MemoryReadByte *read, struct MemoryWriteByte *write, int cpunum)
{	
   
	//CONTEXTM6502 *psCpu1;
	psCpu1 = malloc(m6502GetContextSize());
	memset(psCpu1, 0, m6502GetContextSize());
    psCpu1->m6502Base = gameImage[cpunum];
   	psCpu1->m6502MemoryRead = read;
	psCpu1->m6502MemoryWrite = write;
	m6502SetContext(psCpu1);		
	m6502reset();
	/*
	CONTEXTM6502 *temp;
	if (cpunum==0) temp=psCpu1; else temp=psCpu2;
	
	temp = malloc(m6502zpGetContextSize());
	memset(temp, 0, m6502zpGetContextSize());
    temp->m6502Base = gameImage[cpunum];
   	temp->m6502MemoryRead = read;
	temp->m6502MemoryWrite = write;
	m6502zpSetContext(temp); //Set it up		
	m6502zpreset();
	m6502zpGetContext(temp); //Store it away
	*/
}


void init68k(struct STARSCREAM_PROGRAMREGION *fetch, struct STARSCREAM_DATAREGION *readbyte, struct STARSCREAM_DATAREGION *readword, struct STARSCREAM_DATAREGION *writebyte, struct STARSCREAM_DATAREGION *writeword)
{
      s68000init();
      s68000context.fetch=fetch;
	  s68000context.s_fetch=fetch;
      s68000context.u_fetch=fetch;
      s68000context.s_readbyte=readbyte;
      s68000context.u_readbyte=readbyte;
      s68000context.s_readword=readword;
      s68000context.u_readword=readword;
      s68000context.s_writebyte=writebyte;
      s68000context.u_writebyte=writebyte;
      s68000context.s_writeword=writeword;
      s68000context.u_writeword=writeword;
	  s68000SetContext(&s68000context);
      s68000reset();
	  s68000exec(100);
      s68000reset();
	  write_to_log("68000 Initialized: Initial PC is %06X\n",s68000context.pc);
}


int return_tickcount( int reset)
{
	int val;

  switch  (driver[gamenum].cpu_type[running_cpu])
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
	hrzcounter+=ticks;
	//write_to_log("HRTZ COunter %d",hrzcounter);
	if (hrzcounter > 100) {hrzcounter-=100;hertzflip^=1;}
}

int get_hertz_counter()
{
return hertzflip;
}


void add_eterna_ticks(int cpunum, int ticks)
{
eternaticks[cpunum]+=ticks;
if (eternaticks[cpunum] > 0xffffff) eternaticks[cpunum]=0;
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
  int v=0;
  int temp;

  if (reset == 0xff) //Reset Tickcount;
  {
   vid_tickcount=0;
   vid_tickcount-=m6502zpGetElapsedTicks(0);  //Make vid_tickcount a negative number to check for reset later;
   return 0;
  }
  
  v = vid_tickcount;
  temp = m6502zpGetElapsedTicks(0);
  //if (temp <= cyclecount[running_cpu]) temp = cyclecount[running_cpu]-m6502zpGetElapsedTicks(0);
  //else write_to_log("Video CYCLE Count ERROR occured, check code.");
  v+=temp;
  return v;
}

void init_cpu_config()
{
  int x;
  
  num_cpus=0;
  cpu_configured=0;
  running_cpu=0;

  write_to_log("Starting up cpu settings, defaults");

  for(x=0; x<4 ; x++) { if (driver[gamenum].cpu_type[x])  num_cpus++;}
  
  for(x=0; x < num_cpus; x++) {(int) cyclecount[x] = ((int) driver[gamenum].cpu_freq[x] / (int) driver[gamenum].fps) / (int) driver[gamenum].cpu_divisions[x];  }
  for(x=0; x<4 ; x++) {addcycles[x]=0;interrupt_count[x]=0; interrupt_pending[x]=0;}
  enable_interrupt = 1;
  interrupt_vec = 0xff;
  vid_tickcount=0;//Initalize video tickcount;
  hertzflip=0;
  write_to_log("NUMBER OF CPU'S T RUN %d",num_cpus);

  write_to_log("Finished starting  up cpu settings, defaults");
}


int get_current_cpu()
{
 return running_cpu;
}

void cpu_disable_interrupts(int val)
{
	enable_interrupt = val;
}

int cpu_getcycles(int reset) //Only returns cycles from current context cpu
{
  int ticks=0;

  switch  (driver[gamenum].cpu_type[running_cpu])
	        {
              case CPU_MZ80:  break;
	    	  case CPU_6502Z: ticks=m6502zpGetElapsedTicks(reset);break;
			  case CPU_6502:  ticks=m6502GetElapsedTicks(reset);break;
		    }
  return ticks;
}

void cpu_setcontext(int cpunum)
{
  switch  (driver[gamenum].cpu_type[cpunum])
	        {
              case CPU_MZ80:  break;
	    	  case CPU_6502Z: m6502zpSetContext(c6502[cpunum]); break;
			  case CPU_6502:  m6502SetContext(c6502[cpunum]); break;
		    }
}

void cpu_getcontext(int cpunum)
{
  switch  (driver[gamenum].cpu_type[cpunum])
		    {
              case CPU_MZ80:  break;
			  case CPU_6502Z:m6502zpGetContext(c6502[cpunum]); break;
			  case CPU_6502:  m6502GetContext(c6502[cpunum]); break;
	        }
}


void cpu_do_interrupt(int int_type, int cpunum)
{
	if (enable_interrupt == 0) return;
   
  interrupt_count[cpunum]++;
  //write_to_log("Interrupt count %d", interrupt_count[cpunum]);
  if (interrupt_count[cpunum] == driver[gamenum].cpu_intpass_per_frame[running_cpu]) 
      {
		 intcnt++;
		  // write_to_log("Interrupt count %d", interrupt_count[cpunum]);
	   switch  (driver[gamenum].cpu_type[running_cpu])
		         {
                  case CPU_MZ80:
					   		m6502zpint(1);
					        break;

				  case CPU_6502Z:
                         if (int_type==INT_TYPE_NMI)  {
							                            m6502zpnmi();
													   write_to_log("NMI Taken"); 
						                               }
						 else {
							    m6502zpint(1); 
								write_to_log("INT Taken");
						        }   
					     break;
				 case CPU_6502:
                         if (int_type==INT_TYPE_NMI)  {
							                            m6502nmi();
													   write_to_log("NMI Taken"); 
						                               }
						 else {
							    m6502int(1); 
								write_to_log("INT Taken");
						        }   
					     break;
		        }
	  
			  interrupt_count[cpunum]=0;
     } 
//  check type of cpu (cpu_current)
//  if > 0 switch context, generate int of type
//   mz80GetContext(&context[CurrentCPU]);
//   mz80SetContext(&context[cpu]);
//   mz80nmi();
//   mz80GetContext(&context[cpu]);
//   mz80SetContext(&context[CurrentCPU]);
}

void exec_cpu()
{
   UINT32 dwResult = 0;

   switch  (driver[gamenum].cpu_type[running_cpu])
	        {
              case CPU_MZ80:  break;
	    	  case CPU_6502Z: dwResult = m6502zpexec(cyclecount[running_cpu]); break;
			  case CPU_6502:  dwResult = m6502exec(cyclecount[running_cpu]);   break;
		    }

        if (0x80000000 != dwResult)
			{	
			 allegro_message("Invalid instruction at %.2x\n");
			 exit(1);
			}
    
}

void run_cpus_to_cycles()
{
        UINT32 dwElapsedTicks = 0;
        UINT32 dwResult = 0;
  	   	int  x; 
		int cycles_ran=0;
    	
		int adj=0;
		
		tickcount[0]=0;
		tickcount[1]=0;
		tickcount[2]=0;
		tickcount[3]=0;
	   
		
		//write_to_log("Starting cpu run %d",cyclecount[running_cpu]);
		//write_to_log("Starting cpu run %d",cyclecount[running_cpu+1]);

      
		for (x=0; x < driver[gamenum].cpu_divisions[0]; x++)
		{
				 for (running_cpu=0; running_cpu < num_cpus; running_cpu++)
				 {
					 					  
					 if (num_cpus > 1) {cpu_setcontext(running_cpu);}

					  
                     dwElapsedTicks=cpu_getcycles(0xff);

					 cpu_resetter(running_cpu); //Check for CPU Reset called.
					 process_pending_interrupts(running_cpu); //Check and see if there is a pending interrupt request outstanding
					
					 exec_cpu();

					 cycles_ran = cpu_getcycles(0);
					 tickcount[running_cpu]+=cycles_ran; //Add cycles this pass to frame cycle counter;
					 add_eterna_ticks(running_cpu,cycles_ran);

					 if (running_cpu==0)
					 {
						 if ( vid_tickcount < 1) vid_tickcount = cycles_ran - vid_tickcount; //Play catchup after reset
						 vid_tickcount+=cycles_ran;
						 add_hertz_ticks(0, cycles_ran);
					 }

					 if ( driver[gamenum].int_cpu[running_cpu])  {driver[gamenum].int_cpu[running_cpu]();} 
					 else { 
							if ( driver[gamenum].cpu_int_type[running_cpu] )//Is there an int to run?
								{
								// write_to_log("WARNING --- CALLING STANDARD INTERRUPT HANDLER!!!!");
								cpu_do_interrupt( driver[gamenum].cpu_int_type[running_cpu] ,running_cpu);
								}
							}
					if (num_cpus > 1) {cpu_getcontext(running_cpu);}
				 }
        //write_to_log("Starting cpu run %d",cyclecount[running_cpu+1]);
		      
		}

		framecnt++;
		if (framecnt == driver[gamenum].fps)
		{
        write_to_log("-------------------------------------------------INTERRUPTS PER FRAME %d",intcnt);
		framecnt=0;intcnt=0;
		}
		
	 write_to_log("CPU CYCLES RAN %d CPU CYCLES REQUESTED %d", tickcount[0],driver[gamenum].cpu_freq[0]/driver[gamenum].fps);	
}


void cpu_resetter(int cpunum)
{
  if (reset_cpu_status[cpunum])
	{
	 
		switch  (driver[gamenum].cpu_type[running_cpu])
	        {
              case CPU_MZ80:  break;
	    	  case CPU_6502Z: m6502zpreset(); break;
			  case CPU_6502:  m6502reset(); break;
		    }
		
		//m6502zpreset();
	 tickcount[cpunum]=0;
	 interrupt_count[cpunum]=0;
	 reset_cpu_status[cpunum]=0;
	 if (cpunum==0)vid_tickcount=0;
	}
			
}



void cpu_needs_reset(int cpunum)
{
  reset_cpu_status[cpunum]=1;
}

void cpu_clear_pending_int(int int_type, int cpunum)
{
 ;
}


void process_pending_interrupts(int cpunum)
{
	
	       if (interrupt_pending[cpunum])
		  {     
				 switch  (driver[gamenum].cpu_type[cpunum])
					 {
					  case CPU_MZ80:
					   			m6502zpint(1);
								break;

					  case CPU_6502Z:
	                        if (interrupt_pending[cpunum]==INT_TYPE_NMI) { 
								                                           m6502zpnmi();
																		  // write_to_log("Delayed NMI SET CPU #%d",cpunum); 
							                                              }
							else {
								   m6502zpint(1);
							       //write_to_log("Delayed INT SET CPU #%d",cpunum);
							     }
							break;
				     
					  case CPU_6502:
                            if (interrupt_pending[cpunum]==INT_TYPE_NMI)  
							     {
							      m6502nmi();
								  write_to_log("NMI Taken"); 
						         }
						    else {
							      m6502int(1); 
								  write_to_log("INT Taken");
						         }   
					        break;	
				   }
				 interrupt_pending[cpunum]=0;
		  }
	

}

void set_pending_interrupt(int int_type, int cpunum) //Interrrupt to execute on next cpu cycle
{
 	 interrupt_pending[cpunum] = int_type;
}


//TO DO - ADD CPU SPEED TO MAIN DRIVER and below. Also Multiple CPUs
int cpu_scale_by_cycles(int val, int clock)
{
	int k;
	int current=0;
	int max;

	if (driver[gamenum].cpu_type[1]==CPU_6502Z) 
	{   
		//write_to_log("Major havoc read");
		current=m6502zpGetElapsedTicks(0);
		current+=tickcount[1];
	    //write_to_log("Major havoc read %d",current);
	}
	else 
	{
	 current=m6502zpGetElapsedTicks(0);
	 current+=tickcount[0];
	}
    max= clock / driver[gamenum].fps;
	
	k = val * (float)((float)current / (float) max); //BUFFER_SIZE  * 
   // write_to_log("Major havoc Value Returned %d",k);
return k;
}