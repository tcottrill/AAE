#include "aaemain.h"
#include "starintf.h"


int m68k_context_index;             /* Active context index */
t_m68k m68k_context[MAX_68000];     /* CPU structures */

/*--------------------------------------------------------------------------*/
/* Interface routines                                                       */
/*--------------------------------------------------------------------------*/


/* Must be called prior to using any other functions. */
int m68k_init(void)
{
    int count;
    int size;

    /* Initialize Starscream */
    s68000init();
    size = s68000GetContextSize();
    
    /* Set up each context */
    for(count = 0; count < MAX_68000; count += 1)
    {
        int index;

        /* Clear context and memory structures */
        memset(&m68k_context[count], 0, sizeof(t_m68k));

        /* Initialize memory structures */
        for(index = 0; index < MAX_REGION; index += 1)
        {
            m68k_context[count].fetch[index].lowaddr        = -1;
            m68k_context[count].fetch[index].highaddr       = -1;
            m68k_context[count].fetch[index].offset         = 0;

            m68k_context[count].readbyte[index].lowaddr     = -1;
            m68k_context[count].readbyte[index].highaddr    = -1;
            m68k_context[count].readbyte[index].memorycall  = NULL;
            m68k_context[count].readbyte[index].userdata    = NULL;

            m68k_context[count].readword[index].lowaddr     = -1;
            m68k_context[count].readword[index].highaddr    = -1;
            m68k_context[count].readword[index].memorycall  = NULL;
            m68k_context[count].readword[index].userdata    = NULL;

            m68k_context[count].writebyte[index].lowaddr    = -1;
            m68k_context[count].writebyte[index].highaddr   = -1;
            m68k_context[count].writebyte[index].memorycall = NULL;
            m68k_context[count].writebyte[index].userdata   = NULL;

            m68k_context[count].writeword[index].lowaddr    = -1;
            m68k_context[count].writeword[index].highaddr   = -1;
            m68k_context[count].writeword[index].memorycall = NULL;
            m68k_context[count].writeword[index].userdata   = NULL;
        }

        /* Make new context structure */
        m68k_context[count].context = (byte *)malloc(size);
        if(!m68k_context[count].context) return (0);
        memset(&m68k_context[count].context[0], 0, size);

        s68000SetContext(&m68k_context[count].context[0]);
        s68000context.s_fetch     = s68000context.u_fetch     = m68k_context[count].fetch;
        s68000context.s_readbyte  = s68000context.u_readbyte  = m68k_context[count].readbyte;
        s68000context.s_readword  = s68000context.u_readword  = m68k_context[count].readword;
        s68000context.s_writebyte = s68000context.u_writebyte = m68k_context[count].writebyte;
        s68000context.s_writeword = s68000context.u_writeword = m68k_context[count].writeword;
        s68000GetContext(&m68k_context[count].context[0]);
    }

    /* Set up first context */
    m68k_context_index = 0;
    s68000SetContext(&CP.context[0]);

    return (1);
}


/* Select the current CPU to emulate */
void m68k_switch(int num)
{
    s68000GetContext(&CP.context[0]);
    m68k_context_index = num;
    s68000SetContext(&CP.context[0]);
}


/* Reset a CPU */
int m68k_reset(void)
{
    return (s68000reset());
}


/* Execute program code */
int m68k_execute(int cycles)
{
    return (s68000exec(cycles));
}


/* Cause an interrupt */
int m68k_cause_interrupt(int level)
{
    return (s68000interrupt(level, -1));
}


/* Flush pending interrupts */
void m68k_flush_interrupts(void)
{
    s68000flushInterrupts();
}


/* Returns the program counter */
int m68k_readpc(void)
{
    return (s68000readPC());
}


/* Stop CPU emulation within a handler */
void m68k_stop(void)
{
    s68000releaseTimeslice();
}


void m68k_clear_cycles(void)
{
    s68000tripOdometer();
}


int m68k_get_elapsed_cycles(void)
{
    return (s68000readOdometer());
}


int m68k_fetch(int address)
{
    return (s68000fetch(address));
}


/*--------------------------------------------------------------------------*/
/* Memory map routines                                                      */
/*--------------------------------------------------------------------------*/

void m68k_add_fetch(unsigned lowaddr, unsigned highaddr, unsigned offset)
{
    struct STARSCREAM_PROGRAMREGION *p = &CP.fetch[(CP.index[INDEX_FETCH])];
    p->lowaddr  = lowaddr;
    p->highaddr = highaddr;
    p->offset   = (offset - lowaddr);
    CP.index[INDEX_FETCH] += 1;
}

void m68k_add_readbyte(unsigned lowaddr, unsigned highaddr, void *memorycall, void *userdata)
{
    struct STARSCREAM_DATAREGION *p = &CP.readbyte[(CP.index[INDEX_READBYTE])];
    p->lowaddr    = lowaddr;
    p->highaddr   = highaddr;
    p->memorycall = memorycall;
    p->userdata   = userdata;
    CP.index[INDEX_READBYTE] += 1;
}

