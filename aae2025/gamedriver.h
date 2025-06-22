#pragma once

#ifndef _GAMEDRIVER_H_
#define _GAMEDRIVER_H_

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
#include "aae_mame_driver.h"
#include "gameroms.h"
#include "gamekeys.h"
#include "gamesamp.h"
#include "gameart.h"
#include "gui/gui.h"
#include "asteroid.h"
#include "bwidow.h"
#include "SegaG80.h"
#include "mhavoc.h"
#include "tempest.h"
#include "bzone.h"
#include "starwars.h"
#include "starwars_snd.h"
#include "cinematronics_driver.h"
#include "omegrace.h"
#include "quantum.h"
#include "llander.h"
#include "aztarac.h"
#include "invaders.h"
#include "rallyx.h"
#include "rallyx_vid.h"
#include "earom.h"


struct AAEDriver driver[] =
{
		{
		"aae", "AAE GUI",  0,
		&init_gui,&run_gui, &end_gui,
		input_ports_gui,
		guisamples, noart,
		{CPU_NONE,CPU_NONE,CPU_NONE,CPU_NONE},
		{0,0,0,0},
		{0,0,0,0},
		{0,0,0,0},
		{0,0,0,0},
		{0,0,0,0},
		60, VIDEO_TYPE_RASTER, ORIENTATION_DEFAULT,
		1024 , 768, {0, 1040, 0, 820},
		0,0,0,0,
		0, 0,
		0x4000, 0x800,0
		},
	    {
		"meteorts", "Meteorites (Asteroids Bootleg)", rom_meteorts,
		&init_asteroid,&run_asteroid,&end_asteroid,
		input_ports_asteroid,
		asteroidsamples, asteroidsart,
		{CPU_M6502,CPU_NONE,CPU_NONE,CPU_NONE},
		{1512000,0,0,0},
		{100,0,0,0},
		{4,0,0,0},
		{INT_TYPE_NMI,0,0,0},
		{&asteroid_interrupt,0,0,0},
		60,VIDEO_TYPE_VECTOR | VECTOR_USES_BW , ORIENTATION_DEFAULT,
		1024 , 768, {0, 1040, 0, 820},
		0,0,0,0,
		0, 0,
		0x4000, 0x800,0
		},
		{
		"asterock", "Asterock (Asteroids Bootleg)", rom_asterock,
		&init_asterock,&run_asteroid, &end_asteroid,
		input_ports_asterock,
		asteroidsamples, asteroidsart,
		{CPU_M6502,CPU_NONE,CPU_NONE,CPU_NONE},
		{1512000,0,0,0},
		{100,0,0,0},
		{4,0,0,0},
		{INT_TYPE_NMI,0,0,0},
		{&asteroid_interrupt,0,0,0},
		60,VIDEO_TYPE_VECTOR | VECTOR_USES_BW, ORIENTATION_DEFAULT,
		1024 , 768, {0, 1040, 0, 820},
		0,0,0,0,
		asteroid_hiload, asteroid_hisave,
		0x4000, 0x800,0
		},
		{
		"asteroidb", "Asteroids (Bootleg on Lunar Lander Hardware)",  rom_asteroidb,
		&init_asteroid,&run_asteroid,&end_asteroid,
		input_ports_asteroid,
		asteroidsamples,asteroidsart,
		{CPU_M6502,CPU_NONE,CPU_NONE,CPU_NONE},
		{1512000,0,0,0},
		{100,0,0,0},
		{4,0,0,0},
		{INT_TYPE_NMI,0,0,0},
		{&asteroid_interrupt,0,0,0},
		60,VIDEO_TYPE_VECTOR | VECTOR_USES_BW, ORIENTATION_DEFAULT,
		1024 , 768, {0, 1040, 0, 820},
		0,0,0,0,
		0, 0,
		0x4000, 0x800,0
		},
		{
		"asteroid1", "Asteroids (Revision 1)", rom_asteroid1,
		&init_asteroid,&run_asteroid,&end_asteroid,
		input_ports_asteroid,
		asteroidsamples,asteroidsart,
		{CPU_M6502,CPU_NONE,CPU_NONE,CPU_NONE},
		{1512000,0,0,0},
		{100,0,0,0},
		{4,0,0,0},
		{INT_TYPE_NMI,0,0,0},
		{&asteroid_interrupt,0,0,0},
		60,VIDEO_TYPE_VECTOR | VECTOR_USES_BW, ORIENTATION_DEFAULT,
		1024 , 768, {0, 1040, 0, 820},
		0,0,0,0,
		asteroid1_hiload, asteroid1_hisave,
		0x4000, 0x800,0
		},
		{ "asteroid", "Asteroids (Revision 2)", rom_asteroid
		,&init_asteroid, &run_asteroid,&end_asteroid,
		input_ports_asteroid,
		asteroidsamples,asteroidsart,
		{CPU_M6502,CPU_NONE,CPU_NONE,CPU_NONE},
		{1512000,0,0,0},
		{100,0,0,0},
		{4,0,0,0},
		{INT_TYPE_NMI,0,0,0},
		{&asteroid_interrupt,0,0,0},
		60,VIDEO_TYPE_VECTOR | VECTOR_USES_BW, ORIENTATION_DEFAULT,
		1024 , 768, {0, 1040, 0, 820},
	    0,0,0,0,
		asteroid_hiload, asteroid_hisave,
		0x4000, 0x800,0
		},
		{
		"astdelux1", "Asteroids Deluxe (Revision 1)", rom_astdelux1,
		&init_astdelux, &run_astdelux,&end_astdelux,
		input_ports_astdelux,
		deluxesamples, astdelu1art,
		{CPU_M6502,0,0,0},
		{1512000,0,0,0},
		{100,0,0,0},
		{4,0,0,0},
		{INT_TYPE_NMI,0,0,0},
		{&asteroid_interrupt,0,0,0},
		60,VIDEO_TYPE_VECTOR | VECTOR_USES_BW | VECTOR_USES_OVERLAY1, ORIENTATION_DEFAULT,
		1024 , 768, {0, 1040, 0, 820},
		0,0,0,0,
		0, 0,
		0x4000, 0x800,
		atari_vg_earom_handler
		},
		{
		"astdelux2", "Asteroids Deluxe (Revision 2)", rom_astdelux2,
		&init_astdelux, &run_astdelux,&end_astdelux,
		input_ports_astdelux,
		deluxesamples, astdeluxart,
		{CPU_M6502,CPU_NONE,CPU_NONE,CPU_NONE},
		{1512000,0,0,0},
		{100,0,0,0},
		{4,0,0,0},
		{INT_TYPE_NMI,0,0,0},
		{&asteroid_interrupt,0,0,0},
		60,VIDEO_TYPE_VECTOR | VECTOR_USES_BW | VECTOR_USES_OVERLAY1, ORIENTATION_DEFAULT,
		1024 , 768, {0, 1040, 0, 820},
		0,0,0,0,
		0, 0,
		0x4000, 0x800,
		atari_vg_earom_handler
		},
		{
		"astdelux", "Asteroids Deluxe (Revision 3)", rom_astdelux,
		&init_astdelux, & run_astdelux,&end_astdelux,
		input_ports_astdelux,
		deluxesamples, astdeluxart,
		{CPU_M6502,CPU_NONE,CPU_NONE,CPU_NONE},
		{1512000,0,0,0},
		{100,0,0,0},
		{4,0,0,0},
		{INT_TYPE_NMI,0,0,0},
		{&asteroid_interrupt,0,0,0},
		60,VIDEO_TYPE_VECTOR | VECTOR_USES_BW | VECTOR_USES_OVERLAY1, ORIENTATION_DEFAULT,
		1024 , 768, {0, 1040, 0, 820},
		0,0,0,0,
		0, 0,
		0x4000, 0x800,
		atari_vg_earom_handler
		},
		{
		"llander1", "Lunar Lander (Revision 1)", rom_llander1,
		&init_llander, &run_llander,&end_llander,
		input_ports_llander,
		llander_samples, noart,
		{CPU_M6502,CPU_NONE,CPU_NONE,CPU_NONE},
		{1512000,0,0,0},
		{100,0,0,0},
		{6,0,0,0},
		{INT_TYPE_NMI,0,0,0},
		{&llander_interrupt,0,0,0},
		40,VIDEO_TYPE_VECTOR | VECTOR_USES_BW, ORIENTATION_DEFAULT,
		1024, 768, 	{0, 1044, -80, 780},
		0,0,0,0,
		0,0,
		0x4000, 0x800,0
		},
		{
		"llander", "Lunar Lander (Revision 2)", rom_llander,
		&init_llander,&run_llander,&end_llander,
		input_ports_llander,
		llander_samples, noart,
		{CPU_M6502,CPU_NONE,CPU_NONE,CPU_NONE},
		{1512000,0,0,0},
		{100,0,0,0},
		{6,0,0,0},
		{INT_TYPE_NMI,0,0,0},
		{&llander_interrupt,0,0,0},
		40,VIDEO_TYPE_VECTOR | VECTOR_USES_BW, ORIENTATION_DEFAULT,
		1024, 768, 	{0, 1044, -80, 780},
		0,0,0,0,
		0,0,
		0x4000, 0x800,0
		},
	   {
		"omegrace", "Omega Race", rom_omegrace,
		&init_omega,&run_omega, &end_omega,
		input_ports_omegrace,
		0,omegarace_art,
		{CPU_MZ80,CPU_MZ80,CPU_NONE,CPU_NONE},
		{3020000,1512000,0,0},
		{100,100,0,0},
		{25,25,0,0},
		{INT_TYPE_INT,INT_TYPE_INT,0,0},
		{&omega_interrupt,omega_nmi_interrupt,0,0},
		40,VIDEO_TYPE_VECTOR | VECTOR_USES_BW | VECTOR_USES_OVERLAY1, ORIENTATION_DEFAULT,
		1024, 768, 	{ 0, 1044, 0, 1024 },
		0,0,0,0,
		0, 0,
	    0x8000, 0x1000,nvram_handler
		},
		{
		"deltrace", "Delta Race (Omega Race Bootleg)", rom_deltrace,
		&init_omega,&run_omega, &end_omega,
		input_ports_omegrace,
		0,omegarace_art,
		{CPU_MZ80,CPU_MZ80,CPU_NONE,CPU_NONE},
		{3020000,1512000,0,0},
		{100,100,0,0},
		{25,25,0,0},
		{INT_TYPE_INT,0,0,0},
		{&omega_interrupt,omega_nmi_interrupt,0,0},
		40,VIDEO_TYPE_VECTOR | VECTOR_USES_BW | VECTOR_USES_OVERLAY1, ORIENTATION_DEFAULT,
		1024, 768, 	{ 0, 1044, 0, 1024 },
		0,0,0,0,
		0, 0,
	    0x8000, 0x1000,nvram_handler
		},
		{
		"bzone", "Battlezone (Revision 1)", rom_bzone,
		&init_bzone,&run_bzone, &end_bzone,
		input_ports_bzone,
		bzonesamples, bzoneart,
		{CPU_M6502,CPU_NONE,CPU_NONE,CPU_NONE},
		{1512000,0,0,0},
		{100,0,0,0},
		{1,0,0,0},
		{INT_TYPE_NMI,0,0,0},
		{0,0,0,0},
		40 ,VIDEO_TYPE_VECTOR | VECTOR_USES_BW | VECTOR_USES_OVERLAY1, ORIENTATION_DEFAULT,
		1024, 768, { 0, 460, 0, 395 },
		0,0,0,0,
		0, 0,
		0x2000, 0x1000,
		atari_vg_earom_handler
		},
		{
		"bzonea", "Battlezone (Revision A)", rom_bzonea,
		&init_bzone,&run_bzone, &end_bzone,
		input_ports_bzone,
		bzonesamples, bzoneart,
		{CPU_M6502,CPU_NONE,CPU_NONE,CPU_NONE},
		{1512000,0,0,0},
		{100,0,0,0},
		{1,0,0,0},
		{INT_TYPE_NMI,0,0,0},
		{0,0,0,0},
		40 ,VIDEO_TYPE_VECTOR | VECTOR_USES_BW | VECTOR_USES_OVERLAY1, ORIENTATION_DEFAULT,
		1024, 768, { 0, 460, 0, 395 },
		0,0,0,0,
		0, 0,
		0x2000, 0x1000,
		atari_vg_earom_handler
		},
		{
		"bzonec", "Battlezone Cocktail Proto", rom_bzonec,
		&init_bzone,&run_bzone, &end_bzone,
		input_ports_bzone,
		bzonesamples, bzoneart,
		{CPU_M6502,CPU_NONE,CPU_NONE,CPU_NONE},
		{1512000,0,0,0},
		{100,0,0,0},
		{1,0,0,0},
		{INT_TYPE_NMI,0,0,0},
		{0,0,0,0},
		40 ,VIDEO_TYPE_VECTOR | VECTOR_USES_BW | VECTOR_USES_OVERLAY1, ORIENTATION_DEFAULT,
		1024, 768, { 0, 460, 0, 395 },
		0,0,0,0,
		0, 0,
		0x2000, 0x1000,
		atari_vg_earom_handler
		},
		{
		"bzonep", "Battlezone Plus (Clay Cowgill)", rom_bzonep,
		&init_bzone,&run_bzone, &end_bzone,
		input_ports_bzone,
		bzonesamples, bzoneart,
		{CPU_M6502,CPU_NONE,CPU_NONE,CPU_NONE},
		{1512000,0,0,0},
		{100,0,0,0},
		{1,0,0,0},
		{INT_TYPE_NMI,0,0,0},
		{0,0,0,0},
		40 ,VIDEO_TYPE_VECTOR | VECTOR_USES_BW | VECTOR_USES_OVERLAY1, ORIENTATION_DEFAULT,
		1024, 768, { 0, 460, 0, 395 },
		0,0,0,0,
		0, 0,
		0x2000, 0x1000,
		atari_vg_earom_handler
		},
		{
		"redbaron", "Red Baron", rom_redbaron,
		& init_redbaron,&run_bzone, &end_bzone,
		input_ports_redbaron,
		redbaron_samples, redbaronart,
		{CPU_M6502,CPU_NONE,CPU_NONE,CPU_NONE},
		{1512000,0,0,0},
		{100,0,0,0},
		{1,0,0,0},
		{0,0,0,0},
		{0,0,0,0},
		60,VIDEO_TYPE_VECTOR | VECTOR_USES_BW | VECTOR_USES_OVERLAY1, ORIENTATION_DEFAULT,
		1024, 768, { 0, 460, 0, 395 },
		0,0,0,0,
		0, 0,
		0x2000, 0x1000,
		atari_vg_earom_handler
		},
		{
		"bradley", "Bradley Trainer", rom_bradley,
		&init_bzone,&run_bzone, &end_bzone,
		input_ports_bzone,
		bzonesamples, noart,
		{CPU_M6502,CPU_NONE,CPU_NONE,CPU_NONE},
		{1512000,0,0,0},
		{100,0,0,0},
		{1,0,0,0},
		{INT_TYPE_NMI,0,0,0},
		{0,0,0,0},
		45 ,VIDEO_TYPE_VECTOR | VECTOR_USES_BW, ORIENTATION_DEFAULT,
		1024, 768, { 0, 460, 0, 395 },
		0,0,0,0,
		0, 0,
		0x2000, 0x1000,
		atari_vg_earom_handler
		},
		{
		"spacduel", "Space Duel", rom_spacduel,
		&init_spacduel,& run_bwidow,&end_bwidow,//&set_sd
		input_ports_spacduel,
		0, noart,
		{CPU_M6502,CPU_NONE,CPU_NONE,CPU_NONE},
		{1512000,0,0,0},
		{100,0,0,0},
		{1,0,0,0},
		{INT_TYPE_INT,0,0,0},
		{0,0,0,0},
		45,VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_DEFAULT,
		1024,768, {0, 520, 0, 395},
		0,0,0,0,
		0, 0,
		0x2000, 0x800,
		atari_vg_earom_handler
		},
		{
		"spacduel0", "Space Duel (prototype)", rom_spacduel0,
		&init_spacduel,&run_bwidow,&end_bwidow,//&set_sd
		input_ports_spacduel,
		0, noart,
		{CPU_M6502,CPU_NONE,CPU_NONE,CPU_NONE},
		{1512000,0,0,0},
		{100,0,0,0},
		{1,0,0,0},
		{INT_TYPE_INT,0,0,0},
		{0,0,0,0},
		45,VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_DEFAULT,
		1024,768, {0, 520, 0, 395},
		0,0,0,0,
		0, 0,
		0x2000, 0x800,
		atari_vg_earom_handler
		},
		{
		"spacduel1", "Space Duel Revision 1", rom_spacduel1,
		&init_spacduel,&run_bwidow,&end_bwidow,//&set_sd
		input_ports_spacduel,
		0, noart,
		{CPU_M6502,CPU_NONE,CPU_NONE,CPU_NONE},
		{1512000,0,0,0},
		{100,0,0,0},
		{1,0,0,0},
		{INT_TYPE_INT,0,0,0},
		{0,0,0,0},
		45,VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_DEFAULT,
		1024,768, {0, 520, 0, 395},
		0,0,0,0,
		0, 0,
		0x2000, 0x800,
		atari_vg_earom_handler
		},
		{
		"bwidow",   "Black Widow", rom_bwidow,
		& init_bwidow,& run_bwidow,&end_bwidow,
		input_ports_bwidow,
		0, noart,
		{CPU_M6502,CPU_NONE,CPU_NONE,CPU_NONE},
		{1512000,0,0,0},
		{100,0,0,0},
		{1,0,0,0},
		{INT_TYPE_INT,0,0,0},
		{0,0,0,0},
		60,VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_DEFAULT,
		1024, 768,	{ 0, 540, 0, 450 },
		0,0,0,0,
		0, 0,
		0x2000, 0x800,
		atari_vg_earom_handler
		},
		{
		"gravitar", "Gravitar (Revision 3)", rom_gravitar,
		& init_bwidow,& run_bwidow,&end_bwidow,
		input_ports_gravitar,
		0, noart,
		{CPU_M6502,CPU_NONE,CPU_NONE,CPU_NONE},
		{1515000,0,0,0},
		{100,0,0,0},
		{1,0,0,0},
		{INT_TYPE_INT,0,0,0},
		{0,0,0,0},
		60,VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_DEFAULT,
		1000,790, {0, 460, 0, 395},
		0,0,0,0,
		0, 0,
		0x2000, 0x800,
		atari_vg_earom_handler
		},
		{
		"gravitar2", "Gravitar (Revision 2)", rom_gravitar2,
		& init_bwidow,& run_bwidow,&end_bwidow,
		input_ports_gravitar,
		0, noart,
		{CPU_M6502,CPU_NONE,CPU_NONE,CPU_NONE},
		{1515000,0,0,0},
		{100,0,0,0},
		{1,0,0,0},
		{INT_TYPE_INT,0,0,0},
		{0,0,0,0},
		60,VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_DEFAULT,
		1000,790, {0, 460, 0, 395},
		0,0,0,0,
		0, 0,
		0x2000, 0x800,
		atari_vg_earom_handler
		},
		{
		"gravp", "Gravitar (Prototype)", rom_gravp,
		&init_bwidow,& run_bwidow,&end_bwidow,
		input_ports_gravitar,
		0, noart,
		{CPU_M6502,CPU_NONE,CPU_NONE,CPU_NONE},
		{1515000,0,0,0},
		{100,0,0,0},
		{1,0,0,0},
		{INT_TYPE_INT,0,0,0},
		{0,0,0,0},
		60,VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_DEFAULT,
		1000,790, {0, 460, 0, 395},
		0,0,0,0,
		0, 0,
		0x2000, 0x800,
		atari_vg_earom_handler
		},
		{
		"lunarbat", "Lunar Battle (Prototype, Late)", rom_lunarbat,
		& init_bwidow,& run_bwidow,&end_bwidow,
		input_ports_lunarbat,
		0, noart,
		{CPU_M6502,CPU_NONE,CPU_NONE,CPU_NONE},
		{1512000,0,0,0},
		{100,0,0,0},
		{1,0,0,0},
		{INT_TYPE_INT,0,0,0},
		{0,0,0,0},
		45,VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_DEFAULT,
		1000,790, {0, 460, 0, 395},
		0,0,0,0,
		0, 0,
		0x2000, 0x800,
		atari_vg_earom_handler
		},
		{
		"lunarba1", "Lunar Battle (Prototype, Early)", rom_lunarba1,
		&init_spacduel,& run_bwidow,&end_bwidow,
		input_ports_lunarba1,
		0, noart,
		{CPU_M6502,CPU_NONE,CPU_NONE,CPU_NONE},
		{1512000,0,0,0},
		{100,0,0,0},
		{1,0,0,0},
		{INT_TYPE_INT,0,0,0},
		{0,0,0,0},
		45,VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_DEFAULT,
		1000,790, {0, 460, 0, 395},
		0,0,0,0,
		0, 0,
		0x2000, 0x800,
		atari_vg_earom_handler
		},

