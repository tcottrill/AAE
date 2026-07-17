/*****************************************************************************
 *
 *  74148 8-line-to-3-line priority encoder  -  AAE port
 *
 *  Adapted from M.A.M.E.(TM) machine/74148.c. The logic is verbatim; only the
 *  host integration changed (driver.h -> AAE headers, logerror -> LOG_ERROR).
 *  See 74148.h for the pin/function summary.
 *
 *  Truth table (logic levels = actual voltage on the line):
 *
 *      EI  I0 I1 I2 I3 I4 I5 I6 I7 | A2 A1 A0 | GS EO
 *      ----------------------------+----------+------
 *      H   X  X  X  X  X  X  X  X  | H  H  H  | H  H
 *      L   H  H  H  H  H  H  H  H  | H  H  H  | H  L
 *      L   X  X  X  X  X  X  X  L  | L  L  L  | L  H
 *      L   X  X  X  X  X  X  L  H  | L  L  H  | L  H
 *      L   X  X  X  X  X  L  H  H  | L  H  L  | L  H
 *      L   X  X  X  X  L  H  H  H  | L  H  H  | L  H
 *      L   X  X  X  L  H  H  H  H  | H  L  L  | L  H
 *      L   X  X  L  H  H  H  H  H  | H  L  H  | L  H
 *      L   X  L  H  H  H  H  H  H  | H  H  L  | L  H
 *      L   L  H  H  H  H  H  H  H  | H  H  H  | L  H
 *
 *****************************************************************************/

#include "74148.h"
#include "sys_log.h"


#define MAX_TTL74148 4

struct TTL74148
{
    /* callback */
    void (*output_cb)(void);

    /* inputs */
    int input_lines[8]; /* pins 1-4,10-13 */
    int enable_input;   /* pin 5 */

    /* outputs */
    int output;         /* pins 6,7,9 */
    int output_valid;   /* pin 14 */
    int enable_output;  /* pin 15 */

    /* internals */
    int last_output;
    int last_output_valid;
    int last_enable_output;
};

static struct TTL74148 chips[MAX_TTL74148];


void TTL74148_update(int which)
{
    if (chips[which].enable_input)
    {
        // row 1 in truth table
        chips[which].output = 0x07;
        chips[which].output_valid = 1;
        chips[which].enable_output = 1;
    }
    else
    {
        int bit0, bit1, bit2;

        /* this comes straight off the data sheet schematics */
        bit0 = !((!chips[which].input_lines[1] &
                   chips[which].input_lines[2] &
                   chips[which].input_lines[4] &
                   chips[which].input_lines[6])  |
                 (!chips[which].input_lines[3] &
                   chips[which].input_lines[4] &
                   chips[which].input_lines[6])  |
                 (!chips[which].input_lines[5] &
                   chips[which].input_lines[6])  |
                 (!chips[which].input_lines[7]));

        bit1 = !((!chips[which].input_lines[2] &
                   chips[which].input_lines[4] &
                   chips[which].input_lines[5])  |
                 (!chips[which].input_lines[3] &
                   chips[which].input_lines[4] &
                   chips[which].input_lines[5])  |
                 (!chips[which].input_lines[6])  |
                 (!chips[which].input_lines[7]));

        bit2 = !((!chips[which].input_lines[4])  |
                 (!chips[which].input_lines[5])  |
                 (!chips[which].input_lines[6])  |
                 (!chips[which].input_lines[7]));

        chips[which].output = (bit2 << 2) | (bit1 << 1) | bit0;

        chips[which].output_valid = (chips[which].input_lines[0] &
                                     chips[which].input_lines[1] &
                                     chips[which].input_lines[2] &
                                     chips[which].input_lines[3] &
                                     chips[which].input_lines[4] &
                                     chips[which].input_lines[5] &
                                     chips[which].input_lines[6] &
                                     chips[which].input_lines[7]);

        chips[which].enable_output = !chips[which].output_valid;
    }


    /* call callback if any of the outputs changed */
    if (  chips[which].output_cb &&
        ((chips[which].output        != chips[which].last_output) ||
         (chips[which].output_valid  != chips[which].last_output_valid) ||
         (chips[which].enable_output != chips[which].last_enable_output)))
    {
        chips[which].last_output = chips[which].output;
        chips[which].last_output_valid = chips[which].output_valid;
        chips[which].last_enable_output = chips[which].enable_output;

        chips[which].output_cb();
    }
}


void TTL74148_input_line_w(int which, int input_line, int data)
{
    chips[which].input_lines[input_line] = data ? 1 : 0;
}


void TTL74148_enable_input_w(int which, int data)
{
    chips[which].enable_input = data ? 1 : 0;
}


int TTL74148_output_r(int which)
{
    return chips[which].output;
}


int TTL74148_output_valid_r(int which)
{
    return chips[which].output_valid;
}


int TTL74148_enable_output_r(int which)
{
    return chips[which].enable_output;
}



void TTL74148_config(int which, const struct TTL74148_interface *intf)
{
    if (which >= MAX_TTL74148)
    {
        LOG_ERROR("Only %d 74148's are supported at this time.", MAX_TTL74148);
        return;
    }


    chips[which].output_cb = (intf ? intf->output_cb : 0);
    chips[which].enable_input = 1;
    chips[which].input_lines[0] = 1;
    chips[which].input_lines[1] = 1;
    chips[which].input_lines[2] = 1;
    chips[which].input_lines[3] = 1;
    chips[which].input_lines[4] = 1;
    chips[which].input_lines[5] = 1;
    chips[which].input_lines[6] = 1;
    chips[which].input_lines[7] = 1;

    chips[which].last_output = -1;
    chips[which].last_output_valid = -1;
    chips[which].last_enable_output = -1;
}