void m68k_add_readword(unsigned lowaddr, unsigned highaddr, void *memorycall, void *userdata)
{
    struct STARSCREAM_DATAREGION *p = &CP.readword[(CP.index[INDEX_READWORD])];
    p->lowaddr    = lowaddr;
    p->highaddr   = highaddr;
    p->memorycall = memorycall;
    p->userdata   = userdata;
    CP.index[INDEX_READWORD] += 1;
}

void m68k_add_writebyte(unsigned lowaddr, unsigned highaddr, void *memorycall, void *userdata)
{
    struct STARSCREAM_DATAREGION *p = &CP.writebyte[(CP.index[INDEX_WRITEBYTE])];
    p->lowaddr    = lowaddr;
    p->highaddr   = highaddr;
    p->memorycall = memorycall;
    p->userdata   = userdata;
    CP.index[INDEX_WRITEBYTE] += 1;
}

void m68k_add_writeword(unsigned lowaddr, unsigned highaddr, void *memorycall, void *userdata)
{
    struct STARSCREAM_DATAREGION *p = &CP.writeword[(CP.index[INDEX_WRITEWORD])];
    p->lowaddr    = lowaddr;
    p->highaddr   = highaddr;
    p->memorycall = memorycall;
    p->userdata   = userdata;
    CP.index[INDEX_WRITEWORD] += 1;
}

void m68k_add_write(unsigned lowaddr, unsigned highaddr, void *memorycall, void *userdata)
{
    m68k_add_writebyte(lowaddr, highaddr, memorycall, userdata);
    m68k_add_writeword(lowaddr, highaddr, memorycall, userdata);
}

void m68k_add_read(unsigned lowaddr, unsigned highaddr, void *memorycall, void *userdata)
{
    m68k_add_readbyte(lowaddr, highaddr, memorycall, userdata);
    m68k_add_readword(lowaddr, highaddr, memorycall, userdata);
}

void m68k_add_readwrite(unsigned lowaddr, unsigned highaddr, void *memorycall, void *userdata)
{
    m68k_add_write(lowaddr, highaddr, memorycall, userdata);
    m68k_add_read(lowaddr, highaddr, memorycall, userdata);
}


/*--------------------------------------------------------------------------*/
/* Modify a region's memorycall and userdata pointers                       */
/*--------------------------------------------------------------------------*/

void m68k_modify_fetch_data(unsigned lowaddr, unsigned highaddr, unsigned offset)
{
    int i;
    for(i = 0; i < MAX_REGION; i += 1)
    {
        struct STARSCREAM_PROGRAMREGION *p = &CP.fetch[i];

        if((p->lowaddr == -1) && (p->highaddr == -1) && (p->offset == 0)) break;

        if((p->lowaddr == lowaddr) && (p->highaddr == highaddr))
        {
            p->offset = (offset - lowaddr);
        }
    }
}

void m68k_modify_readbyte_data(unsigned lowaddr, unsigned highaddr, void *memorycall, void *userdata)
{
    int i;
    for(i = 0; i < MAX_REGION; i += 1)
    {
        struct STARSCREAM_DATAREGION *p = &CP.readbyte[i];

        if((p->lowaddr == -1) && (p->highaddr == -1) && (p->memorycall == NULL) && (p->userdata == NULL)) break;

        if((p->lowaddr == lowaddr) && (p->highaddr == highaddr))
        {
            p->memorycall = memorycall;
            p->userdata   = userdata;
        }
    }
}

void m68k_modify_readword_data(unsigned lowaddr, unsigned highaddr, void *memorycall, void *userdata)
{
    int i;
    for(i = 0; i < MAX_REGION; i += 1)
    {
        struct STARSCREAM_DATAREGION *p = &CP.readword[i];

        if((p->lowaddr == -1) && (p->highaddr == -1) && (p->memorycall == NULL) && (p->userdata == NULL)) break;

        if((p->lowaddr == lowaddr) && (p->highaddr == highaddr))
        {
            p->memorycall = memorycall;
            p->userdata   = userdata;
        }
    }
}

void m68k_modify_writebyte_data(unsigned lowaddr, unsigned highaddr, void *memorycall, void *userdata)
{
    int i;
    for(i = 0; i < MAX_REGION; i += 1)
    {
        struct STARSCREAM_DATAREGION *p = &CP.writebyte[i];

        if((p->lowaddr == -1) && (p->highaddr == -1) && (p->memorycall == NULL) && (p->userdata == NULL)) break;

        if((p->lowaddr == lowaddr) && (p->highaddr == highaddr))
        {
            p->memorycall = memorycall;
            p->userdata   = userdata;
        }
    }
}

void m68k_modify_writeword_data(unsigned lowaddr, unsigned highaddr, void *memorycall, void *userdata)
{
    int i;
    for(i = 0; i < MAX_REGION; i += 1)
    {
        struct STARSCREAM_DATAREGION *p = &CP.writeword[i];

        if((p->lowaddr == -1) && (p->highaddr == -1) && (p->memorycall == NULL) && (p->userdata == NULL)) break;

        if((p->lowaddr == lowaddr) && (p->highaddr == highaddr))
        {
            p->memorycall = memorycall;
            p->userdata   = userdata;
        }
    }
}

