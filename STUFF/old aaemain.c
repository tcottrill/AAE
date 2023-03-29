#include "aaemain.h"
#include "globals.h"
#include <mmsystem.h>
#include "allglint.h"
#include "fonts.h"
#include "animation.h"
#include "rand.h"
#include "keysets.h"
#include "asteroid.h"
#include "spacduel.h"
#include "segag80.h"
#include "mhavoc.h"
#include "tempest.h"
#include "bzone.h"
#include "starwars.h"
#include "cinemat.h"
#include "omegrace.h"
#include "quantum.h"
#include "llander.h"
#include "gui.h"
#include "glcode.h"
#include "gameroms.h"
#include "gamedips.h"
#include "dips.h"
#include "keysets.h"
#include "gamekeys.h"
#include "gamesamp.h"
#include "gameart.h"
#include "samples.h"
#include "driver.h"

int x_override;
int y_override;

TOGGLEKEYS g_StartupToggleKeys = {sizeof(TOGGLEKEYS), 0};
FILTERKEYS g_StartupFilterKeys = {sizeof(FILTERKEYS), 0};    
STICKYKEYS g_StartupStickyKeys = {sizeof(STICKYKEYS), 0};


struct { char *desc; int x, y; } gfx_res[] = {
	
	{ "-320x240"	, 320, 240 },
	{ "-512x384"	, 512, 384 },
	{ "-640x480"	, 640, 480 },
	{ "-800x600"	, 800, 600 },
	{ "-1024x768"	, 1024, 768 },
	{ "-1152x720"	, 1152, 720 },
	{ "-1152x864"	, 1152, 864 },
	{ "-1280x1024"	, 1280, 1024 },
	{ "-1600x1200"	, 1600, 1200 },
	{ "-1680x1050"	, 1680, 1050 },
	{ "-1900x1200"	, 1900, 1200 },
	{ NULL		, 0, 0 }
	};

//FOR STARWARS AND ESB , What a nightmare.
///////////////////////////////////////////////////

void update_logic_counter2(void)
	{
		logic_counter2++;
	}
END_OF_FUNCTION(update_logic_counter2);

struct AAEDriver driver[] =
{
	{ "aae", "AAE GUI",  asterock, &init_gui,&run_gui, &end_gui, gui_dips, default_keys, guisamples, noart, {CPU_NONE,CPU_NONE,CPU_NONE,CPU_NONE},60, VEC_COLOR,0 },
	{ "llander", "Lunar Lander (Revision 2)", llander, &init_llander, &run_llander,&end_llander, llander_dips,llander_keys,llander_samples, noart,{CPU_6502Z,CPU_NONE,CPU_NONE,CPU_NONE}, 40,VEC_BW_16,0 },
	{ "llander1", "Lunar Lander (Revision 1)", llander, &init_llander, &run_llander,&end_llander, llander1_dips,llander_keys,llander_samples, noart,{CPU_6502Z,CPU_NONE,CPU_NONE,CPU_NONE}, 40,VEC_BW_16,0 },
	{ "meteorts", "Meteorites (Asteroids Bootleg)", meteorts, &init_asteroid,&run_asteroids, &end_asteroids,asteroid_dips,asteroid_keys,asteroidsamples, asteroidsart,{CPU_6502Z,CPU_NONE,CPU_NONE,CPU_NONE},60,VEC_BW_16 ,0},
	{ "asterock", "Asterock (Asteroids Bootleg)", asterock, &init_asteroid,&run_asteroids, &end_asteroids, asteroid_dips,asteroid_keys,asteroidsamples, asteroidsart,{CPU_6502Z,CPU_NONE,CPU_NONE,CPU_NONE}, 60,VEC_BW_16,0 },
	{ "asteroib", "Asteroids (Bootleg on Lunar Lander Hardware)",  asteroib, &init_asteroid ,&run_asteroids,&end_asteroids, asteroid_dips,asteroid_keys,asteroidsamples, asteroidsart,{CPU_6502Z,CPU_NONE,CPU_NONE,CPU_NONE},60,VEC_BW_16,0},
	{ "asteroi1", "Asteroids (Revision 1)", asteroi1,&init_asteroid,&run_asteroids,&end_asteroids, asteroid_dips,asteroid_keys,asteroidsamples,asteroidsart,{CPU_6502Z,CPU_NONE,CPU_NONE,CPU_NONE}, 60,VEC_BW_16,0 },
	{ "asteroid", "Asteroids (Revision 2)", asteroid ,&init_asteroid, &run_asteroids,&end_asteroids,asteroid_dips,asteroid_keys,asteroidsamples,asteroidsart,{CPU_6502Z,CPU_NONE,CPU_NONE,CPU_NONE},60,VEC_BW_16,0},
	{ "astdelu1", "Asteroids Deluxe (Revision 1)", astdelu1, &init_astdelux, &run_asteroids,&end_astdelux, astdelux_dips,astdelux_keys,deluxesamples, astdelu1art,{CPU_6502Z,CPU_NONE,CPU_NONE,CPU_NONE}, 60,VEC_BW_16,0 },
	{ "astdelu2", "Asteroids Deluxe (Revision 2)", astdelu2, &init_astdelux, &run_asteroids,&end_astdelux, astdelux_dips,astdelux_keys,deluxesamples, astdeluxart,{CPU_6502Z,CPU_NONE,CPU_NONE,CPU_NONE}, 60,VEC_BW_16,0 },
	{ "astdelux", "Asteroids Deluxe (Revision 3)", astdelux, &init_astdelux, &run_asteroids,&end_astdelux, astdelux_dips,astdelux_keys,deluxesamples, astdeluxart,{CPU_6502Z,CPU_NONE,CPU_NONE,CPU_NONE}, 60,VEC_BW_16,0},
	{ "omegrace", "Omega Race", omegrace, &init_omega,&run_omega, &end_omega, omega_dips,omega_keys,omega_samples,omegarace_art,{CPU_6502Z,CPU_NONE,CPU_NONE,CPU_NONE},40,VEC_BW_16,0 },
	{ "deltrace", "Delta Race (Omega Race Bootleg)", deltrace, &init_omega,&run_omega, &end_omega, omega_dips,omega_keys,omega_samples,omegarace_art,{CPU_6502Z,CPU_NONE,CPU_NONE,CPU_NONE},40,VEC_BW_16,0 },
	{ "bzone", "Battlezone (Revision 1)", bzone, &init_bzone,&run_bzone, &end_bzone, bzone_dips, bzone_keys, bzonesamples, bzoneart, {CPU_6502Z,CPU_NONE,CPU_NONE,CPU_NONE},40 ,VEC_BW_16,0 },
	{ "bzone2", "Battlezone (Revision 2)", bzone2, &init_bzone,&run_bzone, &end_bzone, bzone_dips, bzone_keys, bzonesamples, bzoneart,{CPU_6502Z,CPU_NONE,CPU_NONE,CPU_NONE}, 40 ,VEC_BW_16,0 },
	{ "bzonec", "Battlezone Cocktail Proto", bzonec, &init_bzone,&run_bzone, &end_bzone, bzone_dips, bzone_keys, bzonesamples, bzoneart,{CPU_6502Z,CPU_NONE,CPU_NONE,CPU_NONE}, 40 ,VEC_BW_16,0 },
	{ "bzonep", "Battlezone Plus (Clay Cowgill)", bzonep, &init_bzone,&run_bzone, &end_bzone, bzone_dips, bzone_keys, bzonesamples, bzoneart,{CPU_6502Z,CPU_NONE,CPU_NONE,CPU_NONE}, 40 ,VEC_BW_16,0 },
	{ "redbaron", "Red Baron", redbaron, &init_bzone,&run_bzone, &end_bzone, redbaron_dips, redbaron_keys, redbaron_samples, redbaronart,{CPU_6502Z,CPU_NONE,CPU_NONE,CPU_NONE}, 60 ,VEC_BW_16 },

