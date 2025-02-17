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
#include "cinemat.h"
#include "omegrace.h"
#include "quantum.h"
#include "llander.h"
#include "earom.h"


struct AAEDriver driver[] =
{
		{
		"aae", "AAE GUI",  rom_asteroid,
		&init_gui,0,&run_gui, &end_gui,
		input_ports_gui,
		guisamples, noart,
		{CPU_NONE,CPU_NONE,CPU_NONE,CPU_NONE},
		{0,0,0,0},
		{0,0,0,0},
		{0,0,0,0},
		{0,0,0,0},
		{0,0,0,0},
		60, VEC_COLOR,0,
		{0,1024,0,812},
		0, 0,
		0x4000, 0x800,0
		},

		{
		"asteroi1", "Asteroids (Revision 1)", rom_asteroi1,
		&init_asteroid,0,&run_asteroid,&end_asteroid,
		input_ports_asteroid,
		asteroidsamples,asteroidsart,
		{CPU_M6502,CPU_NONE,CPU_NONE,CPU_NONE},
		{1512000,0,0,0},
		{100,0,0,0},
		{4,0,0,0},
		{INT_TYPE_NMI,0,0,0},
		{&asteroid_interrupt,0,0,0},
		60,VEC_BW_16,0,
		{0,1024,0,812},
		asteroid1_hiload, asteroid1_hisave,
		0x4000, 0x800,0
		},
		{ "asteroid", "Asteroids (Revision 2)", rom_asteroid
		,&init_asteroid,0, &run_asteroid,&end_asteroid,
		input_ports_asteroid,
		asteroidsamples,asteroidsart,
		{CPU_M6502,CPU_NONE,CPU_NONE,CPU_NONE},
		{1512000,0,0,0},
		{100,0,0,0},
		{4,0,0,0},
		{INT_TYPE_NMI,0,0,0},
		{&asteroid_interrupt,0,0,0},
		60,VEC_BW_16,0,
		{0,1024,0,812},
		asteroid_hiload, asteroid_hisave,
		0x4000, 0x800,0
		},
		{
		"astdelu1", "Asteroids Deluxe (Revision 1)", rom_astdelu1,
		&init_astdelux,0, &run_astdelux,&end_astdelux,
		input_ports_astdelux,
		deluxesamples, astdelu1art,
		{CPU_M6502,0,0,0},
		{1512000,0,0,0},
		{100,0,0,0},
		{4,0,0,0},
		{INT_TYPE_NMI,0,0,0},
		{&asteroid_interrupt,0,0,0},
		60,VEC_BW_16,0,
		{0,1024,0,812},
		0, 0,
		0x4000, 0x800,
		atari_vg_earom_handler
		},
		{
		"astdelu2", "Asteroids Deluxe (Revision 2)", rom_astdelu2,
		&init_astdelux,0, &run_astdelux,&end_astdelux,
		input_ports_astdelux,
		deluxesamples, astdeluxart,
		{CPU_M6502,CPU_NONE,CPU_NONE,CPU_NONE},
		{1512000,0,0,0},
		{100,0,0,0},
		{4,0,0,0},
		{INT_TYPE_NMI,0,0,0},
		{&asteroid_interrupt,0,0,0},
		60,VEC_BW_16,0,
		{0,1024,0,812},
		0, 0,
		0x4000, 0x800,
		atari_vg_earom_handler
		},
		{
		"astdelux", "Asteroids Deluxe (Revision 3)", rom_astdelux,
		&init_astdelux,0, & run_astdelux,&end_astdelux,
		input_ports_astdelux,
		deluxesamples, astdeluxart,
		{CPU_M6502,CPU_NONE,CPU_NONE,CPU_NONE},
		{1512000,0,0,0},
		{100,0,0,0},
		{4,0,0,0},
		{INT_TYPE_NMI,0,0,0},
		{&asteroid_interrupt,0,0,0},
		60,VEC_BW_16,0,
		{0,1024,0,812},
		0, 0,
		0x4000, 0x800,
		atari_vg_earom_handler
		},