void m68k_modify_write_data(unsigned lowaddr, unsigned highaddr, void *memorycall, void *userdata)
{
    m68k_modify_writebyte_data(lowaddr, highaddr, memorycall, userdata);
    m68k_modify_writeword_data(lowaddr, highaddr, memorycall, userdata);
}

void m68k_modify_read_data(unsigned lowaddr, unsigned highaddr, void *memorycall, void *userdata)
{
    m68k_modify_readbyte_data(lowaddr, highaddr, memorycall, userdata);
    m68k_modify_readword_data(lowaddr, highaddr, memorycall, userdata);
}

void m68k_modify_readwrite_data(unsigned lowaddr, unsigned highaddr, void *memorycall, void *userdata)
{
    s68000GetContext(&CP.context[0]);
    m68k_modify_write_data(lowaddr, highaddr, memorycall, userdata);
    m68k_modify_read_data(lowaddr, highaddr, memorycall, userdata);
    s68000SetContext(&CP.context[0]);
}

/*--------------------------------------------------------------------------*/
/* Modify a region's address range                                          */
/*--------------------------------------------------------------------------*/

void m68k_modify_fetch_addr(unsigned lowaddr, unsigned highaddr, unsigned offset)
{
    int i;
    for(i = 0; i < MAX_REGION; i += 1)
    {
        struct STARSCREAM_PROGRAMREGION *p = &CP.fetch[i];

        if((p->lowaddr == -1) && (p->highaddr == -1) && (p->offset == 0)) break;
        
        if((offset - p->lowaddr) == p->offset)
        {
            p->lowaddr = lowaddr;
            p->highaddr = highaddr;
            p->offset = (offset - lowaddr);
        }
    }
}

void m68k_modify_readbyte_addr(unsigned lowaddr, unsigned highaddr, void *memorycall, void *userdata)
{
    int i;
    for(i = 0; i < MAX_REGION; i += 1)
    {
        struct STARSCREAM_DATAREGION *p = &CP.readbyte[i];

        if((p->lowaddr == -1) && (p->highaddr == -1) && (p->memorycall == NULL) && (p->userdata == NULL)) break;

        if((p->memorycall == memorycall) && (p->userdata == userdata))
        {
            p->lowaddr = lowaddr;
            p->highaddr = highaddr;
        }
    }
}

void m68k_modify_readword_addr(unsigned lowaddr, unsigned highaddr, void *memorycall, void *userdata)
{
    int i;
    for(i = 0; i < MAX_REGION; i += 1)
    {
        struct STARSCREAM_DATAREGION *p = &CP.readword[i];

        if((p->lowaddr == -1) && (p->highaddr == -1) && (p->memorycall == NULL) && (p->userdata == NULL)) break;

        if((p->memorycall == memorycall) && (p->userdata == userdata))
        {
            p->lowaddr = lowaddr;
            p->highaddr = highaddr;
        }
    }
}

void m68k_modify_writebyte_addr(unsigned lowaddr, unsigned highaddr, void *memorycall, void *userdata)
{
    int i;
    for(i = 0; i < MAX_REGION; i += 1)
    {
        struct STARSCREAM_DATAREGION *p = &CP.writebyte[i];

        if((p->lowaddr == -1) && (p->highaddr == -1) && (p->memorycall == NULL) && (p->userdata == NULL)) break;

        if((p->memorycall == memorycall) && (p->userdata == userdata))
        {
            p->lowaddr = lowaddr;
            p->highaddr = highaddr;
        }
    }
}

void m68k_modify_writeword_addr(unsigned lowaddr, unsigned highaddr, void *memorycall, void *userdata)
{
    int i;
    for(i = 0; i < MAX_REGION; i += 1)
    {
        struct STARSCREAM_DATAREGION *p = &CP.writeword[i];

        if((p->lowaddr == -1) && (p->highaddr == -1) && (p->memorycall == NULL) && (p->userdata == NULL)) break;

        if((p->memorycall == memorycall) && (p->userdata == userdata))
        {
            p->lowaddr = lowaddr;
            p->highaddr = highaddr;
        }
    }
}

void m68k_modify_write_addr(unsigned lowaddr, unsigned highaddr, void *memorycall, void *userdata)
{
    m68k_modify_writebyte_addr(lowaddr, highaddr, memorycall, userdata);
    m68k_modify_writeword_addr(lowaddr, highaddr, memorycall, userdata);
}

void m68k_modify_read_addr(unsigned lowaddr, unsigned highaddr, void *memorycall, void *userdata)
{
    m68k_modify_readbyte_addr(lowaddr, highaddr, memorycall, userdata);
    m68k_modify_readword_addr(lowaddr, highaddr, memorycall, userdata);
}

void m68k_modify_readwrite_addr(unsigned lowaddr, unsigned highaddr, void *memorycall, void *userdata)
{
    m68k_modify_write_addr(lowaddr, highaddr, memorycall, userdata);
    m68k_modify_read_addr(lowaddr, highaddr, memorycall, userdata);
}