	{ "spacduel", "Space Duel", spacduel, &init_spacduel,&run_spacduel,&end_spacduel, spacduel_dips,spacduel_keys, nosamples, noart,{CPU_6502Z,CPU_NONE,CPU_NONE,CPU_NONE}, 45,VEC_COLOR,0},
	{ "bwidow",   "Black Widow", bwidow, &init_spacduel,&run_spacduel,&end_spacduel, bwidow_dips,bwidow_keys, nosamples, noart,{CPU_6502,CPU_NONE,CPU_NONE,CPU_NONE}, 60,VEC_COLOR,0 },
	{ "gravitar", "Gravitar (Revision 3)", gravitar, &init_spacduel,&run_spacduel,&end_spacduel, gravitar_dips,gravitar_keys, nosamples, noart,{CPU_6502Z,CPU_NONE,CPU_NONE,CPU_NONE}, 60,VEC_COLOR,0 },
	{ "gravitr2", "Gravitar (Revision 2)", gravitr2, &init_spacduel,&run_spacduel,&end_spacduel, gravitar_dips,gravitar_keys, nosamples, noart,{CPU_6502Z,CPU_NONE,CPU_NONE,CPU_NONE}, 60,VEC_COLOR,0 },
	{ "gravp", "Gravitar (Prototype)", gravp, &init_spacduel,&run_spacduel,&end_spacduel, gravitar_dips,gravitar_keys, nosamples, noart,{CPU_6502Z,CPU_NONE,CPU_NONE,CPU_NONE}, 60,VEC_COLOR,0 },
	{ "lunarbat", "Lunar Battle (Prototype, Late)", lunarbat, &init_spacduel,&run_spacduel,&end_spacduel, gravitar_dips, gravitar_keys, nosamples, noart, {CPU_6502Z,CPU_NONE,CPU_NONE,CPU_NONE},45,VEC_COLOR,0 },
	{ "lunarba1", "Lunar Battle (Prototype, Early)", lunarba1, &init_spacduel,&run_spacduel,&end_spacduel, gravitar_dips, gravitar_keys, nosamples, noart, {CPU_6502Z,CPU_NONE,CPU_NONE,CPU_NONE},45,VEC_COLOR ,0},
	{ "tempestm", "Tempest Multigame (1999 Clay Cowgill)", tempestm, &init_tempest,&run_tempest,&end_tempest, tempest_dips, tempestm_keys, nosamples, tempestart,{CPU_6502Z,CPU_NONE,CPU_NONE,CPU_NONE},60, VEC_COLOR,1 },
	{ "tempest", "Tempest (Revision 3)", tempest, &init_tempest,&run_tempest,&end_tempest, tempest_dips, tempest_keys, nosamples, tempestart,{CPU_6502Z,CPU_NONE,CPU_NONE,CPU_NONE},60, VEC_COLOR,1 },
	{ "tempest3", "Tempest (Revision 2B)", tempest3, &init_tempest,&run_tempest,&end_tempest, tempest_dips, tempest_keys, nosamples, tempestart,{CPU_6502Z,CPU_NONE,CPU_NONE,CPU_NONE},60, VEC_COLOR ,1},
	{ "tempest2", "Tempest (Revision 2A)", tempest2, &init_tempest,&run_tempest,&end_tempest, tempest_dips, tempest_keys, nosamples, tempestart,{CPU_6502Z,CPU_NONE,CPU_NONE,CPU_NONE},60, VEC_COLOR,1 },
	{ "tempest1", "Tempest (Revision 1)", tempest1, &init_tempest,&run_tempest,&end_tempest, tempest_dips, tempest_keys, nosamples, tempestart,{CPU_6502Z,CPU_NONE,CPU_NONE,CPU_NONE},60, VEC_COLOR,1 },
	{ "temptube", "Tempest (Revision 1)", temptube, &init_tempest,&run_tempest,&end_tempest, tempest_dips, tempest_keys, nosamples, tempestart,{CPU_6502Z,CPU_NONE,CPU_NONE,CPU_NONE},60, VEC_COLOR,1 },
	{ "alienst", "Aliens (Tempest Alpha)", alienst, &init_tempest,&run_tempest,&end_tempest, tempest_dips, tempest_keys, nosamples, tempestart,{CPU_6502Z,CPU_NONE,CPU_NONE,CPU_NONE},60, VEC_COLOR,1 },
	{ "vbreak", "Vector Breakout (1999 Clay Cowgill)", vbreak, &init_tempest,&run_tempest,&end_tempest, tempest_dips, tempest_keys, nosamples, tempestart,{CPU_6502Z,CPU_NONE,CPU_NONE,CPU_NONE},60, VEC_COLOR,1 },
	{ "vortex", "Vortex (Tempest Beta)", vortex, &init_tempest,&run_tempest,&end_tempest, tempest_dips, tempest_keys, nosamples, tempestart,{CPU_6502Z,CPU_NONE,CPU_NONE,CPU_NONE},60, VEC_COLOR ,1},
	{ "zektor", "Zektor", zektor, &init_segag80,&run_segag80,&end_segag80, zektor_dips, zektor_keys, zektor_samples, noart, {CPU_MZ80,CPU_NONE,CPU_NONE,CPU_NONE},40, VEC_COLOR,0 },
	{ "solarq", "Solar Quest", solarq, &init_cinemat,&run_cinemat, &end_cinemat, cinemat_dips, solarq_keys, solarq_samples, solarq_art, {CPU_CCPU,CPU_NONE,CPU_NONE,CPU_NONE},38 ,VEC_BW_64,0 },
	{ "quantum", "Quantum", quantum, &init_quantum,&run_quantum, &end_quantum, quantum_dips,quantum_keys, nosamples,quantumart,{CPU_68000,CPU_NONE,CPU_NONE,CPU_NONE},60,VEC_COLOR,1 },
	{ "tacscan", "Tac/Scan", tacscan, &init_segag80,&run_segag80,&end_segag80, tacscan_dips, tacscan_keys, tacscan_samples, tacscanart, {CPU_MZ80,CPU_NONE,CPU_NONE,CPU_NONE},40, VEC_COLOR,1 },
	{ "startrek", "Star Trek", startrek, &init_segag80,&run_segag80,&end_segag80, startrek_dips, startrek_keys, startrek_samples, startrekart,{ CPU_MZ80,CPU_NONE,CPU_NONE,CPU_NONE},40, VEC_COLOR,0 },
	{ "spacfury", "Space Fury (Revision C)", spacfury, &init_segag80,&run_segag80,&end_segag80, spacfury_dips, spacfury_keys, spacfury_samples, spacfuryart,{ CPU_MZ80,CPU_NONE,CPU_NONE,CPU_NONE},40, VEC_COLOR ,0},
	{ "spacfura", "Space Fury (Revision A)", spacfura, &init_segag80,&run_segag80,&end_segag80, spacfury_dips, spacfury_keys, spacfury_samples, spacfuryart,{CPU_MZ80,CPU_NONE,CPU_NONE,CPU_NONE}, 40, VEC_COLOR,0 },
	{ "spacfurb", "Space Fury (Revision B)", spacfurb, &init_segag80,&run_segag80,&end_segag80, spacfury_dips, spacfury_keys, spacfury_samples, spacfuryart,{CPU_MZ80,CPU_NONE,CPU_NONE,CPU_NONE}, 40, VEC_COLOR,0 },
	{ "elim2", "Eliminator (2 Player Set 1)", elim2, &init_segag80,&run_segag80,&end_segag80, elim_dips, elim_keys, elim_samples, noart,{CPU_MZ80,CPU_NONE,CPU_NONE,CPU_NONE}, 40, VEC_COLOR ,0},
	{ "elim2a", "Eliminator (2 Player Set 2A)", elim2a, &init_segag80,&run_segag80,&end_segag80, elim_dips, elim_keys, elim_samples, noart,{CPU_MZ80,CPU_NONE,CPU_NONE,CPU_NONE}, 40, VEC_COLOR,0 },
	{ "elim2c", "Eliminator (2 Player Set 2C)", elim2c, &init_segag80,&run_segag80,&end_segag80, elim_dips, elim_keys, elim_samples, noart,{CPU_MZ80,CPU_NONE,CPU_NONE,CPU_NONE}, 40, VEC_COLOR ,0},
	{ "elim4", "Eliminator (4 Player)", elim4, &init_segag80,&run_segag80,&end_segag80, elim_dips, elim_keys, elim_samples, noart,{CPU_MZ80,CPU_NONE,CPU_NONE,CPU_NONE}, 40, VEC_COLOR,0 },
	{ "elim4p", "Eliminator (4 Player Prototype)", elim4p, &init_segag80,&run_segag80,&end_segag80, elim_dips, elim_keys, elim_samples, noart,{ CPU_MZ80,CPU_NONE,CPU_NONE,CPU_NONE},40, VEC_COLOR,0 },
	{ "starcas", "Star Castle", starcas, &init_cinemat,&run_cinemat, &end_cinemat, cinemat_dips, starcas_keys, starcas_samples, starcas_art, {CPU_CCPU,CPU_NONE,CPU_NONE,CPU_NONE},38 ,VEC_BW_16,2 },
	{ "armora", "Armor Attack", armora, &init_cinemat,&run_cinemat, &end_cinemat, cinemat_dips, armora_keys, armora_samples, armora_art,{CPU_CCPU,CPU_NONE,CPU_NONE,CPU_NONE},38 ,VEC_BW_16,2 },
	{ "barrier", "Barrier", barrier, &init_cinemat,&run_cinemat, &end_cinemat, cinemat_dips, barrier_keys, barrier_samples, barrier_art,{CPU_CCPU,CPU_NONE,CPU_NONE,CPU_NONE},38 ,VEC_BW_16,0 },
	{ "sundance", "Sundance", sundance, &init_cinemat,&run_cinemat, &end_cinemat, cinemat_dips, sundance_keys, sundance_samples, sundance_art,{CPU_CCPU,CPU_NONE,CPU_NONE,CPU_NONE}, 38 ,VEC_BW_64,1 },
	{ "warrior", "Warrior", warrior, &init_cinemat,&run_cinemat, &end_cinemat, cinemat_dips, warrior_keys, warrior_samples, warrior_art,{CPU_CCPU,CPU_NONE,CPU_NONE,CPU_NONE}, 38 ,VEC_BW_16,0 },
	{ "ripoff", "RipOff", ripoff, &init_cinemat,&run_cinemat, &end_cinemat, cinemat_dips, ripoff_keys, ripoff_samples, ripoff_art,{CPU_CCPU,CPU_NONE,CPU_NONE,CPU_NONE}, 38 ,VEC_BW_16,0 },
	{ "tailg", "TailGunner", tailg, &init_cinemat,&run_cinemat, &end_cinemat, cinemat_dips, tailg_keys, tailg_samples, tailg_art,{CPU_CCPU,CPU_NONE,CPU_NONE,CPU_NONE}, 38 ,VEC_BW_16,0 },
	{ "starhawk", "StarHawk (Missing Some Samples)", starhawk, &init_cinemat,&run_cinemat, &end_cinemat, cinemat_dips, starhawk_keys, starhawk_samples, starhawk_art,{CPU_CCPU,CPU_NONE,CPU_NONE,CPU_NONE}, 38 ,VEC_BW_16,0 },
	{ "spacewar", "SpaceWar", spacewar, &init_cinemat,&run_cinemat, &end_cinemat, cinemat_dips, spacewar_keys, spacewar_samples, spacewar_art, {CPU_CCPU,CPU_NONE,CPU_NONE,CPU_NONE},38 ,VEC_BW_16,0 },
	{ "speedfrk", "Speed Freak (No Sound, Input)", speedfrk, &init_cinemat,&run_cinemat, &end_cinemat, cinemat_dips, speedfrk_keys, speedfrk_samples, speedfrk_art,{CPU_CCPU,CPU_NONE,CPU_NONE,CPU_NONE}, 38 ,VEC_BW_16,0 },
	{ "demon", "Demon (No Sound)", demon, &init_cinemat,&run_cinemat, &end_cinemat, cinemat_dips, demon_keys, demon_samples, demon_art,{CPU_CCPU,CPU_NONE,CPU_NONE,CPU_NONE}, 38 ,VEC_BW_16,2 },
	{ "boxingb", "Boxing Bugs (No Sound, Input)", boxingb, &init_cinemat,&run_cinemat, &end_cinemat, cinemat_dips, boxingb_keys, boxingb_samples, noart, {CPU_CCPU,CPU_NONE,CPU_NONE,CPU_NONE},38 ,VEC_BW_64,0 },
	{ "wotw", "War of the Worlds (No Sound)", wotw, &init_cinemat,&run_cinemat, &end_cinemat, cinemat_dips, wotw_keys, nosamples, noart,{CPU_CCPU,CPU_NONE,CPU_NONE,CPU_NONE}, 38 ,VEC_BW_64,0 },
	{ "mhavoc", "Major Havoc (Revision 3)", mhavoc, &init_mhavoc,&run_mhavoc,&end_mhavoc, mhavoc_dips,mhavoc_keys, nosamples, noart, {CPU_6502Z,CPU_6502Z,CPU_NONE,CPU_NONE},50,VEC_COLOR,0},
	{ "starwars", "Star Wars (Revision 2)", starwars, &init_starwars,&run_starwars,&end_starwars, starwars_dips,starwars_keys, nosamples, noart,{CPU_6809,CPU_6809,CPU_NONE,CPU_NONE}, 30,VEC_COLOR,0},
	{ "starwar1", "Star Wars (Revision 1)", starwar1, &init_starwars,&run_starwars,&end_starwars, starwars_dips,starwars_keys, nosamples, noart,{CPU_6809,CPU_6809,CPU_NONE,CPU_NONE}, 30,VEC_COLOR,0},
	{ "mhavoc2", "Major Havoc (Revision 2)", mhavoc2, &init_mhavoc,&run_mhavoc,&end_mhavoc, mhavoc_dips,mhavoc_keys, nosamples, noart,{CPU_6502Z,CPU_6502Z,CPU_NONE,CPU_NONE}, 50,VEC_COLOR,0},
	{ "mhavocrv", "Major Havoc (Return To VAX - Mod by Jeff Askey)", mhavocrv, &init_mhavoc,&run_mhavoc,&end_mhavoc, mhavoc_dips,mhavoc_keys, nosamples, noart,{CPU_6502Z,CPU_6502Z,CPU_NONE,CPU_NONE}, 50,VEC_COLOR,0},
	{ "mhavocp", "Major Havoc (Prototype)", mhavocp, &init_mhavoc,&run_mhavoc,&end_mhavoc, mhavocp_dips,mhavoc_keys, nosamples, noart, {CPU_6502Z,CPU_6502Z,CPU_NONE,CPU_NONE},50,VEC_COLOR,0},
	{ "alphaone", " Alpha One (Major Havoc Prototype - 3 Lives)", alphaone, &init_mhavoc,&run_alphaone,&end_mhavoc, no_dips, alphaone_keys, nosamples, noart, {CPU_6502Z,CPU_6502Z,CPU_NONE,CPU_NONE},50,VEC_COLOR,0},
	{ "alphaona", " Alpha One (Major Havoc Prototype - 5 Lives)", alphaona, &init_mhavoc,&run_alphaone,&end_mhavoc, no_dips ,alphaone_keys, nosamples, noart,{CPU_6502Z,CPU_6502Z,CPU_NONE,CPU_NONE}, 50,VEC_COLOR,0},
	{ 0,0,0,0,0,0	}	/* end of array */
};