		{
		"meteorts", "Meteorites (Asteroids Bootleg)", rom_meteorts,
		&init_asteroid,0,&run_asteroid,&end_asteroid,
		input_ports_asteroid,
		asteroidsamples, asteroidsart,
		{CPU_M6502,CPU_NONE,CPU_NONE,CPU_NONE},
		{1512000,0,0,0},
		{100,0,0,0},
		{4,0,0,0},
		{INT_TYPE_NMI,0,0,0},
		{&asteroid_interrupt,0,0,0},
		60,VEC_BW_16 ,0,
		{0,1024,0,812},
		0, 0,
		0x4000, 0x800,0
		},
			/*
		{
		"asterock", "Asterock (Asteroids Bootleg)", rom_asterock,
		&init_asteroid,0,&run_asteroids, &end_asteroids,
		input_ports_asteroid,
		asteroidsamples, asteroidsart,
		{CPU_M6502,CPU_NONE,CPU_NONE,CPU_NONE},
		{1512000,0,0,0},
		{4,0,0,0},
		{1,0,0,0},
		{INT_TYPE_NMI,0,0,0},
		{&asteroid_interrupt,0,0,0},
		60,VEC_BW_16,0,
		{0,1024,0,812},
		asteroid_load_hi, asteroid_save_hi,
		0x4000, 0x800,0
		},
		*/
		{
		"asteroib", "Asteroids (Bootleg on Lunar Lander Hardware)",  rom_asteroib,
		&init_asteroid,0,&run_asteroid,&end_asteroid,
		input_ports_asteroid,
		asteroidsamples,asteroidsart,
		{CPU_M6502,CPU_NONE,CPU_NONE,CPU_NONE},
		{1512000,0,0,0},
		{100,0,0,0},
		{4,0,0,0},
		{INT_TYPE_NMI,0,0,0},
		{&asteroid_interrupt,0,0,0},
		60,VEC_BW_16,0,
		{0,1024,0,812},
		0, 0,
		0x4000, 0x800,0
		},
		{
		"llander1", "Lunar Lander (Revision 1)", rom_llander,
		&init_llander,0, &run_llander,&end_llander,
		input_ports_llander,
		llander_samples, noart,
		{CPU_M6502,CPU_NONE,CPU_NONE,CPU_NONE},
		{1512000,0,0,0},
		{100,0,0,0},
		{6,0,0,0},
		{INT_TYPE_NMI,0,0,0},
		{&llander_interrupt,0,0,0},
		40,VEC_BW_16,0,
		{0,1024,0,812},
		0,0,
		0x4000, 0x800,0
		},
		{
		"llander", "Lunar Lander (Revision 2)", rom_llander,
		&init_llander,0,&run_llander,&end_llander,
		input_ports_llander,
		llander_samples, noart,
		{CPU_M6502,CPU_NONE,CPU_NONE,CPU_NONE},
		{1512000,0,0,0},
		{100,0,0,0},
		{6,0,0,0},
		{INT_TYPE_NMI,0,0,0},
		{&llander_interrupt,0,0,0},
		40,VEC_BW_16,0,
		{0,1024,0,812},
		0,0,
		0x4000, 0x800,0
		},
	   {
		"omegrace", "Omega Race", rom_omegrace,
		&init_omega,0,&run_omega, &end_omega,
		input_ports_omegrace,
		0,omegarace_art,
		{CPU_MZ80,CPU_MZ80,CPU_NONE,CPU_NONE},
		{3020000,1512000,0,0},
		{100,100,0,0},
		{25,25,0,0},
		{INT_TYPE_INT,INT_TYPE_INT,0,0},
		{&omega_interrupt,omega_nmi_interrupt,0,0},
		40,VEC_BW_16,0,
		{0,1024,0,812},
		0, 0,
	    0x8000, 0x1000,nvram_handler
		},
		{
		"deltrace", "Delta Race (Omega Race Bootleg)", rom_deltrace,
		&init_omega,0,&run_omega, &end_omega,
		input_ports_omegrace,
		0,omegarace_art,
		{CPU_MZ80,CPU_MZ80,CPU_NONE,CPU_NONE},
		{3020000,1512000,0,0},
		{100,100,0,0},
		{25,25,0,0},
		{INT_TYPE_INT,0,0,0},
		{&omega_interrupt,omega_nmi_interrupt,0,0},
		40,VEC_BW_16,0,
		{0,1024,0,812},
		0, 0,
	    0x8000, 0x1000,nvram_handler
		},
		{
		"bzone", "Battlezone (Revision 1)", rom_bzone,
		&init_bzone,0,&run_bzone, &end_bzone,
		input_ports_bzone,
		bzonesamples, bzoneart,
		{CPU_M6502,CPU_NONE,CPU_NONE,CPU_NONE},
		{1512000,0,0,0},
		{100,0,0,0},
		{1,0,0,0},
		{INT_TYPE_NMI,0,0,0},
		{0,0,0,0},
		40 ,VEC_BW_16,0,
		{0,1024,0,812},
		0, 0,
		0x2000, 0x1000,
		atari_vg_earom_handler
		},
		{
		"bzone2", "Battlezone (Revision 2)", rom_bzone2,
		&init_bzone,0,&run_bzone, &end_bzone,
		input_ports_bzone,
		bzonesamples, bzoneart,
		{CPU_M6502,CPU_NONE,CPU_NONE,CPU_NONE},
		{1512000,0,0,0},
		{100,0,0,0},
		{1,0,0,0},
		{INT_TYPE_NMI,0,0,0},
		{0,0,0,0},
		40 ,VEC_BW_16,0,
		{0,1024,0,812},
		0, 0,
		0x2000, 0x1000,
		atari_vg_earom_handler
		},
		{
		"bzonec", "Battlezone Cocktail Proto", rom_bzonec,
		&init_bzone,0,&run_bzone, &end_bzone,
		input_ports_bzone,
		bzonesamples, bzoneart,
		{CPU_M6502,CPU_NONE,CPU_NONE,CPU_NONE},
		{1512000,0,0,0},
		{100,0,0,0},
		{1,0,0,0},
		{INT_TYPE_NMI,0,0,0},
		{0,0,0,0},
		40 ,VEC_BW_16,0,
		{0,1024,0,812},
		0, 0,
		0x2000, 0x1000,
		atari_vg_earom_handler
		},
		{
		"bzonep", "Battlezone Plus (Clay Cowgill)", rom_bzonep,
		&init_bzone,0,&run_bzone, &end_bzone,
		input_ports_bzone,
		bzonesamples, bzoneart,
		{CPU_M6502,CPU_NONE,CPU_NONE,CPU_NONE},
		{1512000,0,0,0},
		{100,0,0,0},
		{1,0,0,0},
		{INT_TYPE_NMI,0,0,0},
		{0,0,0,0},
		40 ,VEC_BW_16,0,
		{0,1024,0,812},
		0, 0,
		0x2000, 0x1000,
		atari_vg_earom_handler
		},
		{
		"redbaron", "Red Baron", rom_redbaron,
		& init_redbaron,0,&run_bzone, &end_bzone,
		input_ports_redbaron,
		redbaron_samples, redbaronart,
		{CPU_M6502,CPU_NONE,CPU_NONE,CPU_NONE},
		{1512000,0,0,0},
		{100,0,0,0},
		{1,0,0,0},
		{0,0,0,0},
		{0,0,0,0},
		60,VEC_BW_16,0,
		{0,1024,0,812},
		0, 0,
		0x2000, 0x1000,
		atari_vg_earom_handler
		},
		{
		"bradley", "Bradley Trainer", rom_bradley,
		&init_bzone,0,&run_bzone, &end_bzone,
		input_ports_bzone,
		bzonesamples, noart,
		{CPU_M6502,CPU_NONE,CPU_NONE,CPU_NONE},
		{1512000,0,0,0},
		{100,0,0,0},
		{1,0,0,0},
		{INT_TYPE_NMI,0,0,0},
		{0,0,0,0},
		45 ,VEC_BW_16,0,
		{0,1024,0,812},
		0, 0,
		0x2000, 0x1000,
		atari_vg_earom_handler
		},
		{
		"spacduel", "Space Duel", rom_spacduel,
		&init_spacduel,0,& run_bwidow,&end_bwidow,//&set_sd
		input_ports_spacduel,
		0, noart,
		{CPU_M6502,CPU_NONE,CPU_NONE,CPU_NONE},
		{1512000,0,0,0},
		{100,0,0,0},
		{1,0,0,0},
		{INT_TYPE_INT,0,0,0},
		{0,0,0,0},
		45,VEC_COLOR,0,
		{0,1024,0,812},
		0, 0,
		0x2000, 0x800,
		atari_vg_earom_handler
		},
		{
		"bwidow",   "Black Widow", rom_bwidow,
		& init_bwidow,0,& run_bwidow,&end_bwidow,
		input_ports_bwidow,
		0, noart,
		{CPU_M6502,CPU_NONE,CPU_NONE,CPU_NONE},
		{1512000,0,0,0},
		{100,0,0,0},
		{1,0,0,0},
		{INT_TYPE_INT,0,0,0},
		{0,0,0,0},
		60,VEC_COLOR,0,
		{0,1024,0,812},
		0, 0,
		0x2000, 0x800,
		atari_vg_earom_handler
		},
		{
		"gravitar", "Gravitar (Revision 3)", rom_gravitar,
		& init_bwidow,0,& run_bwidow,&end_bwidow,
		input_ports_gravitar,
		0, noart,
		{CPU_M6502,CPU_NONE,CPU_NONE,CPU_NONE},
		{1515000,0,0,0},
		{100,0,0,0},
		{1,0,0,0},
		{INT_TYPE_INT,0,0,0},
		{0,0,0,0},
		60,VEC_COLOR,0,
		{0,1024,0,812},
		0, 0,
		0x2000, 0x800,
		atari_vg_earom_handler
		},
		{
		"gravitr2", "Gravitar (Revision 2)", rom_gravitr2,
		& init_bwidow,0,& run_bwidow,&end_bwidow,
		input_ports_gravitar,
		0, noart,
		{CPU_M6502,CPU_NONE,CPU_NONE,CPU_NONE},
		{1515000,0,0,0},
		{100,0,0,0},
		{1,0,0,0},
		{INT_TYPE_INT,0,0,0},
		{0,0,0,0},
		60,VEC_COLOR,0,
		{0,1024,0,812},
		0, 0,
		0x2000, 0x800,
		atari_vg_earom_handler
		},
		{
		"gravp", "Gravitar (Prototype)", rom_gravp,
		&init_bwidow,0,& run_bwidow,&end_bwidow,
		input_ports_gravitar,
		0, noart,
		{CPU_M6502,CPU_NONE,CPU_NONE,CPU_NONE},
		{1515000,0,0,0},
		{100,0,0,0},
		{1,0,0,0},
		{INT_TYPE_INT,0,0,0},
		{0,0,0,0},
		60,VEC_COLOR,0,
		{0,1024,0,812},
		0, 0,
		0x2000, 0x800,
		atari_vg_earom_handler
		},
		{
		"lunarbat", "Lunar Battle (Prototype, Late)", rom_lunarbat,
		& init_bwidow,0,& run_bwidow,&end_bwidow,
		input_ports_gravitar,
		0, noart,
		{CPU_M6502,CPU_NONE,CPU_NONE,CPU_NONE},
		{1512000,0,0,0},
		{100,0,0,0},
		{1,0,0,0},
		{INT_TYPE_INT,0,0,0},
		{0,0,0,0},
		45,VEC_COLOR,0,
		{0,1024,0,812},
		0, 0,
		0x2000, 0x800,
		atari_vg_earom_handler
		},
		{
		"lunarba1", "Lunar Battle (Prototype, Early)", rom_lunarba1,
		&init_spacduel,0,& run_bwidow,&end_bwidow,
		input_ports_gravitar,
		0, noart,
		{CPU_M6502,CPU_NONE,CPU_NONE,CPU_NONE},
		{1512000,0,0,0},
		{100,0,0,0},
		{1,0,0,0},
		{INT_TYPE_INT,0,0,0},
		{0,0,0,0},
		45,VEC_COLOR ,0,
		{0,1024,0,812},
		0, 0,
		0x2000, 0x800,
		atari_vg_earom_handler
		},

