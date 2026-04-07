#ifndef CCHASM_H
#define CCHASM_H

// ---------------------------------------------------------------------------
// cchasm.h - Cosmic Chasm driver header for AAE
//
// Cinematronics / GCE, 1983
// ---------------------------------------------------------------------------

int  init_cchasm();
void run_cchasm();
void end_cchasm();

void cchasm_interrupt();
void cchasm_sound_interrupt();

#endif // CCHASM_H