void reset_to_default_keys()
{

//gamekeys = default_keys
gamekeys = driver[0].game_keys;  //Get defaults---
retrieve_keys();	
}


int mystrcmp(const char *s1, const char *s2)
{
    while(*s1 && *s2 && *s1 == *s2) {
        s1++;
        s2++;
    }

    return *s1 - *s2;
}

void sort_games(void)
{
int go=0;
int i,b;
char tempchar[128];
int tempgame=0;
int result=0;
int loc;
            
for (i=1; i < num_games-1; i++)
 {
       strcpy(tempchar,gamelist[i].glname);    
       loc=gamelist[i].gamenum;
       for (b=i; b < num_games-1; b++)
       {
         
          result = strcmp (tempchar,gamelist[b].glname); 
		  if (result > 0) { 
             			  strcpy (tempchar,gamelist[b].glname);
                          tempgame=gamelist[b].gamenum;
						  loc = b; //save the location of this one.
                          //OK, Start the move
						  strcpy (gamelist[loc].glname,gamelist[i].glname);   //move lower stuff to new location
                          gamelist[loc].gamenum=gamelist[i].gamenum;
       
                          strcpy (gamelist[i].glname,tempchar); 
                          gamelist[i].gamenum = tempgame;
		                  } 
	   }
       
 }

}