		{
		"tempestm", "Tempest Multigame (1999 Clay Cowgill)", rom_tempestm,
		& init_tempestm,&run_tempest,&end_tempest,
		input_ports_tempest,
		0, tempestart,
		{CPU_M6502,CPU_NONE,CPU_NONE,CPU_NONE},
		{1512000,0,0,0},
		{100,0,0,0},
		{4,0,0,0},
		{INT_TYPE_INT,0,0,0},
		{&tempest_interrupt,0,0,0},
		60, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_ROTATE_270,
		1024, 768,{ 0, 580, 0, 570 },
		0,0,0,0,
		0, 0,
		0x2000, 0x1000,
		atari_vg_earom_handler
		},
		{
		"tempest", "Tempest (Revision 3)", rom_tempest,
		&init_tempest,&run_tempest,&end_tempest,
		input_ports_tempest,
		0, tempestart,
		{CPU_M6502,CPU_NONE,CPU_NONE,CPU_NONE},
		{1515000,0,0,0},
		{100,0,0,0},
		{4,0,0,0},
		{INT_TYPE_INT,0,0,0},
		{&tempest_interrupt,0,0,0},
		60, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_ROTATE_270,
		1024, 768,{ 0, 580, 0, 570 },
		0,0,0,0,
		0, 0,
		0x2000, 0x1000,
		atari_vg_earom_handler
		},
		{
		"tempest3", "Tempest (Revision 2B)", rom_tempest3,
		&init_tempest,&run_tempest,&end_tempest,
		input_ports_tempest,
		0, tempestart,
		{CPU_M6502,CPU_NONE,CPU_NONE,CPU_NONE},
		{1512000,0,0,0},
		{100,0,0,0},
		{4,0,0,0},
		{INT_TYPE_INT,0,0,0},
		{&tempest_interrupt,0,0,0},
		60, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_ROTATE_270,
		1024, 768,{ 0, 580, 0, 570 },
		0,0,0,0,
		0, 0,
		0x2000, 0x1000,
		atari_vg_earom_handler
		},
		{
		"tempest2", "Tempest (Revision 2A)", rom_tempest2,
		&init_tempest,&run_tempest,&end_tempest,
		input_ports_tempest,
		0, tempestart,
		{CPU_M6502,CPU_NONE,CPU_NONE,CPU_NONE},
		{1512000,0,0,0},
		{100,0,0,0},
		{4,0,0,0},
		{INT_TYPE_INT,0,0,0},
		{&tempest_interrupt,0,0,0},
		60, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_ROTATE_270,
		1024, 768,{ 0, 580, 0, 570 },
		0,0,0,0,
		0, 0,
		0x2000, 0x1000,
		atari_vg_earom_handler
		},
		{
		"tempest1", "Tempest (Revision 1)", rom_tempest1,
		&init_tempest,&run_tempest,&end_tempest,
		input_ports_tempest,
		0, tempestart,
		{CPU_M6502,CPU_NONE,CPU_NONE,CPU_NONE},
		{1512000,0,0,0},
		{100,0,0,0},
		{4,0,0,0},
		{INT_TYPE_INT,0,0,0},
		{&tempest_interrupt,0,0,0},
		60, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_ROTATE_270,
		1024, 768,{ 0, 580, 0, 570 },
		0,0,0,0,
		0, 0,
		0x2000, 0x1000,
		atari_vg_earom_handler
		},
		{
		"temptube", "Tempest Tubes", rom_temptube,
		&init_tempest,&run_tempest,&end_tempest,
		input_ports_tempest,
		0, tempestart,
		{CPU_M6502,CPU_NONE,CPU_NONE,CPU_NONE},
		{1512000,0,0,0},
		{100,0,0,0},
		{4,0,0,0},
		{INT_TYPE_INT,0,0,0},
		{&tempest_interrupt,0,0,0},
		60, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_ROTATE_270,
		1024, 768, { 0, 580, 0, 570 },
		0,0,0,0,
		0, 0,
		0x2000, 0x1000,
		atari_vg_earom_handler
		},
		{
		"aliensv", "Aliens (Tempest Alpha)", rom_aliensv,
		&init_tempest,&run_tempest,&end_tempest,
		input_ports_tempest,
		0, tempestart,
		{CPU_M6502,CPU_NONE,CPU_NONE,CPU_NONE},
		{1512000,0,0,0},
		{100,0,0,0},
		{4,0,0,0},
		{INT_TYPE_INT,0,0,0},
		{&tempest_interrupt,0,0,0},
		60, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_ROTATE_270,
		1024, 768, { 0, 580, 0, 570 },
		0,0,0,0,
		0, 0,
		0x2000, 0x1000,
		atari_vg_earom_handler
		},
		{
		"vbrakout", "Vector Breakout (1999 Clay Cowgill)", rom_vbrakout,
		&init_tempest,&run_tempest,&end_tempest,
		input_ports_tempest,
		0, tempestart,
		{CPU_M6502,CPU_NONE,CPU_NONE,CPU_NONE},
		{1512000,0,0,0},
		{100,0,0,0},
		{4,0,0,0},
		{INT_TYPE_INT,0,0,0},
		{&tempest_interrupt,0,0,0},
		60, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_ROTATE_270,
		1024, 768, { 0, 580, 0, 570 },
		0,0,0,0,
		0, 0,
		0x2000, 0x1000,
		atari_vg_earom_handler
		},
		{
		"vortex", "Vortex (Tempest Beta)", rom_vortex,
		&init_tempest,&run_tempest,&end_tempest,
		input_ports_tempest,
		0, tempestart,
		{CPU_M6502,CPU_NONE,CPU_NONE,CPU_NONE},
		{1512000,0,0,0},
		{100,0,0,0},
		{4,0,0,0},
		{INT_TYPE_INT,0,0,0},
		{&tempest_interrupt,0,0,0},
		60, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_ROTATE_270,
		1024, 768, { 0, 580, 0, 570 },
		0,0,0,0,
		0, 0,
		0x2000, 0x1000,
		atari_vg_earom_handler
		},

