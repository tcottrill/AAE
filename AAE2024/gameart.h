//============================================================================
// AAE is a poorly written M.A.M.E (TM) derivitave based on early MAME 
// code, 0.29 through .90 mixed with code of my own. This emulator was 
// created solely for my amusement and learning and is provided only 
// as an archival experience. 
// 
// All MAME code used and abused in this emulator remains the copyright 
// of the dedicated people who spend countless hours creating it. All
// MAME code should be annotated as belonging to the MAME TEAM. 
//============================================================================


#ifndef GAMEART_H
#define GAMEART_H

#include "aae_mame_driver.h"

#define ART_START(name)  static const struct artworks name[] = {
#define ART_LOAD(zipfile, filename, type, target) { zipfile, filename, type, target },
#define ART_END {NULL, NULL, 0}};

//
//Artwork Setting.
//Backdrop : layer 0
//Overlay : layer 1
// Bezel Mask : layer 2
// Bezel : layer 3
//Screen burn layer 4: Yea I know, but...
ART_START(noart)
ART_END

ART_START(asteroidsart)
ART_LOAD("asteroid.zip", "asteroids_internal.png", ART_TEX, 3)
ART_LOAD("custom.zip", "shot.png", GAME_TEX, 0)
ART_END

ART_START(astdeluxart)
ART_LOAD("astdelux.zip", "astdelux.png", ART_TEX, 0)
ART_LOAD("custom.zip", "astdelux_overlay.png", ART_TEX, 1)
ART_LOAD("astdelux.zip", "asteroids_deluxe_bezel.png", ART_TEX, 3)
ART_LOAD("custom.zip", "shot.png", GAME_TEX, 0)
ART_END

ART_START(astdelu1art)
ART_LOAD("astdelux.zip", "astdelu1.png", ART_TEX, 0)
ART_LOAD("custom.zip", "astdelux_overlay.png", ART_TEX, 1)
ART_LOAD("astdelux.zip", "asteroids_deluxe_bezel.png", ART_TEX, 3)
ART_LOAD("custom.zip", "shot.png", GAME_TEX, 0)
ART_END

ART_START(bzoneart)
ART_LOAD("bzone.zip", "bzone_overlay.png", ART_TEX, 1)
ART_LOAD("bzone.zip", "bzone.png", ART_TEX, 3)
ART_END

ART_START(solarq_art)
ART_LOAD("solarq.zip", "solarq.png", ART_TEX, 0)
ART_LOAD("solarq.zip", "solaroverlay.png", ART_TEX, 1)
ART_LOAD("solarq.zip", "solarquest_bezel.png", ART_TEX, 3)
ART_LOAD("solarq.zip", "cineshot.png", GAME_TEX, 0)
ART_END

ART_START(starcas_art)
ART_LOAD("starcas.zip", "starcas2.png", ART_TEX, 1)
ART_LOAD("starcas.zip", "mystarcastle_bezel.png", ART_TEX, 3)
ART_LOAD("starcas.zip", "cineshot.png", GAME_TEX, 0)
ART_END

ART_START(tempestart)
ART_LOAD("custom.zip", "vert_mask.png", ART_TEX, 2)
ART_END

ART_START(spacfuryart)
ART_LOAD("spacfury.zip", "space_fury_bezel.png", ART_TEX, 3)
ART_END

ART_START(startrekart)
ART_LOAD("startrek.zip", "startrek.png", ART_TEX, 3)
ART_END

ART_START(tacscanart)
ART_LOAD("custom.zip", "vert_mask.png", ART_TEX, 2)
ART_END

ART_START(sundance_art)
ART_LOAD("custom.zip", "yellow.png", ART_TEX, 1)
ART_LOAD("custom.zip", "vert_mask.png", ART_TEX, 2)
ART_LOAD("custom.zip", "cineshot.png", GAME_TEX, 0)
ART_END

ART_START(barrier_art)
ART_LOAD("custom.zip", "astdelux_overlay.png", ART_TEX, 1)
ART_LOAD("barrier.zip", "barrier_bezel.png", ART_TEX, 3)
ART_LOAD("custom.zip", "cineshot.png", GAME_TEX, 0)
ART_END

ART_START(quantumart)
ART_LOAD("custom.zip", "vert_mask.png", ART_TEX, 2)
ART_END

ART_START(redbaronart)
ART_LOAD("redbaron.zip", "redbaron_overlay.png", ART_TEX, 1)
ART_LOAD("redbaron.zip", "rbbezel.png", ART_TEX, 3)
ART_END

ART_START(ripoff_art)
ART_LOAD("custom.zip", "cineshot.png", GAME_TEX, 0)
ART_LOAD("ripoff.zip", "ripoff.png", ART_TEX, 3)
ART_END

ART_START(starhawk_art)
ART_LOAD("custom.zip", "cineshot.png", GAME_TEX, 0)
ART_END

ART_START(speedfrk_art)
ART_LOAD("custom.zip", "cineshot.png", GAME_TEX, 0)
ART_END

ART_START(demon_art)
ART_LOAD("custom.zip", "cineshot.png", GAME_TEX, 0)
ART_LOAD("demon.zip", "demon.png", ART_TEX, 1)
ART_END

ART_START(spacewar_art)
ART_LOAD("custom.zip", "cineshot.png", GAME_TEX, 0)
ART_END

ART_START(tailg_art)
ART_LOAD("custom.zip", "cineshot.png", GAME_TEX, 0)
ART_LOAD("custom.zip", "astdelux_overlay.png", ART_TEX, 1)
ART_END

ART_START(omegarace_art)
ART_LOAD("omegrace.zip", "omegbkdp3.png", ART_TEX, 0)
ART_LOAD("omegrace.zip", "omegrace_overlay.png", ART_TEX, 1)
ART_LOAD("omegrace.zip", "omegbezlcroped.png", ART_TEX, 3)
ART_END

ART_START(armora_art)
ART_LOAD("armora.zip", "armoraoverlay.png", ART_TEX, 1)
ART_LOAD("armora.zip", "armora.png", ART_TEX, 3)
ART_LOAD("custom.zip", "cineshot.png", GAME_TEX, 0)
ART_END

ART_START(warrior_art)
ART_LOAD("warrior.zip", "warrior.png", ART_TEX, 0)
//ART_LOAD("warrior.zip","warrior_bezel.png",ART_TEX, 3)
ART_LOAD("custom.zip", "cineshot.png", GAME_TEX, 0)
ART_END

#endif