		{
		"tempestm", "Tempest Multigame (1999 Clay Cowgill)", rom_tempestm,
		&init_tempest,0,&run_tempest,&end_tempest,
		input_ports_tempest,
		0, tempestart,
		{CPU_M6502,CPU_NONE,CPU_NONE,CPU_NONE},
		{1512000,0,0,0},
		{100,0,0,0},
		{4,0,0,0},
		{INT_TYPE_INT,0,0,0},
		{&tempest_interrupt,0,0,0},
		60, VEC_COLOR,1,
		{0,1024,0,812},
		0, 0,
		0x2000, 0x1000,
		atari_vg_earom_handler
		},
		{
		"tempest", "Tempest (Revision 3)", rom_tempest,
		&init_tempest,0,&run_tempest,&end_tempest,
		input_ports_tempest,
		0, tempestart,
		{CPU_M6502,CPU_NONE,CPU_NONE,CPU_NONE},
		{1515000,0,0,0},
		{100,0,0,0},
		{4,0,0,0},
		{INT_TYPE_INT,0,0,0},
		{&tempest_interrupt,0,0,0},
		60, VEC_COLOR,1,
		{0,1024,0,812},
		0, 0,
		0x2000, 0x1000,
		atari_vg_earom_handler
		},
		{
		"tempest3", "Tempest (Revision 2B)", rom_tempest3,
		&init_tempest,0,&run_tempest,&end_tempest,
		input_ports_tempest,
		0, tempestart,
		{CPU_M6502,CPU_NONE,CPU_NONE,CPU_NONE},
		{1512000,0,0,0},
		{100,0,0,0},
		{4,0,0,0},
		{INT_TYPE_INT,0,0,0},
		{&tempest_interrupt,0,0,0},
		60, VEC_COLOR ,1,
		{0,1024,0,812},
		0, 0,
		0x2000, 0x1000,
		atari_vg_earom_handler
		},
		{
		"tempest2", "Tempest (Revision 2A)", rom_tempest2,
		&init_tempest,0,&run_tempest,&end_tempest,
		input_ports_tempest,
		0, tempestart,
		{CPU_M6502,CPU_NONE,CPU_NONE,CPU_NONE},
		{1512000,0,0,0},
		{100,0,0,0},
		{4,0,0,0},
		{INT_TYPE_INT,0,0,0},
		{&tempest_interrupt,0,0,0},
		60, VEC_COLOR,1,
		{0,1024,0,812},
		0, 0,
		0x2000, 0x1000,
		atari_vg_earom_handler
		},
		{
		"tempest1", "Tempest (Revision 1)", rom_tempest1,
		&init_tempest,0,&run_tempest,&end_tempest,
		input_ports_tempest,
		0, tempestart,
		{CPU_M6502,CPU_NONE,CPU_NONE,CPU_NONE},
		{1512000,0,0,0},
		{100,0,0,0},
		{4,0,0,0},
		{INT_TYPE_INT,0,0,0},
		{&tempest_interrupt,0,0,0},
		60, VEC_COLOR,1,
		{0,1024,0,812},
		0, 0,
		0x2000, 0x1000,
		atari_vg_earom_handler
		},
		{
		"temptube", "Tempest (Revision 1)", rom_temptube,
		&init_tempest,0,&run_tempest,&end_tempest,
		input_ports_tempest,
		0, tempestart,
		{CPU_M6502,CPU_NONE,CPU_NONE,CPU_NONE},
		{1512000,0,0,0},
		{100,0,0,0},
		{4,0,0,0},
		{INT_TYPE_INT,0,0,0},
		{&tempest_interrupt,0,0,0},
		60, VEC_COLOR,1,
		{0,1024,0,812},
		0, 0,
		0x2000, 0x1000,
		atari_vg_earom_handler
		},
		{
		"aliensv", "Aliens (Tempest Alpha)", rom_aliensv,
		&init_tempest,0,&run_tempest,&end_tempest,
		input_ports_tempest,
		0, tempestart,
		{CPU_M6502,CPU_NONE,CPU_NONE,CPU_NONE},
		{1512000,0,0,0},
		{100,0,0,0},
		{4,0,0,0},
		{INT_TYPE_INT,0,0,0},
		{&tempest_interrupt,0,0,0},
		60, VEC_COLOR,1,
		{0,1024,0,812},
		0, 0,
		0x2000, 0x1000,
		atari_vg_earom_handler
		},
		{
		"vbrakout", "Vector Breakout (1999 Clay Cowgill)", rom_vbrakout,
		&init_tempest,0,&run_tempest,&end_tempest,
		input_ports_tempest,
		0, tempestart,
		{CPU_M6502,CPU_NONE,CPU_NONE,CPU_NONE},
		{1512000,0,0,0},
		{100,0,0,0},
		{4,0,0,0},
		{INT_TYPE_INT,0,0,0},
		{&tempest_interrupt,0,0,0},
		60, VEC_COLOR,1,
		{0,1024,0,812},
		0, 0,
		0x2000, 0x1000,
		atari_vg_earom_handler
		},
		{
		"tvortex", "Vortex (Tempest Beta)", rom_vortex,
		&init_tempest,0,&run_tempest,&end_tempest,
		input_ports_tempest,
		0, tempestart,
		{CPU_M6502,CPU_NONE,CPU_NONE,CPU_NONE},
		{1512000,0,0,0},
		{100,0,0,0},
		{4,0,0,0},
		{INT_TYPE_INT,0,0,0},
		{&tempest_interrupt,0,0,0},
		60, VEC_COLOR ,1,
		{0,1024,0,812},
		0, 0,
		0x2000, 0x1000,
		atari_vg_earom_handler
		},