void run_game(void)
{
	DEVMODE dvmd;
	int cx = GetSystemMetrics(SM_CXSCREEN);
   // height
    int cy = GetSystemMetrics(SM_CYSCREEN);
    //width
	int game0=0;
	int retval=0;
	int goodload=99;
	int testwidth=0;
	int testheight=0;
    int testwindow=0;
	double starttime=0;
	double gametime=0;
	double millsec = 0;
	int quickfix=0;
  
	static int framemiss=0;
	int framecount=0;
	num_samples=0;
	errorlog=0;
	
    
    EnumDisplaySettings(NULL, ENUM_CURRENT_SETTINGS, &dvmd);
	



    //Check for same resolution or need to switch.
	res_reset=1;
	testwidth=config.screenw;testheight=config.screenh;testwindow=config.windowed;
    
	setup_game_config();
	sanity_check_config();
	write_to_log("Running game %s",driver[gamenum].desc);
	if (in_gui) {if (config.windowed==testwindow && config.screenh ==testheight && config.screenw==testwidth){res_reset=0;}else {res_reset=1;}
	}
	if (x_override || y_override) 
	{
	res_reset=1;
	config.screenh = y_override;
	config.screenw = x_override;
	}
	//Check for setting greater then screen availability
	if ( config.screenh > cy || config.screenw > cx) 
	{
		allegro_message("Overriding config screen settings, \nphysical size smaller then config setting");
		config.screenh = cy; config.screenw=cx; res_reset=1;
	}
    


	if (res_reset ) {
		            write_to_log("OpenGL Init");
		            end_gl();   //Clean up any stray textures
		            init_gl();  //Reinit OpenGl
	                }
	else texture_reinit(); //This cleans up the FBO textures preping them for reuse. 


	//////////////////////////////////////////////////////////////INITIAL VARIABLES SETUP ///////////////////////////////////////////////////
	SetGammaRamp(config.gamma, config.bright,config.contrast);
	//SetGammaRamp(127,127,127);
	set_aae_leds(0,0,0 );  //Reset LEDS
	config.mainvol *=12.75;
	config.pokeyvol *=12.75; //Adjust from menu values
	config.noisevol *=12.75;
    //Check;
	if (config.noisevol > 255) config.noisevol=254;
	if (config.pokeyvol > 255) config.pokeyvol=254;
	if (config.mainvol > 255) config.mainvol=254;
    if (config.noisevol <= 0 ) config.noisevol=1;
	if (config.mainvol <= 0 ) config.mainvol=1;
	if (config.pokeyvol <= 0 ) config.pokeyvol=1;

	set_volume(config.mainvol, 0);
	SetThreadPriority(GetCurrentThread(), ABOVE_NORMAL_PRIORITY_CLASS);
	switch (config.priority)
	{
	case 4: SetPriorityClass(GetCurrentProcess(), REALTIME_PRIORITY_CLASS);break;
	case 3: SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);break;
	case 2: SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);break; 
	case 1: SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);break;
	case 0: SetPriorityClass(GetCurrentProcess(),IDLE_PRIORITY_CLASS);break;
	}
	//Input FIX, this should fix the ESC key intermittently not working due to dinput focus loss
	remove_keyboard();
	install_keyboard();
   
	//////////////////////////////////////////////////////////////END VARIABLES SETUP ///////////////////////////////////////////////////
	if (gamenum >0 ){
					 goodload=load_roms(driver[gamenum].name, driver[gamenum].rom);
					 if (goodload==0) {
						               write_to_log("Rom loading failure, exiting...");have_error=10;gamenum=0;gamefps = 60;
					                   if(!in_gui){exit(1);}
					                   }
                     }
	
	
	if (gamenum >0 ){load_artwork(driver[gamenum].artwork);resize_art_textures(); }
	setup_video_config();
	if (config.bezel==0){msx=sx;msy=sy;	esx=ex;	esy=ey;}
	else {msx=b1sx;msy=b1sy;esx=b2sx;esy=b2sy;}
		
    dips =driver[gamenum].game_dips;
	retrieve_dips();
	/////////HACK TO TEMP FIX, REMOVE!!!
	quickfix=gamenum;
	gamenum=0;frameavg=0;fps_count=0;frames=0;
	//////////////////////////
	gamekeys = default_keys;//driver[0].game_keys;  //Get defaults---
	retrieve_keys();	
	//HACK///
	gamenum=quickfix;
	gamekeys = driver[gamenum].game_keys; //Get game specific ---FIX...
	retrieve_keys();	
	gamefps = driver[gamenum].fps;
	millsec = (double) 1000 / (double) driver[gamenum].fps;
 	goodload=load_samples(driver[gamenum].game_samples, 0);
	if (goodload==0) {write_to_log("Samples loading failure, bad with no sound..."); }
  	//Now load the Ambient and menu samples
	goodload=load_samples(noise_samples, 1);
	if (goodload==0) {write_to_log("Samples loading failure, bad with no sound..."); }
	voice_init(num_samples); //INITIALIZE SAMPLE VOICES
	write_to_log("Number of samples for this game is %d",num_samples);
	setup_ambient(VECTOR);
	write_to_log("Initializing Game");
	driver[gamenum].init_game();
   if (gamenum) game0=1; else game0=0;

   while (!done)
	{  
		set_render();
		if (!paused && have_error==0) {driver[gamenum].run_game();}       
		
		render();	
	 	msg_loop();
	    
		(double) gametime=TimerGetTimeMS();
		//if (driver[gamenum].fps !=60){
		                               while (((double)(gametime)-(double)starttime)  < (double)millsec)
		                                   {
              
											   HANDLE current_thread = GetCurrentThread();
		                                        int old_priority = GetThreadPriority(current_thread);


											   if (((double)gametime -  (double) starttime) < (double) (millsec-3) )
		                                       {
												 SetThreadPriority(current_thread, THREAD_PRIORITY_TIME_CRITICAL);
												 Sleep(1);
												 SetThreadPriority(current_thread, old_priority);
											    }
		                                       (double) gametime=TimerGetTimeMS();
									       }
		                             //  }
		if (frames>60){	frameavg++; if (frameavg > 10000) {frameavg=0;fps_count=0;}
		(double) fps_count += 1000 / ((double)(gametime)-(double)starttime);}
		//if (fps_count < (driver[gamenum].fps-5)){framemiss++;write_to_log("Frame MISS#: %d Frame Count: %d",framemiss,framecount);}
		//framecount++;
		frames++;
		
		(double) starttime=TimerGetTimeMS();
		
		allegro_gl_flip();
	}
    
    if (game0==0) driver[0].end_game(); else driver[gamenum].end_game();
    reset_for_new_game();
}

