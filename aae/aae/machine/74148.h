// -----------------------------------------------------------------------------
// 74148 8-line-to-3-line priority encoder  -  AAE port
//
// Adapted from the M.A.M.E.(TM) machine/74148.c / .h. Pure combinational logic;
// the only host dependency (logerror) is routed to the AAE logger in the .cpp.
// Used by the Exidy Vertigo driver to arbitrate the 68000 interrupt levels.
//
//   /IN0../IN7  active-low request inputs (set via TTL74148_input_line_w)
//   /EI         enable input            (TTL74148_enable_input_w)
//   A2..A0      encoded output          (TTL74148_output_r)
//   /GS         output valid            (TTL74148_output_valid_r)
//   /EO         enable output           (TTL74148_enable_output_r)
//
// Call TTL74148_update() after changing the inputs; output_cb fires on change.
// -----------------------------------------------------------------------------

#ifndef TTL74148_H
#define TTL74148_H

#pragma once

/* The interface structure */
struct TTL74148_interface
{
    void (*output_cb)(void);
};

void TTL74148_config(int which, const struct TTL74148_interface *intf);

/* must call TTL74148_update() after setting the inputs */
void TTL74148_update(int which);

void TTL74148_input_line_w(int which, int input_line, int data);
void TTL74148_enable_input_w(int which, int data);
int  TTL74148_output_r(int which);
int  TTL74148_output_valid_r(int which);
int  TTL74148_enable_output_r(int which);

#endif // TTL74148_H