		{
		"zektor", "Zektor", rom_zektor,
		&init_zektor,0,&run_segag80,&end_segag80,
		input_ports_zektor,
		zektor_samples, noart,
		{CPU_MZ80,CPU_NONE,CPU_NONE,CPU_NONE},
		{3000000,0,0,0},
		{1,0,0,0},
		{1,0,0,0},
		{INT_TYPE_INT,0,0,0},
		{&sega_interrupt,0,0,0},
		40, VEC_COLOR,0,
		{0,1024,0,812},
	     0, 0,0,0,0
		},
		{
		"tacscan", "Tac/Scan", rom_tacscan,
		&init_tacscan,0,&run_segag80,&end_segag80,
		input_ports_tacscan,
		tacscan_samples, tacscanart,
		{CPU_MZ80,CPU_NONE,CPU_NONE,CPU_NONE},
		{3000000,0,0,0},
		{1,0,0,0},
		{1,0,0,0},
		{INT_TYPE_INT,0,0,0},
		{&sega_interrupt,0,0,0},
		40, VEC_COLOR,1,
		{0,1024,0,812},
		 0, 0,0,0,0
		},
		{
		"startrek", "Star Trek", rom_startrek,
		&init_startrek,0,&run_segag80,&end_segag80,
		input_ports_startrek,
		startrek_samples, startrekart,
		{ CPU_MZ80,CPU_NONE,CPU_NONE,CPU_NONE},
		{3000000,0,0,0},
		{1,0,0,0},
		{1,0,0,0},
		{INT_TYPE_INT,0,0,0},
		{&sega_interrupt,0,0,0},
		40, VEC_COLOR,0,
		{0,1024,0,812},
		 0, 0,0,0,0
		},
		{
		"spacfury", "Space Fury (Revision C)", rom_spacfury,
		&init_spacfury,0,&run_segag80,&end_segag80,
		input_ports_spacfury,
		spacfury_samples, spacfuryart,
		{ CPU_MZ80,CPU_NONE,CPU_NONE,CPU_NONE},
		{3000000,0,0,0},
		{1,0,0,0},
		{1,0,0,0},
		{INT_TYPE_INT,0,0,0},
		{&sega_interrupt,0,0,0},
		40, VEC_COLOR ,0,
		{0,1024,0,812},
		 0, 0,0,0,0
		},
		{
		"spacfura", "Space Fury (Revision A)", rom_spacfura,
		&init_spacfury,0,&run_segag80,&end_segag80,
		input_ports_spacfury,
		spacfury_samples, spacfuryart,
		{CPU_MZ80,CPU_NONE,CPU_NONE,CPU_NONE},
		{3000000,0,0,0},
		{1,0,0,0},
		{1,0,0,0},
		{INT_TYPE_INT,0,0,0},
		{&sega_interrupt,0,0,0},
		40, VEC_COLOR,0,
		{0,1024,0,812},
		 0, 0,0,0,0
		},
		{
		"spacfurb", "Space Fury (Revision B)", rom_spacfurb,
		&init_spacfury,0,&run_segag80,&end_segag80,
		input_ports_spacfury,
		spacfury_samples, spacfuryart,
		{CPU_MZ80,CPU_NONE,CPU_NONE,CPU_NONE},
		{3000000,0,0,0},
		{1,0,0,0},
		{1,0,0,0},
		{INT_TYPE_INT,0,0,0},
		{&sega_interrupt,0,0,0},
		40, VEC_COLOR,0,
		{0,1024,0,812},
		 0, 0, 0, 0, 0
		},
		{
		"elim2", "Eliminator (2 Player Set 1)", rom_elim2,
		&init_elim2,0,&run_segag80,&end_segag80,
		input_ports_elim2,
		elim_samples, noart,
		{CPU_MZ80,CPU_NONE,CPU_NONE,CPU_NONE},
		{3000000,0,0,0},
		{1,0,0,0},
		{1,0,0,0},
		{INT_TYPE_INT,0,0,0},
		{&sega_interrupt,0,0,0},
		40, VEC_COLOR ,0,
		{0,1024,0,812},
		 0, 0, 0, 0, 0
		},
		{
		"elim2a", "Eliminator (2 Player Set 2A)", rom_elim2a,
		&init_elim2,0,&run_segag80,&end_segag80,
		input_ports_elim2,
		elim_samples, noart,
		{CPU_MZ80,CPU_NONE,CPU_NONE,CPU_NONE},
		{3000000,0,0,0},
		{1,0,0,0},
		{1,0,0,0},
		{INT_TYPE_INT,0,0,0},
		{&sega_interrupt,0,0,0},
		40, VEC_COLOR,0,
		{0,1024,0,812},
		 0, 0, 0, 0, 0
		},
		{
		"elim2c", "Eliminator (2 Player Set 2C)", rom_elim2c,
		&init_elim2,0,&run_segag80,&end_segag80,
		input_ports_elim2,
		elim_samples, noart,
		{CPU_MZ80,CPU_NONE,CPU_NONE,CPU_NONE},
		{3000000,0,0,0},
		{1,0,0,0},
		{1,0,0,0},
		{INT_TYPE_INT,0,0,0},
		{&sega_interrupt,0,0,0},
		40, VEC_COLOR ,0,
		{0,1024,0,812},
		 0, 0, 0, 0, 0
		},
		{
		"elim4", "Eliminator (4 Player)", rom_elim4,
		&init_elim4,0,&run_segag80,&end_segag80,
		input_ports_elim4,
		elim_samples, noart,
		{CPU_MZ80,CPU_NONE,CPU_NONE,CPU_NONE},
		{3000000,0,0,0},
		{1,0,0,0},
		{1,0,0,0},
		{INT_TYPE_INT,0,0,0},
		{&sega_interrupt,0,0,0},
		40, VEC_COLOR,0,
		{0,1024,0,812},
		 0, 0, 0, 0, 0
		},
		{
		"elim4p", "Eliminator (4 Player Prototype)", rom_elim4p,
		&init_elim4,0,&run_segag80,&end_segag80,
		input_ports_elim4,
		elim_samples, noart,
		{ CPU_MZ80,CPU_NONE,CPU_NONE,CPU_NONE},
		{3000000,0,0,0},
		{1,0,0,0},
		{1,0,0,0},
		{INT_TYPE_INT,0,0,0},
		{&sega_interrupt,0,0,0},
		40, VEC_COLOR,0,
		{0,1024,0,812},
		 0, 0, 0, 0, 0
		},