/*
	
	SetGammaRamp(double gamma, double bright, double contrast)
	gamma [0,255] (increased gamma < 127, decreased gamma > 127)
	bright [0,255] (darker < 127, brighter > 127)
	contrast [0,255] (lower < 127, higher > 127)

	Thanks to 'DFrey' for posting this information in the 
	discussion boards on OpenGL.org.

*/

void reset_for_new_game()
{
//write_to_log("restarting");
free_samples();//Free and allocated Samples
write_to_log("Finished Freeing Samples");
free_game_textures(); //Free textures
write_to_log("Finished Freeing Textures");
//If Using GUI and resolution change, or not in gui
//if (in_gui && res_reset==0)
write_to_log("Done Freeing All"); //Log it.
memset(keys,0,256); //Clear all keys
config.artwork=0;
config.bezel=0;
config.overlay=0;
if (done==2) {done=0;run_game();}
if (done==3) {done=0;gamenum=0;run_game();}

}

void SetGammaRamp(double gamma, double bright, double contrast)
{
	HWND hwnd;
	HDC hDC;
    int x;
	static WORD gamma_ramp[768];
	static WORD old_gamma_ramp[768];
	double v;
	double ft;
	static int savedramp;

	hwnd = win_get_window();
    hDC = GetDC(hwnd);
	savedramp= 0;
	ft = 2.0 / 255.0;
	
	if (gamma==0 && bright==0 && contrast==0) //Restore old value on exit
	{
	SetDeviceGammaRamp(hDC, old_gamma_ramp);
	
	}
	// save old gamma ramp if this is first time modified	
	// FIXME: should get the gamma ramp anyway to see if it has been changed from what was	last set	
	if (!savedramp)
	{
		GetDeviceGammaRamp(hDC, old_gamma_ramp);
	     savedramp = 1;
	}
	
	for ( x = 0; x < 256; x++)
	{
		if (255 == gamma)
		{
			v = 0.0;
		}
		else
		{
			v = pow((double)x / 255, gamma / (255 - gamma));
		}
		
		v += ft * bright - 1.0;
		v += (ft * (double)x - 1.0) * (contrast / 255.0 -.5) * (ft * contrast + 1.0);
		v *= 65535;
		if (v<0)
		{
			v=0;
		}
		else if (v>65535)
		{
				v=65535;
		}
				
		gamma_ramp[x] = (WORD)v;
	}

	memcpy(gamma_ramp + 256, gamma_ramp, 512);
	memcpy(gamma_ramp + 512, gamma_ramp, 512);
	
	SetDeviceGammaRamp(hDC, gamma_ramp);
	
}


