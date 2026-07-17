// -----------------------------------------------------------------------------
// Exidy Vertigo hardware - shared declarations (AAE port)
//
// Adapted from the M.A.M.E.(TM) Exidy Vertigo driver (vertigo.h / vidhrdw,
// driver by Mathis Rosenhauer). The vector processor lives in
// vidhrdwr/vertigo_video.cpp; the driver, machine I/O and memory maps live in
// drivers/vertigo.cpp.
// -----------------------------------------------------------------------------

#ifndef VERTIGO_AAE_H
#define VERTIGO_AAE_H

#pragma once

#include "deftypes.h"

// ---- defined in vidhrdwr/vertigo_video.cpp ---------------------------------
// 0x002000-0x003fff vector RAM, shared with the 68000 bus. Set by the driver in
// init() to point at the work-RAM buffer backing that range.
extern UINT16 *vertigo_vectorram;

// Build the microcode table from the PROMs and reset the vector processor.
void vertigo_vproc_init(void);

// Run the bit-slice vector CPU for `cycles` 2901 cycles. `irq4` is the current
// state of the INTL4 line; a rising edge clears the vector list for a new frame.
void vertigo_vproc(int cycles, int irq4);

#endif // VERTIGO_AAE_H