		{
		"zektor", "Zektor", rom_zektor,
		&init_zektor,&run_segag80,&end_segag80,
		input_ports_zektor,
		zektor_samples, noart,
		{CPU_MZ80,CPU_NONE,CPU_NONE,CPU_NONE},
		{3000000,0,0,0},
		{1,0,0,0},
		{1,0,0,0},
		{INT_TYPE_INT,0,0,0},
		{&sega_interrupt,0,0,0},
		40, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_DEFAULT,
		1024, 768, { 512, 1536, 552, 1464 },
		0,0,0,0, // Raster Graphix
		0, 0,   // Loader
		0, 0,  // Vector Location, Size
		0 // Earom Loader
		},
		{
		"tacscan", "Tac/Scan", rom_tacscan,
		&init_tacscan,&run_segag80,&end_segag80,
		input_ports_tacscan,
		tacscan_samples, tacscanart,
		{CPU_MZ80,CPU_NONE,CPU_NONE,CPU_NONE},
		{3000000,0,0,0},
		{1,0,0,0},
		{1,0,0,0},
		{INT_TYPE_INT,0,0,0},
		{&sega_interrupt,0,0,0},
		40, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_ROTATE_270,
		1024, 768, { 512, 1536, 552, 1464 },
		0,0,0,0, // Raster Graphix
		0, 0,   // Loader
		0, 0,  // Vector Location, Size
		0 // Earom Loader
		},
		{
		"startrek", "Star Trek", rom_startrek,
		&init_startrek,&run_segag80,&end_segag80,
		input_ports_startrek,
		startrek_samples, startrekart,
		{ CPU_MZ80,CPU_NONE,CPU_NONE,CPU_NONE},
		{3000000,0,0,0},
		{1,0,0,0},
		{1,0,0,0},
		{INT_TYPE_INT,0,0,0},
		{&sega_interrupt,0,0,0},
		40, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_DEFAULT,
		1024, 768, { 512, 1536, 552, 1464 },
		0,0,0,0, // Raster Graphix
		0, 0,   // Loader
		0, 0,  // Vector Location, Size
		0 // Earom Loader
		},
		{
		"spacfury", "Space Fury (Revision C)", rom_spacfury,
		&init_spacfury,&run_segag80,&end_segag80,
		input_ports_spacfury,
		spacfury_samples, spacfuryart,
		{ CPU_MZ80,CPU_NONE,CPU_NONE,CPU_NONE},
		{3000000,0,0,0},
		{1,0,0,0},
		{1,0,0,0},
		{INT_TYPE_INT,0,0,0},
		{&sega_interrupt,0,0,0},
		40,VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_DEFAULT,
		1024, 768, { 512, 1536, 552, 1464 },
		0,0,0,0, // Raster Graphix
		0, 0,   // Loader
		0, 0,  // Vector Location, Size
		0 // Earom Loader
		},
		{
		"spacfura", "Space Fury (Revision A)", rom_spacfura,
		&init_spacfury,&run_segag80,&end_segag80,
		input_ports_spacfury,
		spacfury_samples, spacfuryart,
		{CPU_MZ80,CPU_NONE,CPU_NONE,CPU_NONE},
		{3000000,0,0,0},
		{1,0,0,0},
		{1,0,0,0},
		{INT_TYPE_INT,0,0,0},
		{&sega_interrupt,0,0,0},
		40, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_DEFAULT,
		1024, 768, { 512, 1536, 552, 1464 },
		0,0,0,0, // Raster Graphix
		0, 0,   // Loader
		0, 0,  // Vector Location, Size
		0 // Earom Loader
		},
		{
		"spacfurb", "Space Fury (Revision B)", rom_spacfurb,
		&init_spacfury,&run_segag80,&end_segag80,
		input_ports_spacfury,
		spacfury_samples, spacfuryart,
		{CPU_MZ80,CPU_NONE,CPU_NONE,CPU_NONE},
		{3000000,0,0,0},
		{1,0,0,0},
		{1,0,0,0},
		{INT_TYPE_INT,0,0,0},
		{&sega_interrupt,0,0,0},
		40, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_DEFAULT,
		1024, 768, { 512, 1536, 552, 1464 },
		0,0,0,0, // Raster Graphix
		0, 0,   // Loader
		0, 0,  // Vector Location, Size
		0 // Earom Loader
		},
		{
		"elim2", "Eliminator (2 Player Set 1)", rom_elim2,
		&init_elim2,&run_segag80,&end_segag80,
		input_ports_elim2,
		elim_samples, noart,
		{CPU_MZ80,CPU_NONE,CPU_NONE,CPU_NONE},
		{3000000,0,0,0},
		{1,0,0,0},
		{1,0,0,0},
		{INT_TYPE_INT,0,0,0},
		{&sega_interrupt,0,0,0},
		40, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_DEFAULT,
		1024, 768, { 512, 1536, 552, 1464 },
		0,0,0,0, // Raster Graphix
		0, 0,   // Loader
		0, 0,  // Vector Location, Size
		0 // Earom Loader
		},
		{
		"elim2a", "Eliminator (2 Player Set 2A)", rom_elim2a,
		&init_elim2,&run_segag80,&end_segag80,
		input_ports_elim2,
		elim_samples, noart,
		{CPU_MZ80,CPU_NONE,CPU_NONE,CPU_NONE},
		{3000000,0,0,0},
		{1,0,0,0},
		{1,0,0,0},
		{INT_TYPE_INT,0,0,0},
		{&sega_interrupt,0,0,0},
		40, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_DEFAULT,
		1024, 768, { 512, 1536, 552, 1464 },
		0,0,0,0, // Raster Graphix
		0, 0,   // Loader
		0, 0,  // Vector Location, Size
		0 // Earom Loader
		},
		{
		"elim2c", "Eliminator (2 Player Set 2C)", rom_elim2c,
		&init_elim2,&run_segag80,&end_segag80,
		input_ports_elim2,
		elim_samples, noart,
		{CPU_MZ80,CPU_NONE,CPU_NONE,CPU_NONE},
		{3000000,0,0,0},
		{1,0,0,0},
		{1,0,0,0},
		{INT_TYPE_INT,0,0,0},
		{&sega_interrupt,0,0,0},
		40, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_DEFAULT,
		1024, 768, { 512, 1536, 552, 1464 },
		0,0,0,0, // Raster Graphix
		0, 0,   // Loader
		0, 0,  // Vector Location, Size
		0 // Earom Loader
		},
		{
		"elim4", "Eliminator (4 Player)", rom_elim4,
		&init_elim4,&run_segag80,&end_segag80,
		input_ports_elim4,
		elim_samples, noart,
		{CPU_MZ80,CPU_NONE,CPU_NONE,CPU_NONE},
		{3000000,0,0,0},
		{1,0,0,0},
		{1,0,0,0},
		{INT_TYPE_INT,0,0,0},
		{&sega_interrupt,0,0,0},
		40, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_DEFAULT,
		1024, 768, { 512, 1536, 552, 1464 },
		0,0,0,0, // Raster Graphix
		0, 0,   // Loader
		0, 0,  // Vector Location, Size
		0 // Earom Loader
		},
		{
		"elim4p", "Eliminator (4 Player Prototype)", rom_elim4p,
		&init_elim4,&run_segag80,&end_segag80,
		input_ports_elim4,
		elim_samples, noart,
		{ CPU_MZ80,CPU_NONE,CPU_NONE,CPU_NONE},
		{3000000,0,0,0},
		{1,0,0,0},
		{1,0,0,0},
		{INT_TYPE_INT,0,0,0},
		{&sega_interrupt,0,0,0},
		40, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_DEFAULT,
		1024, 768, { 512, 1536, 552, 1464 },
		0,0,0,0, // Raster Graphix
		0, 0,   // Loader
		0, 0,  // Vector Location, Size
		0 // Earom Loader
		},

