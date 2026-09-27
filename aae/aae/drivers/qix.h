#pragma once

int  init_qix();
void run_qix();
void end_qix();

void qix_vblank_interrupt();
void qix_video_dummy_interrupt();
void qix_sound_dummy_interrupt();