void gameparse(int argc, char *argv[])
{
	char *mylist;
	int x=0;
    int loop=0;
    int w=0;
	int list=0;
	int retval=0;
	int i;
	int j;


	if (gamenum ==0) return;

    for (i = 1;i < argc;i++)   
	{
		if (stricmp(argv[i],"-listroms") == 0) list = 1;
		if (stricmp(argv[i],"-verifyroms") == 0) list = 2;
		if (stricmp(argv[i],"-listsamples") == 0) list = 3;
		if (stricmp(argv[i],"-verifysamples") == 0) list = 4;
		if (strcmp(argv[2], "-debug") == 0) {config.debug=1;}	
	

	for (j=0; gfx_res[j].desc != NULL; j++)
		{
			if (stricmp(argv[i], gfx_res[j].desc) == 0)
			{
				x_override = gfx_res[j].x;
				y_override = gfx_res[j].y;
				break;
			}
		}

	}
	 
switch (list)
	{
		case 1:
    
		mylist=malloc(10000);
		strcpy(mylist,"\n");
	       while (driver[gamenum].rom[x].romSize > 0)
				 {  
					  if (driver[gamenum].rom[x].loadAddr !=0x999) {
					                                             if(driver[gamenum].rom[x].filename != (char *)-1){       
						                                             strcat(mylist,driver[gamenum].rom[x].filename);
	                                                                  if(w>1){strcat(mylist,"\n");w=0;} else {strcat(mylist," ");w++;}
																                                                   }	 
					                                                     
																     }
				  x++;	                                                  
				}
		
		strcat(mylist,"\0");
		if (gamenum>0){ allegro_message("%s rom list: %s",driver[gamenum].name,mylist);}
	    free(mylist);
		close_log();
		exit(1);break;
	

		case 2:
			  	setup_game_config(); //Have to do this so the Rom loader can find the rom path.......
	            sanity_check_config();         
				
				mylist=malloc(0x25000);
				strcpy(mylist,"\n");                
				write_to_log("Starting rom verify");
				while (driver[gamenum].rom[x].romSize > 0)
				 {  
					  if (driver[gamenum].rom[x].loadAddr !=0x999) {
					                                             if(driver[gamenum].rom[x].filename != (char *)-1){       
						                                                    strcat(mylist,driver[gamenum].rom[x].filename);
					                                                       	retval=verify_rom(driver[gamenum].name, driver[gamenum].rom , x);
					                                                       
																			switch(retval)
																			{
																			case 0: strcat(mylist," BAD? ");break;
																			case 1: strcat(mylist," OK ");break;
																			case 3: strcat(mylist," BADSIZE ");break;
																			case 4: strcat(mylist," NOFILE ");break;
  																		    case 5: strcat(mylist," NOZIP ");break;
   																		    }
																			if(w>1){strcat(mylist,"\n");w=0;} 
																			else {strcat(mylist," ");w++;}

																			 
																                                                   }	 
					                                                     
																 }
				x++;	                                                  
				}
					strcat(mylist,"\0");

					if (gamenum>0){ allegro_message("%s rom verify: %s",driver[gamenum].name,mylist);}
					free(mylist);
					close_log();
					exit(1);  break;        
	            
            	

			case 3: mylist=malloc(5000);
		            strcpy(mylist,"\n");
					while (strcmp (driver[gamenum].game_samples[i],"NULL")) {strcat(mylist,driver[gamenum].game_samples[i]);strcat(mylist," ");i++;} 
					strcat(mylist,"\0");
					if (gamenum>0) allegro_message("%s Samples: %s",driver[gamenum].name,mylist);
					free(mylist);
					close_log();
					exit(1);
					break;
			case 4: mylist=malloc(0x11000);i=0;
				    strcpy(mylist,"\n");                
				    write_to_log("Starting sample verify");
				    while (strcmp (driver[gamenum].game_samples[i],"NULL"))
				     {  
					 
					   strcat(mylist,driver[gamenum].game_samples[i]);
					   retval=verify_sample(driver[gamenum].game_samples,i);
					   
					   switch(retval)
						{
					  	 case 0: strcat(mylist," BAD? ");break;
						 case 1: strcat(mylist," OK ");break;
						 case 3: strcat(mylist," BADSIZE ");break;
						 case 4: strcat(mylist," NOFILE ");break;
  						 case 5: strcat(mylist," NOZIP ");break;
   						}
						if(w>1){strcat(mylist,"\n");w=0;} 
						else {strcat(mylist," ");w++;}
 					i++;	
					}	 
															
				                                                  
					strcat(mylist,"\0");

					if (gamenum>0){ allegro_message("%s sample verify: %s",driver[gamenum].name,mylist);}
					free(mylist);
					close_log();
					exit(1);  break;        
	
    }
   
	 
} 