		{
		"mhavoc", "Major Havoc (Revision 3)", rom_mhavoc,
		&init_mhavoc,&run_mhavoc,&end_mhavoc,
		input_ports_mhavoc,
		0, noart,
		{CPU_M6502,CPU_M6502,CPU_NONE,CPU_NONE},
		{2500000,1250000,0,0},
		{400,400,0,0},
		{100,100,0,0},
		{INT_TYPE_INT,INT_TYPE_NONE,0,0},
		{&mhavoc_interrupt,0,0,0},
		50,VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_DEFAULT,
		//1024, 768, { 0, 300, 2, 260 },
		1024, 768, { 0, 341, 0, 260 },
		0,0,0,0,
		0,0,
	    0x4000, 0x1000,
		mhavoc_nvram_handler
		},
					{
		"mhavoc2", "Major Havoc (Revision 2)", rom_mhavoc2,
		&init_mhavoc,&run_mhavoc,&end_mhavoc,
		input_ports_mhavoc,
		0, noart,
		{CPU_M6502,CPU_M6502,CPU_NONE,CPU_NONE},
		{2500000,1250000,0,0},
		{400,400,0,0},
		{100,100,0,0},
		{INT_TYPE_INT,INT_TYPE_NONE,0,0},
		{&mhavoc_interrupt,0,0,0},
		50,VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_DEFAULT,
		1024, 768, { 0, 341, 0, 260 },
		0,0,0,0,
		0,0,
		0x4000, 0x1000,
		mhavoc_nvram_handler
		},
		{
		"mhavocrv", "Major Havoc (Return To VAX - Mod by Jeff Askey)", rom_mhavocrv,
		& init_mhavoccrv,&run_mhavoc,&end_mhavoc,
		input_ports_mhavoc,
		0, noart,
		{CPU_M6502,CPU_M6502,CPU_NONE,CPU_NONE},
		{2500000,1250000,0,0},
		{400,400,0,0},
		{100,100,0,0},
		{INT_TYPE_INT,INT_TYPE_NONE,0,0},
		{&mhavoc_interrupt,0,0,0},
		50,VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_DEFAULT,
		1024, 768, { 0, 341, 0, 260 },
		0,0,0,0,
		0,0,
		0x4000, 0x1000,
		mhavoc_nvram_handler
		},
		{
		"mhavocpe", "Major Havoc (The Promised End 1.01 adpcm)", rom_mhavocpe,
		& init_mhavocpe,&run_mhavoc,&end_mhavoc,
		input_ports_mhavoc,
		mhavocpe_samples, noart,
		{CPU_M6502,CPU_M6502,CPU_NONE,CPU_NONE},
		{2500000,1250000,0,0},
		{400,400,0,0},
		{100,100,0,0},
		{INT_TYPE_INT,INT_TYPE_NONE,0,0},
		{&mhavoc_interrupt,0,0,0},
		50,VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_DEFAULT,
		1024, 768, { 0, 335, 0, 260 },
		0,0,0,0,
		0,0,
		0x4000, 0x1000,
		mhavoc_nvram_handler
		},
		{
		"mhavocp", "Major Havoc (Prototype)", rom_mhavocp,
		&init_mhavoc,&run_mhavoc,&end_mhavoc,
		input_ports_mhavocp,
		0, noart,
		{CPU_M6502,CPU_M6502,CPU_NONE,CPU_NONE},
		{2500000,1250000,0,0},
		{400,400,0,0},
		{100,100,0,0},
		{INT_TYPE_INT,INT_TYPE_NONE,0,0},
		{&mhavoc_interrupt,0,0,0},
		50,VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_DEFAULT,
		1024, 768, { 0, 341, 0, 260 },
		0,0,0,0,
		0,0,
		0x4000, 0x1000,
		mhavoc_nvram_handler
		},
		{
		"alphaone", " Alpha One (Major Havoc Prototype - 3 Lives)", rom_alphaone,
		& init_alphone,&run_mhavoc,&end_mhavoc,
		input_ports_alphaone,
		0, noart,
		{CPU_M6502,CPU_NONE,CPU_NONE,CPU_NONE},
		{2500000,0,0,0},
		{400,0,0,0},
		{100,0,0,0},
		{INT_TYPE_INT,INT_TYPE_NONE,0,0},
		{&mhavoc_interrupt,0,0,0},
		50,VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_DEFAULT,
		1024, 768, { 0, 512, 2, 384 },
		0,0,0,0,
		0,0,
		0x4000, 0x1000,
		mhavoc_nvram_handler
		},
		{
		"alphaonea", " Alpha One (Major Havoc Prototype - 5 Lives)", rom_alphaonea,
		& init_alphone,&run_mhavoc,&end_mhavoc,
		input_ports_alphaone,
		0, noart,
		{CPU_M6502,CPU_NONE,CPU_NONE,CPU_NONE},
		{2500000,0,0,0},
		{400,0,0,0},
		{100,0,0,0},
		{INT_TYPE_INT,INT_TYPE_NONE,0,0},
		{&mhavoc_interrupt,0,0,0},
		50,VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_DEFAULT,
		1024, 768, { 0, 512, 2, 384 },
		0,0,0,0,
		0,0,
		0x4000, 0x1000,
		mhavoc_nvram_handler
		},

