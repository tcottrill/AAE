#pragma once

#ifndef RALLYX_H
#define RALLYX_H

#pragma warning(disable:4996 4102)


int  init_rallyx();
void run_rallyx();
void end_rallyx();
extern void rallyx_interrupt();
extern struct GfxDecodeInfo rallyx_gfxdecodeinfo[];




#endif