		{
		"mhavoc", "Major Havoc (Revision 3)", rom_mhavoc,
		&init_mhavoc,0,&run_mhavoc,&end_mhavoc,
		input_ports_mhavoc,
		0, noart,
		{CPU_M6502,CPU_M6502,CPU_NONE,CPU_NONE},
		{2500000,1250000,0,0},
		{400,400,0,0},
		{100,100,0,0},
		{INT_TYPE_INT,INT_TYPE_NONE,0,0},
		{&mhavoc_interrupt,0,0,0},
		50,VEC_COLOR,0,
		{0,1024,0,812},
		0,0,
	    0x4000, 0x1000,
		mhavoc_nvram_handler
		},
					{
		"mhavoc2", "Major Havoc (Revision 2)", rom_mhavoc2,
		&init_mhavoc,0,&run_mhavoc,&end_mhavoc,
		input_ports_mhavoc,
		0, noart,
		{CPU_M6502,CPU_M6502,CPU_NONE,CPU_NONE},
		{2500000,1250000,0,0},
		{400,400,0,0},
		{100,100,0,0},
		{INT_TYPE_INT,INT_TYPE_NONE,0,0},
		{&mhavoc_interrupt,0,0,0},
		50,VEC_COLOR,0,
		{0,1024,0,812},
		0,0,
		0x4000, 0x1000,
		mhavoc_nvram_handler
		},
		{
		"mhavocrv", "Major Havoc (Return To VAX - Mod by Jeff Askey)", rom_mhavocrv,
		&init_mhavoc,0,&run_mhavoc,&end_mhavoc,
		input_ports_mhavoc,
		0, noart,
		{CPU_M6502,CPU_M6502,CPU_NONE,CPU_NONE},
		{2500000,1250000,0,0},
		{400,400,0,0},
		{100,100,0,0},
		{INT_TYPE_INT,INT_TYPE_NONE,0,0},
		{&mhavoc_interrupt,0,0,0},
		50,VEC_COLOR,0,
		{0,1024,0,812},
		0,0,
		0x4000, 0x1000,
		mhavoc_nvram_handler
		},
		{
		"mhavocpe", "Major Havoc (The Promised End 1.01 adpcm)", rom_mhavocpe,
		&init_mhavoc,0,&run_mhavoc,&end_mhavoc,
		input_ports_mhavoc,
		mhavocpe_samples, noart,
		{CPU_M6502,CPU_M6502,CPU_NONE,CPU_NONE},
		{2500000,1250000,0,0},
		{400,400,0,0},
		{100,100,0,0},
		{INT_TYPE_INT,INT_TYPE_NONE,0,0},
		{&mhavoc_interrupt,0,0,0},
		50,VEC_COLOR,0,
		{0,1024,0,812},
		0,0,
		0x4000, 0x1000,
		mhavoc_nvram_handler
		},
		{
		"mhavocp", "Major Havoc (Prototype)", rom_mhavocp,
		&init_mhavoc,0,&run_mhavoc,&end_mhavoc,
		input_ports_mhavocp,
		0, noart,
		{CPU_M6502,CPU_M6502,CPU_NONE,CPU_NONE},
		{2500000,1250000,0,0},
		{400,400,0,0},
		{100,100,0,0},
		{INT_TYPE_INT,INT_TYPE_NONE,0,0},
		{&mhavoc_interrupt,0,0,0},
		50,VEC_COLOR,0,
		{0,1024,0,812},
		0,0,
		0x4000, 0x1000,
		mhavoc_nvram_handler
		},
		{
		"alphaone", " Alpha One (Major Havoc Prototype - 3 Lives)", rom_alphaone,
		&init_mhavoc,0,&run_mhavoc,&end_mhavoc,
		input_ports_alphaone,
		0, noart,
		{CPU_M6502,CPU_NONE,CPU_NONE,CPU_NONE},
		{2500000,0,0,0},
		{400,0,0,0},
		{100,0,0,0},
		{INT_TYPE_INT,INT_TYPE_NONE,0,0},
		{&mhavoc_interrupt,0,0,0},
		50,VEC_COLOR,0,
		{0,1024,0,812},
		0,0,
		0x4000, 0x1000,
		mhavoc_nvram_handler
		},
		{
		"alphaona", " Alpha One (Major Havoc Prototype - 5 Lives)", rom_alphaona,
		&init_mhavoc,0,&run_mhavoc,&end_mhavoc,
		input_ports_alphaone,
		0, noart,
		{CPU_M6502,CPU_NONE,CPU_NONE,CPU_NONE},
		{2500000,0,0,0},
		{400,0,0,0},
		{100,0,0,0},
		{INT_TYPE_INT,INT_TYPE_NONE,0,0},
		{&mhavoc_interrupt,0,0,0},
		50,VEC_COLOR,0,
		{0,1024,0,812},
		0,0,
		0x4000, 0x1000,
		mhavoc_nvram_handler
		},