	///////////////////////////////////////CINEMATRONICS////////////////////////////////////////////////

		{
		"solarq", "Solar Quest", rom_solarq,
		&init_solarq,&run_cinemat, &end_cinemat,
		input_ports_solarq,
		solarq_samples, solarq_art,
		{CPU_CCPU,CPU_NONE,CPU_NONE,CPU_NONE},
		{4980750,0,0,0},
		{1,0,0,0},
		{1,0,0,0},
		{0,0,0,0},
		{0,0,0,0},
		38 ,VIDEO_TYPE_VECTOR | VECTOR_USES_BW | VECTOR_USES_OVERLAY1, ORIENTATION_DEFAULT,
		1024, 768, {0,1024,0,768},
		0,0,0,0, // Raster Graphix
		0, 0,   // Loader
		0, 0,  // Vector Location, Size
		0 // Earom Loader
		},
		{
		"starcas", "Star Castle", rom_starcas,
		&init_starcas,&run_cinemat, &end_cinemat,
		input_ports_starcas,
		starcas_samples, starcas_art,
		{CPU_CCPU,CPU_NONE,CPU_NONE,CPU_NONE},
		{4980750,0,0,0},
		{1,0,0,0},
		{1,0,0,0},
		{0,0,0,0},
		{0,0,0,0},
		38 ,VIDEO_TYPE_VECTOR | VECTOR_USES_BW | VECTOR_USES_OVERLAY1, ORIENTATION_DEFAULT,
		1024, 768, {0,1024,0,768},
		0,0,0,0, // Raster Graphix
		0, 0,   // Loader
		0, 0,  // Vector Location, Size
		0 // Earom Loader
		},
		{
		"ripoff", "RipOff", rom_ripoff,
		&init_ripoff,&run_cinemat, &end_cinemat,
		input_ports_ripoff,
		ripoff_samples, ripoff_art,
		{CPU_CCPU,CPU_NONE,CPU_NONE,CPU_NONE},
		{4980750,0,0,0},
		{1,0,0,0},
		{1,0,0,0},
		{0,0,0,0},
		{0,0,0,0},
		38 ,VIDEO_TYPE_VECTOR | VECTOR_USES_BW, ORIENTATION_DEFAULT,
		1024, 768, {0,1024,0,768},
		0,0,0,0, // Raster Graphix
		0, 0,   // Loader
		0, 0,  // Vector Location, Size
		0 // Earom Loader
		},
		{
		"armora", "Armor Attack", rom_armora,
		&init_armora,&run_cinemat, &end_cinemat,
		input_ports_armora,
		armora_samples, armora_art,
		{CPU_CCPU,CPU_NONE,CPU_NONE,CPU_NONE},
		{4980750,0,0,0},
		{1,0,0,0},
		{1,0,0,0},
		{0,0,0,0},
		{0,0,0,0},
		38 ,VIDEO_TYPE_VECTOR | VECTOR_USES_BW | VECTOR_USES_OVERLAY2, ORIENTATION_DEFAULT,
		1024, 768, {0,1024,0,772},
		0,0,0,0, // Raster Graphix
		0, 0,   // Loader
		0, 0,  // Vector Location, Size
		0 // Earom Loader
		},
		{
		"barrier", "Barrier", rom_barrier,
		&init_barrier,&run_cinemat, &end_cinemat,
		input_ports_barrier,
		barrier_samples, barrier_art,
		{CPU_CCPU,CPU_NONE,CPU_NONE,CPU_NONE},
		{4980750,0,0,0},
		{1,0,0,0},
		{1,0,0,0},
		{0,0,0,0},
		{0,0,0,0},
		38 ,VIDEO_TYPE_VECTOR | VECTOR_USES_BW | VECTOR_USES_OVERLAY1, ORIENTATION_DEFAULT,
		1024, 768, {0,1024,0,768},
		0,0,0,0, // Raster Graphix
		0, 0,   // Loader
		0, 0,  // Vector Location, Size
		0 // Earom Loader
		},
		{
		"sundance", "Sundance", rom_sundance,
		&init_sundance,&run_cinemat, &end_cinemat,
		input_ports_sundance,
		sundance_samples, sundance_art,
		{CPU_CCPU,CPU_NONE,CPU_NONE,CPU_NONE},
		{4980750,0,0,0},
		{1,0,0,0},
		{1,0,0,0},
		{0,0,0,0},
		{0,0,0,0},
		38 ,VIDEO_TYPE_VECTOR | VECTOR_USES_BW | VECTOR_USES_OVERLAY1, ORIENTATION_ROTATE_270,
		1024, 768, {0,1024,0,768},
		0,0,0,0, // Raster Graphix
		0, 0,   // Loader
		0, 0,  // Vector Location, Size
		0 // Earom Loader
		},
		{
		"warrior", "Warrior", rom_warrior,
		&init_warrior,&run_cinemat, &end_cinemat,
		input_ports_warrior,
		warrior_samples, warrior_art,
		{CPU_CCPU,CPU_NONE,CPU_NONE,CPU_NONE},
		{4980750,0,0,0},
		{1,0,0,0},
		{1,0,0,0},
		{0,0,0,0},
		{0,0,0,0},
		38 ,VIDEO_TYPE_VECTOR | VECTOR_USES_BW, ORIENTATION_DEFAULT,
		1024, 768, {0,1024,0,780},
		0,0,0,0, // Raster Graphix
		0, 0,   // Loader
		0, 0,  // Vector Location, Size
		0 // Earom Loader
		},
		{
		"tailg", "TailGunner", rom_tailg,
		&init_tailg,&run_cinemat, &end_cinemat,
		input_ports_tailg,
		tailg_samples, tailg_art,
		{CPU_CCPU,CPU_NONE,CPU_NONE,CPU_NONE},
		{4980750,0,0,0},
		{1,0,0,0},
		{1,0,0,0},
		{0,0,0,0},
		{0,0,0,0},
		38 ,VIDEO_TYPE_VECTOR | VECTOR_USES_BW |VECTOR_USES_OVERLAY1, ORIENTATION_DEFAULT,
		1024, 768, {0,1024,0,768},
		0,0,0,0, // Raster Graphix
		0, 0,   // Loader
		0, 0,  // Vector Location, Size
		0 // Earom Loader
		},
		{
		"starhawk", "StarHawk", rom_starhawk,
		&init_starhawk,&run_cinemat, &end_cinemat,
		input_ports_starhawk,
		starhawk_samples, starhawk_art,
		{CPU_CCPU,CPU_NONE,CPU_NONE,CPU_NONE},
		{4980750,0,0,0},
		{1,0,0,0},
		{1,0,0,0},
		{0,0,0,0},
		{0,0,0,0},
		38 ,VIDEO_TYPE_VECTOR | VECTOR_USES_BW, ORIENTATION_DEFAULT,
		1024, 768, {0,1024,0,768},
		0,0,0,0, // Raster Graphix
		0, 0,   // Loader
		0, 0,  // Vector Location, Size
		0 // Earom Loader
		},
		{
		"spacewar", "SpaceWar", rom_spacewar,
		&init_spacewar,&run_cinemat, &end_cinemat,
		input_ports_spacewar,
		spacewar_samples, spacewar_art,
		{CPU_CCPU,CPU_NONE,CPU_NONE,CPU_NONE},
		{4980750,0,0,0},
		{1,0,0,0},
		{1,0,0,0},
		{0,0,0,0},
		{0,0,0,0},
		38 ,VIDEO_TYPE_VECTOR | VECTOR_USES_BW, ORIENTATION_DEFAULT,
		1024, 768, {0,1024,0,768},
		0,0,0,0, // Raster Graphix
		0, 0,   // Loader
		0, 0,  // Vector Location, Size
		0 // Earom Loader
		},
		{
		"speedfrk", "Speed Freak", rom_speedfrk,
		&init_speedfrk,&run_cinemat, &end_cinemat,
		input_ports_speedfrk,
		speedfrk_samples, speedfrk_art,
		{CPU_CCPU,CPU_NONE,CPU_NONE,CPU_NONE},
		{4980750,0,0,0},
		{1,0,0,0},
		{1,0,0,0},
		{0,0,0,0},
		{0,0,0,0},
		38 ,VIDEO_TYPE_VECTOR | VECTOR_USES_BW, ORIENTATION_DEFAULT,
	    1024, 768, {0,1024,0,768},
		0,0,0,0, // Raster Graphix
		0, 0,   // Loader
		0, 0,  // Vector Location, Size
		0 // Earom Loader
		},
		{
		"demon", "Demon", rom_demon,
		&init_demon,&run_cinemat, &end_cinemat,
		input_ports_demon,
		demon_samples, demon_art,
		{CPU_CCPU,CPU_NONE,CPU_NONE,CPU_NONE},
		{4980750,0,0,0},
		{1,0,0,0},
		{1,0,0,0},
		{0,0,0,0},
		{0,0,0,0},
		38 ,VIDEO_TYPE_VECTOR | VECTOR_USES_BW | VECTOR_USES_OVERLAY2, ORIENTATION_DEFAULT,
		1024, 768, {0,1024,0,800},
		0,0,0,0, // Raster Graphix
		0, 0,   // Loader
		0, 0,  // Vector Location, Size
		0 // Earom Loader
		},
		{
		"boxingb", "Boxing Bugs", rom_boxingb,
		&init_boxingb,&run_cinemat,
		&end_cinemat, input_ports_boxingb,
		boxingb_samples, noart,
		{CPU_CCPU,CPU_NONE,CPU_NONE,CPU_NONE},
		{4980750,0,0,0},
		{1,0,0,0},
		{1,0,0,0},
		{0,0,0,0},
		{0,0,0,0},
		38 ,VIDEO_TYPE_VECTOR | VECTOR_USES_BW, ORIENTATION_DEFAULT,
		1024, 768, {0,1024,0,768},
		0,0,0,0, // Raster Graphix
		0, 0,   // Loader
		0, 0,  // Vector Location, Size
		0 // Earom Loader
		},
		{
		"wotw", "War of the Worlds", rom_wotw,
		&init_wotw,&run_cinemat, &end_cinemat,
		input_ports_wotw,
		wotw_samples, noart,
		{CPU_CCPU,CPU_NONE,CPU_NONE,CPU_NONE},
		{4980750,0,0,0},
		{1,0,0,0},
		{1,0,0,0},
		{0,0,0,0},
		{0,0,0,0},
		38 ,VIDEO_TYPE_VECTOR | VECTOR_USES_BW, ORIENTATION_DEFAULT,
		1024, 768, {0,1024,0,768},
		0,0,0,0, // Raster Graphix
		0, 0,   // Loader
		0, 0,  // Vector Location, Size
		0 // Earom Loader
		},
		{
		"qb3", "QB-3 (prototype)", rom_qb3,
		&init_qb3,&run_cinemat, &end_cinemat,
		input_ports_qb3,
		wotw_samples, noart,
		{CPU_CCPU,CPU_NONE,CPU_NONE,CPU_NONE},
		{4980750,0,0,0},
		{1,0,0,0},
		{1,0,0,0},
		{0,0,0,0},
		{0,0,0,0},
		38 ,VIDEO_TYPE_VECTOR | VECTOR_USES_BW, ORIENTATION_DEFAULT,
		1024, 768, {0,1024,0,768},
		0,0,0,0, // Raster Graphix
		0, 0,   // Loader
		0, 0,  // Vector Location, Size
		0 // Earom Loader
		},
		{
		"quantum1", "Quantum (Revision 1)", rom_quantum1,
		&init_quantum,&run_quantum, &end_quantum,
		input_ports_quantum,
		0,quantumart,
		{CPU_68000,CPU_NONE,CPU_NONE,CPU_NONE},
		{6048000,0,0,0},
		{100,0,0,0},
		{3,0,0,0},
		{INT_TYPE_INT,0,0,0},
		{&quantum_interrupt,0,0,0},
		60,VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_ROTATE_270,
		1024, 768, 	{ 0, 900, 0, 620 },
		0,0,0,0,
		0, 0,
		0x0, 0x2000,quantum_nvram_handler
		},
		{
		"quantum", "Quantum (Revision 2)", rom_quantum,
		&init_quantum,&run_quantum, &end_quantum,
		input_ports_quantum,
		0,quantumart,
		{CPU_68000,CPU_NONE,CPU_NONE,CPU_NONE},
		{6000000,0,0,0},
		{100,0,0,0},
		{3,0,0,0},
		{INT_TYPE_68K1,0,0,0},
		{&quantum_interrupt,0,0,0},
		60,VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_ROTATE_270,
		1024, 768, 	{ 0, 900, 0, 620 },
		0,0,0,0,
		0, 0,
		0x0, 0x2000,quantum_nvram_handler
		},
		{
		"quantump", "Quantum (Prototype)", rom_quantump,
		&init_quantum,&run_quantum, &end_quantum,
		input_ports_quantum,
		0,quantumart,
		{CPU_68000,CPU_NONE,CPU_NONE,CPU_NONE},
		{6048000,0,0,0},
		{100,0,0,0},
		{3,0,0,0},
		{INT_TYPE_INT,0,0,0},
		{&quantum_interrupt,0,0,0},
		60,VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_ROTATE_270,
		1024, 768, 	{ 0, 900, 0, 620 },
		0,0,0,0,
		0, 0,
		0x0, 0x2000,quantum_nvram_handler
		},
		{
		"starwars", "Star Wars (Revision 2)", rom_starwars,
		&init_starwars,&run_starwars,&end_starwars,
		input_ports_starwars,
		0, noart,
		{CPU_M6809,CPU_M6809,CPU_NONE,CPU_NONE},
		{1512000,1512000,0,0},
		{100,100,0,0}, //168
		{6,24,0,0},  //31
		{INT_TYPE_INT,INT_TYPE_INT,0,0},
		{&starwars_interrupt,&starwars_snd_interrupt,0,0},
		30,VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_DEFAULT,
		1024, 768, { 0, 240, 0, 280 },
		0,0,0,0,
		0,0,
		0x000, 0x3000,
		starwars_nvram_handler
		},
		{
		"starwars1", "Star Wars (Revision 1)", rom_starwars1,
		& init_starwars,& run_starwars,& end_starwars,
		input_ports_starwars,
		0, noart,
		{CPU_M6809,CPU_M6809,CPU_NONE,CPU_NONE},
		{1512000,1512000,0,0},
		{100,100,0,0},
		{6,24,0,0},
		{INT_TYPE_INT,INT_TYPE_INT,0,0},
		{&starwars_interrupt,&starwars_snd_interrupt,0,0},
		30,VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_DEFAULT,
		1024, 768, { 0, 240, 0, 280 },
		0,0,0,0,
		0, 0,
		0x0, 0x3000,starwars_nvram_handler
		},
		{
		"esb", "Star Wars: Empire Strike Back", rom_esb,
		&init_esb, &run_starwars, &end_starwars,
		input_ports_esb,
		0, noart,
		{ CPU_M6809,CPU_M6809,CPU_NONE,CPU_NONE },
		{ 1512000,1512000,0,0 },
		{ 100,100,0,0 }, //168
		{ 6,24,0,0 },  //31
		{ INT_TYPE_INT,INT_TYPE_INT,0,0 },
		{ &starwars_interrupt,&starwars_snd_interrupt,0,0 },
		30, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_DEFAULT,
		1024, 768, { 0, 240, 0, 280 },
		0, 0, 0, 0,
		0, 0,
		0x000, 0x3000,
		0
		},