void AllowAccessibilityShortcutKeys(int bAllowKeys )
{
	 

    if( bAllowKeys )
    {
        // Restore StickyKeys/etc to original state and enable Windows key      
        //STICKYKEYS sk = g_StartupStickyKeys;
        //TOGGLEKEYS tk = g_StartupToggleKeys;
        //FILTERKEYS fk = g_StartupFilterKeys;
        
        SystemParametersInfo(SPI_SETSTICKYKEYS, sizeof(STICKYKEYS), &g_StartupStickyKeys, 0);
        SystemParametersInfo(SPI_SETTOGGLEKEYS, sizeof(TOGGLEKEYS), &g_StartupToggleKeys, 0);
        SystemParametersInfo(SPI_SETFILTERKEYS, sizeof(FILTERKEYS), &g_StartupFilterKeys, 0);
    }
   
	else
    {
        // Disable StickyKeys/etc shortcuts but if the accessibility feature is on, 
        // then leave the settings alone as its probably being usefully used
 
		FILTERKEYS fkOff = g_StartupFilterKeys;
        STICKYKEYS skOff = g_StartupStickyKeys;
		TOGGLEKEYS tkOff = g_StartupToggleKeys;

        if( (skOff.dwFlags & SKF_STICKYKEYSON) == 0 )
        {
            // Disable the hotkey and the confirmation
            skOff.dwFlags &= ~SKF_HOTKEYACTIVE;
            skOff.dwFlags &= ~SKF_CONFIRMHOTKEY;
 
            SystemParametersInfo(SPI_SETSTICKYKEYS, sizeof(STICKYKEYS), &skOff, 0);
        }
	
    	 
        if( (tkOff.dwFlags & TKF_TOGGLEKEYSON) == 0 )
        {
            // Disable the hotkey and the confirmation
            tkOff.dwFlags &= ~TKF_HOTKEYACTIVE;
            tkOff.dwFlags &= ~TKF_CONFIRMHOTKEY;
 
            SystemParametersInfo(SPI_SETTOGGLEKEYS, sizeof(TOGGLEKEYS), &tkOff, 0);
        }
	
		
 
      
        if( (fkOff.dwFlags & FKF_FILTERKEYSON) == 0 )
        {
            // Disable the hotkey and the confirmation
            fkOff.dwFlags &= ~FKF_HOTKEYACTIVE;
            fkOff.dwFlags &= ~FKF_CONFIRMHOTKEY;
 
            SystemParametersInfo(SPI_SETFILTERKEYS, sizeof(FILTERKEYS), &fkOff, 0);
        }
    }


}