	///////////////////////////////////////CINEMATRONICS////////////////////////////////////////////////

		{
		"solarq", "Solar Quest", rom_solarq,
		&init_solarq,0,&run_cinemat, &end_cinemat,
		input_ports_solarq,
		solarq_samples, solarq_art,
		{CPU_CCPU,CPU_NONE,CPU_NONE,CPU_NONE},
		{4980750,0,0,0},
		{1,0,0,0},
		{1,0,0,0},
		{0,0,0,0},
		{0,0,0,0},
		38 ,VEC_BW_64,0,
		{0,1024,0,812},
		0, 0, 0, 0, 0
		},
		{
		"starcas", "Star Castle", rom_starcas,
		&init_starcas,0,&run_cinemat, &end_cinemat,
		input_ports_starcas,
		starcas_samples, starcas_art,
		{CPU_CCPU,CPU_NONE,CPU_NONE,CPU_NONE},
		{4980750,0,0,0},
		{1,0,0,0},
		{1,0,0,0},
		{0,0,0,0},
		{0,0,0,0},
		38 ,VEC_BW_64,0,
		{0,1024,0,812},
		0, 0, 0, 0, 0
		},
		{
		"ripoff", "RipOff", rom_ripoff,
		&init_ripoff,0,&run_cinemat, &end_cinemat,
		input_ports_ripoff,
		ripoff_samples, ripoff_art,
		{CPU_CCPU,CPU_NONE,CPU_NONE,CPU_NONE},
		{4980750,0,0,0},
		{1,0,0,0},
		{1,0,0,0},
		{0,0,0,0},
		{0,0,0,0},
		38 ,VEC_BW_64,0,
		{0,1024,0,812},
		0, 0, 0, 0, 0
		},
		{
		"armora", "Armor Attack", rom_armora,
		&init_armora,0,&run_cinemat, &end_cinemat,
		input_ports_armora,
		armora_samples, armora_art,
		{CPU_CCPU,CPU_NONE,CPU_NONE,CPU_NONE},
		{4980750,0,0,0},
		{1,0,0,0},
		{1,0,0,0},
		{0,0,0,0},
		{0,0,0,0},
		38 ,VEC_BW_64,2,
		{0,1024,0,812},
		0, 0, 0, 0, 0
		},
		{
		"barrier", "Barrier", rom_barrier,
		&init_barrier,0,&run_cinemat, &end_cinemat,
		input_ports_barrier,
		barrier_samples, barrier_art,
		{CPU_CCPU,CPU_NONE,CPU_NONE,CPU_NONE},
		{4980750,0,0,0},
		{1,0,0,0},
		{1,0,0,0},
		{0,0,0,0},
		{0,0,0,0},
		38 ,VEC_BW_64,0,
		{0,1024,0,812},
		0, 0, 0, 0, 0
		},
		{
		"sundance", "Sundance", rom_sundance,
		&init_sundance,0,&run_cinemat, &end_cinemat,
		input_ports_sundance,
		sundance_samples, sundance_art,
		{CPU_CCPU,CPU_NONE,CPU_NONE,CPU_NONE},
		{4980750,0,0,0},
		{1,0,0,0},
		{1,0,0,0},
		{0,0,0,0},
		{0,0,0,0},
		38 ,VEC_BW_64,1,
		{0,1024,0,812},
		0, 0, 0, 0, 0
		},
		{
		"warrior", "Warrior", rom_warrior,
		&init_warrior,0,&run_cinemat, &end_cinemat,
		input_ports_warrior,
		warrior_samples, warrior_art,
		{CPU_CCPU,CPU_NONE,CPU_NONE,CPU_NONE},
		{4980750,0,0,0},
		{1,0,0,0},
		{1,0,0,0},
		{0,0,0,0},
		{0,0,0,0},
		38 ,VEC_BW_64,0,
		{0,1024,0,812},
		0, 0, 0, 0, 0
		},
		{
		"tailg", "TailGunner", rom_tailg,
		&init_tailg,0,&run_cinemat, &end_cinemat,
		input_ports_tailg,
		tailg_samples, tailg_art,
		{CPU_CCPU,CPU_NONE,CPU_NONE,CPU_NONE},
		{4980750,0,0,0},
		{1,0,0,0},
		{1,0,0,0},
		{0,0,0,0},
		{0,0,0,0},
		38 ,VEC_BW_64,0,
		{0,1024,0,812},
		0, 0, 0, 0, 0
		},
		{
		"starhawk", "StarHawk", rom_starhawk,
		&init_starhawk,0,&run_cinemat, &end_cinemat,
		input_ports_starhawk,
		starhawk_samples, starhawk_art,
		{CPU_CCPU,CPU_NONE,CPU_NONE,CPU_NONE},
		{4980750,0,0,0},
		{1,0,0,0},
		{1,0,0,0},
		{0,0,0,0},
		{0,0,0,0},
		38 ,VEC_BW_64,0,
		{0,1024,0,812},
		0, 0, 0, 0, 0
		},
		{
		"spacewar", "SpaceWar", rom_spacewar,
		&init_spacewar,0,&run_cinemat, &end_cinemat,
		input_ports_spacewar,
		spacewar_samples, spacewar_art,
		{CPU_CCPU,CPU_NONE,CPU_NONE,CPU_NONE},
		{4980750,0,0,0},
		{1,0,0,0},
		{1,0,0,0},
		{0,0,0,0},
		{0,0,0,0},
		38 ,VEC_BW_64,0,
		{0,1024,0,812},
		0, 0, 0, 0, 0
		},
		{
		"speedfrk", "Speed Freak", rom_speedfrk,
		&init_speedfrk,0,&run_cinemat, &end_cinemat,
		input_ports_speedfrk,
		speedfrk_samples, speedfrk_art,
		{CPU_CCPU,CPU_NONE,CPU_NONE,CPU_NONE},
		{4980750,0,0,0},
		{1,0,0,0},
		{1,0,0,0},
		{0,0,0,0},
		{0,0,0,0},
		38 ,VEC_BW_64,0,
		{0,1024,0,812},
		0, 0, 0, 0, 0
		},
		{
		"demon", "Demon", rom_demon,
		&init_demon,0,&run_cinemat, &end_cinemat,
		input_ports_demon,
		demon_samples, demon_art,
		{CPU_CCPU,CPU_NONE,CPU_NONE,CPU_NONE},
		{4980750,0,0,0},
		{1,0,0,0},
		{1,0,0,0},
		{0,0,0,0},
		{0,0,0,0},
		38 ,VEC_BW_64,2,
		{0,1024,0,812},
		0, 0, 0, 0, 0
		},
		{
		"boxingb", "Boxing Bugs", rom_boxingb,
		&init_boxingb,0,&run_cinemat,
		&end_cinemat, input_ports_boxingb,
		boxingb_samples, noart,
		{CPU_CCPU,CPU_NONE,CPU_NONE,CPU_NONE},
		{4980750,0,0,0},
		{1,0,0,0},
		{1,0,0,0},
		{0,0,0,0},
		{0,0,0,0},
		38 ,VEC_BW_64,0,
		{0,1024,0,812},
		0, 0, 0, 0, 0
		},
		{
		"wotw", "War of the Worlds", rom_wotw,
		&init_wotw,0,&run_cinemat, &end_cinemat,
		input_ports_wotw,
		wotw_samples, noart,
		{CPU_CCPU,CPU_NONE,CPU_NONE,CPU_NONE},
		{4980750,0,0,0},
		{1,0,0,0},
		{1,0,0,0},
		{0,0,0,0},
		{0,0,0,0},
		38 ,VEC_BW_64,0,
		{0,1024,0,812},
		0, 0, 0, 0, 0
		},
		{
		"quantum1", "Quantum (Revision 1)", rom_quantum1,
		&init_quantum,0,&run_quantum, &end_quantum,
		input_ports_quantum,
		0,quantumart,
		{CPU_68000,CPU_NONE,CPU_NONE,CPU_NONE},
		{6048000,0,0,0},
		{100,0,0,0},
		{3,0,0,0},
		{INT_TYPE_INT,0,0,0},
		{&quantum_interrupt,0,0,0},
		60,VEC_COLOR,1,
		{0,1024,0,812},
		0, 0,
		0x0, 0x2000
		,0
		},
		{
		"quantum", "Quantum (Revision 2)", rom_quantum,
		&init_quantum,0,&run_quantum, &end_quantum,
		input_ports_quantum,
		0,quantumart,
		{CPU_68000,CPU_NONE,CPU_NONE,CPU_NONE},
		{6000000,0,0,0},
		{100,0,0,0},
		{3,0,0,0},
		{INT_TYPE_68K1,0,0,0},
		{&quantum_interrupt,0,0,0},
		60,VEC_COLOR,1,
		{0,1024,0,812},
		0, 0,
		0x0, 0x2000
		,0
		},
		{
		"quantump", "Quantum (Prototype)", rom_quantump,
		&init_quantum,0,&run_quantum, &end_quantum,
		input_ports_quantum,
		0,quantumart,
		{CPU_68000,CPU_NONE,CPU_NONE,CPU_NONE},
		{6048000,0,0,0},
		{100,0,0,0},
		{3,0,0,0},
		{INT_TYPE_INT,0,0,0},
		{&quantum_interrupt,0,0,0},
		60,VEC_COLOR,1,
		{0,1024,0,812},
		0, 0,
		0x0, 0x2000
		,0
		},
		{
		"starwars", "Star Wars (Revision 2)", rom_starwars,
		&init_starwars,0,&run_starwars,&end_starwars,
		input_ports_starwars,
		0, noart,
		{CPU_M6809,CPU_M6809,CPU_NONE,CPU_NONE},
		{1512000,1512000,0,0},
		{100,100,0,0}, //168
		{6,24,0,0},  //31
		{INT_TYPE_INT,INT_TYPE_INT,0,0},
		{&starwars_interrupt,&starwars_snd_interrupt,0,0},
		30,VEC_COLOR,0,
		{0,1024,0,768},
		0,0,
		0x000, 0x3000,
		0
		},
		{
		"starwar1", "Star Wars (Revision 1)", rom_starwar1,
		& init_starwars,0,& run_starwars,& end_starwars,
		input_ports_starwars,
		0, noart,
		{CPU_M6809,CPU_M6809,CPU_NONE,CPU_NONE},
		{1512000,1512000,0,0},
		{100,100,0,0},
		{6,24,0,0},
		{INT_TYPE_INT,INT_TYPE_INT,0,0},
		{0,0,0,0},
		30,VEC_COLOR,0,
		{ 0,1024,0,812 },
		0, 0,
		0x0, 0x3000,0
		},

		{ 0,0,0,0,0,0,0,0,0,0}// end of array
		};


#endif