		{
		"aztarac", "Aztarac", rom_aztarac,
		&init_aztarac,&run_aztarac, &end_aztarac,
		input_ports_aztarac,
		0,noart,
		{CPU_68000,CPU_MZ80,CPU_NONE,CPU_NONE},
		{8000000,2000000,0,0},
		{100,100,0,0},
		{1,100,0,0},
		{INT_TYPE_68K4,INT_TYPE_NONE,0,0},
		{&aztarac_interrupt,&aztarac_sound_interrupt,0,0},
		40,VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_DEFAULT,
		1024, 768, 	{ 0, 400, 0, 300 },
		0,0,0,0,
		0, 0,
		0x0, 0x2000,aztarac_nvram_handler
		},
		{
		"invaders", "Space Invaders", rom_invaders,
		&init_invaders,&run_invaders,&end_invaders,
		input_ports_invaders,
		invaders_samples, invaders_art,
		{CPU_8080,CPU_NONE,CPU_NONE,CPU_NONE},
		{2000000,0,0,0},
		{100,0,0,0},
		{2,0,0,0},
		{INT_TYPE_INT,0,0,0},
		{&invaders_interrupt,0,0,0},
		60,VIDEO_TYPE_RASTER | VECTOR_USES_OVERLAY1, ORIENTATION_ROTATE_270,
		32 * 8, 32 * 8, { 0 * 8, 32 * 8 - 1, 0 * 8, 28 * 8 - 1 },
		0, 21 / 3, 0, init_palette,
		0,0, // HI Score Handling
		0, 0, 0 // Vector, NVRAM
		},
		{
		"invaddlx", "Space Invaders Deluxe", rom_invaddlx,
		&init_invaddlx,&run_invaders,&end_invaders,
		input_ports_invaddlx,
		invaders_samples, invaddlx_art,
		{CPU_8080,CPU_NONE,CPU_NONE,CPU_NONE},
		{2000000,0,0,0},
		{100,0,0,0},
		{2,0,0,0},
		{INT_TYPE_INT,0,0,0},
		{&invaders_interrupt,0,0,0},
		60,VIDEO_TYPE_RASTER | VECTOR_USES_OVERLAY1, ORIENTATION_ROTATE_270,
		32 * 8, 32 * 8, { 0 * 8, 32 * 8 - 1, 0 * 8, 28 * 8 - 1 },
		0, 21 / 3, 0, init_palette,
		0,0, // HI Score Handling
		0, 0, 0 // Vector, NVRAM
		},
		{
		"rallyx", "Rally-X", rom_rallyx,
		&init_rallyx,&run_rallyx,&end_rallyx,
		input_ports_rallyx,
		rallyx_samples, noart,
		{CPU_MZ80,CPU_NONE,CPU_NONE,CPU_NONE},
		{3072000,0,0,0},
		{100,0,0,0},
		{1,0,0,0},
		{INT_TYPE_INT,0,0,0},
		{&rallyx_interrupt,0,0,0},
		60, VIDEO_TYPE_RASTER , ORIENTATION_DEFAULT,
		36 * 8, 28 * 8, { 0 * 8, 36 * 8 - 1, 0 * 8, 28 * 8 - 1 },
		rallyx_gfxdecodeinfo,
		32,64 * 4 + 4 * 2,
		rallyx_vh_convert_color_prom,
		0,0, // HI Score Handling
		0, 0, 0 // Vector, NVRAM
		},

		{ 0,0,0,0,0,0,0,0,0,0}// end of array
		};


#endif