int main(int argc, char *argv[])
{
	int i;
	int loop=0;
	int loop2=0;
	char *mylist;

    TIMECAPS caps;
   // int goodload;
	
	allegro_init(); 
    install_allegro_gl();
	install_keyboard(); 
    install_mouse();
	install_timer();
    install_joystick(JOY_TYPE_AUTODETECT);
    reserve_voices(16, -1);
		
	if (install_sound(DIGI_AUTODETECT,MIDI_NONE, NULL)) {
		allegro_message("Can't initialize sound driver.\nThis program requires sound support, sorry.");
		return -1;
	}	
   	
	open_log("aae.log");
	gamenum=0; //Clear to make sure!
    x_override=0;
    y_override=0;
	while (driver[loop].name !=0){num_games++;loop++;}
	write_to_log("Number of supported games is: %d",num_games);
	num_games++;
	
    for (loop = 1; loop < (num_games-1); loop++) //
	{
		strcpy(gamelist[loop].glname,driver[loop].desc);
	    gamelist[loop].gamenum = loop;
		if (loop < num_games-2){gamelist[loop].next=(loop+1);}
		else {gamelist[loop].next= 1;}
				
		if (loop > 1) {gamelist[loop].prev=(loop-1);}
		else {gamelist[loop].prev=(num_games-2);}
        
		
	}
	//write_to_log("Sorting games");
	sort_games();
    loop2=0;
	loop=1;

	///TURN OFF STICKY KEYS
	 // Save the current sticky/toggle/filter key settings so they can be restored them later
    SystemParametersInfo(SPI_GETSTICKYKEYS, sizeof(STICKYKEYS), &g_StartupStickyKeys, 0);
    SystemParametersInfo(SPI_GETTOGGLEKEYS, sizeof(TOGGLEKEYS), &g_StartupToggleKeys, 0);
    SystemParametersInfo(SPI_GETFILTERKEYS, sizeof(FILTERKEYS), &g_StartupFilterKeys, 0);
 
    // Disable ShortCut Keys
    AllowAccessibilityShortcutKeys(0 );


for (i = 1;i < argc;i++){	
	if (strcmp(argv[i], "-listromstotxt") == 0)
	{
		mylist=malloc(0x50000);
		strcpy(mylist,"AAE All Games RomList\n");
		
		while( loop < (num_games-1)){
			strcat(mylist,"\n");strcat(mylist,driver[loop].name);strcat(mylist,":\n");//allegro_message("%s",mylist);
		    write_to_log("gamename %s",driver[loop].name);
			   while (driver[loop].rom[loop2].romSize > 0)
		           {
		
		             if (driver[loop].rom[loop2].loadAddr !=0x999) { 
			                                             if(driver[loop].rom[loop2].filename != (char *)-1){
		                                                                            // write_to_log("looping %s",driver[loop].rom[loop2].filename);
													                                 strcat(mylist,driver[loop].rom[loop2].filename);
														                             strcat(mylist,"\n");
														                                                     }
					                                                }
		                                                 loop2++;
		           }
		
		loop2=0; //reset!!
		loop++;}
		strcat(mylist,"\0");
		write_to_log("SAVING Romlist");
		save_file_char("AAE All Game Roms List.txt", mylist, strlen(mylist));
		free(mylist);
		close_log();
		exit(1);
	}


	
	  for (loop = 1; loop < (num_games-1); loop++)
	  {
		  if (strcmp(argv[1], driver[loop].name) == 0) gamenum=loop;
		
      }
	     if (argc > 2) gameparse(argc, argv);
  }
	
	
	//THIS IS WHERE THE CODING STARTS
	
	if (gamenum) in_gui=0; else in_gui=1;

	timeGetDevCaps(&caps,sizeof(TIMECAPS));
	write_to_log("Setting timer resolution to Min Supported: %d (ms)",caps.wPeriodMin); 
	
	write_to_log("Number of supported joysticks: %d ",num_joysticks); 
	if (num_joysticks)
	{   poll_joystick();
		for (loop = 0; loop < num_joysticks; loop++) {
		 write_to_log("joy %d number of sticks %d",loop,joy[loop].num_sticks);
		                   for (loop2 = 0; loop2 < joy[loop].num_sticks; loop2++)
						   {
						   
						       write_to_log("stick number  %d flag %d",loop2,joy[loop].stick[loop2].flags);
						       write_to_log("stick number  %d axis 0 pos %d",loop2,joy[loop].stick[loop2].axis[0].pos);
							   write_to_log("stick number  %d axis 1 pos %d",loop2,joy[loop].stick[loop2].axis[1].pos);
						   }  
			
		}
	}
	timeBeginPeriod(caps.wPeriodMin);
    frames=0; //init frame counter
   	testsw=0; //init testswitch off , had to make common for all
	config.hack=0; //Just to set, used for some games with hacks.
	showfps=0;
	keyin=1;
	gamefps=0;
	show_menu=0;
	menulevel=100;//Top Level
    menuitem=1; //TOP VAL
	showinfo=0;
	done=0;
	//////////////////////////////////////////////////////
	set_display_switch_mode(SWITCH_PAUSE);
    TimerInit(); //Start timer
	write_to_log("Number of supported games in this release: %d",num_games);

	LOCK_VARIABLE(logic_counter2);
	LOCK_FUNCTION(update_logic_counter2);
	install_int_ex(update_logic_counter2, MSEC_TO_TIMER(32));
	initrand();
	fillstars(stars); 
	have_error=0;
	key_set_flag=0;
	//ListDisplaySettings();
    
	/*
	SetThreadPriority(GetCurrentThread(), ABOVE_NORMAL_PRIORITY_CLASS);
	switch (config.priority)
	{
	case 4: SetPriorityClass(GetCurrentProcess(), REALTIME_PRIORITY_CLASS);break;
	case 3: SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);break;
	case 2: SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);break; 
	case 1: SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);break;
	case 0: SetPriorityClass(GetCurrentProcess(),IDLE_PRIORITY_CLASS);break;
	}
	*/
	
	//set_window_title("AAE BETA 2");
   // set_window_close_button(0);
   /////
	//AllocConsole();  
   // SetConsoleTitle("Alert Window");

	//freopen("CONIN$","rb",stdin);   // reopen stdin handle as console window input
    //freopen("CONOUT$","wb",stdout);  // reopen stout handle as console window output
    //freopen("CONOUT$","wb",stderr); // reopen stderr handle as console window output

	//printf("This is a console output test");
    //write_to_log("CPU TYPE FOR THIS GAME is %d %d %d %d", driver[gamenum].cputype[0],driver[gamenum].cputype[1],driver[gamenum].cputype[2],driver[gamenum].cputype[3]);

	
	run_game();
	//END-----------------------------------------------------
    timeEndPeriod(caps.wPeriodMin); 
	KillFont();
	SetGammaRamp(0, 0,0);
	set_aae_leds(0,0,0);
	AllowAccessibilityShortcutKeys(1);
    //FreeConsole( );
	close_log();
   	return 0;   
}
END_OF_MAIN();


/*

void ListDisplaySettings(void) {
  DEVMODE devMode;
  LONG    i;
  int	  x=0;
  LONG    modeExist;
   // enumerate all modes & set to current in list
  
  for (i=0; i <52;i++) {vidmodes[i].curvid=0;vidmodes[i].vidfin=0;}
  
  vidmodes[51].curvid=0; //Set defaults
  modeExist = EnumDisplaySettings(NULL, 0, &devMode);
  write_to_log("Available Screen modes for %s", devMode.dmDeviceName);
  for (i=0; modeExist;i++) {
	  if (devMode.dmDisplayFrequency < 70 && devMode.dmBitsPerPel > 8 && devMode.dmPelsHeight >=480){
	      write_to_log( "%ldx%ldx%ld-bit color (%ld Hz)", devMode.dmPelsWidth, devMode.dmPelsHeight, devMode.dmBitsPerPel, devMode.dmDisplayFrequency);
                        vidmodes[x].vidheight=devMode.dmPelsHeight;
	                    vidmodes[x].vidwidth=devMode.dmPelsWidth;
                        vidmodes[x].vidbpp=devMode.dmBitsPerPel;
						vidmodes[x].vidfin=0;
						x++;
	                    }
    modeExist = EnumDisplaySettings(NULL, i, &devMode);
  }
	vidmodes[x].vidheight=-1;
	vidmodes[x].vidwidth=-1;
	vidmodes[x].vidbpp=-1;
	vidmodes[51].vidfin=(x-1);
    //check for current mode and set//
	x=0;
	while (vidmodes[x].vidwidth !=-1 && x < 51)
	{
	if (vidmodes[x].vidwidth ==config.screenw && vidmodes[x].vidheight==config.screenh && vidmodes[x].vidbpp==config.colordepth)
	{vidmodes[51].curvid=x;write_to_log("Current res is number %d",vidmodes[51].curvid);}
	
	x++;
	}
	if (vidmodes[51].curvid==0)write_to_log("Crap, no video mode matched, interesting..?");
}
*/

