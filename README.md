AAE is a poorly written M.A.M.E (TM) derivitave based on early MAME code, 0.29 through .90 mixed with code of my own. This emulator was created solely for my amusement and learning and is provided only as an archival experience. 

All MAME code used and abused in this emulator remains the copyright of the dedicated people who spend countless hours creating it. All MAME code should be annotated as belonging to the MAME TEAM. I thank everyone who has worked on MAME and I am in awe of all that they have created. 

That being said, 

While I am not busy working, I am reviving this 17 year old code and updating it with code I have written in the past several years, as well as fixing many of the glaring issues. I am still really happy with how the graphics look all this time later on an OLED screen with freesync, and I can't believe it still runs on modern hardware. 

To Build, add the included allegro include, lib and dll files and compile with Visual Studio 2022. See the project Include and Library folders for details.
Note: Windows 7 build and compatibility has not been tested. 

Started:
Code cleanup, moving to a more sane directory structure
Build the last revision of allegro 4 and added it to the build. (This should get completely removed at a later date)
Updated/Changed the input with code from an early revision of MAME. This fixes many of the biggest play issues. Some fixes still need to be implemented.
Added the 6809 CPU Class
Started updating and fixing the CPU code and adding the timer code.
Updating all the sound code. 

TBD: 08092024
Add back in StarWars. (done)
Fix the remaing issues with input. (done)
Finish fixing the menu and config options code. 
Add in more driver settings so my newer code will work with what's here.
Add in my 6502 CPU Class. (done) (Requires the cpu_control code to be rewritten, and this will require all the atari drivers to completely reworked.)
Add in the Z80 class I am still working on. (done)
Fix emulation in Omega Race to be real. (sort of done)
Add in the later MAME (TM) avg vector code. This requires just about all the Atari drivers to be completely rebuilt mame style, and the cpu_control code to be completely changed. See Star Wars above. This is a major cleanup effort and will take a while.
Add in the correct widescreen support class, cleanup the gl code. 
Update all ROM files to match current MAME naming.

Completed 09272024
All Inputs should now be corrected. 
All Cinematronics games should be working correctly with correct dipswitches.
Started Cleanup and Reorganization of just about every file. 
Working on cleaning up and merging the current atart avg/dvg video code. 
Just fixed the last glaring issue in my Z80 core, with that core the Sega decryption issues are fixed. TBD

Completed 10192024
Swapped out the 6502 Assembly code for my new 6502 core, one step closer to a 64 bit compile.
Starwars is back in and working correctly with a really old style emulation ala M.A.M.E(TM)
All games should be working now with correct input, even if you can't save the settings. 
Omega Race is fixed, if not emulated correctly, at least it plays well.
Next is to fix the menu system, fix some pokey sound issues and start chipping away at the more insane spagetti code.
The GUI is still an issue, with at least two memory leaks.

10/27/24 to 11/28/24

Major Fix: found and corrected a serious memory leak in the graphics core, this has been plaguing the emulator for so many years, fixes many small and (large) crashes and issues. error_tex[2], not error_tex[1]! 

Redid the PPC code on the Z80 core, invalidated the PPC on Interrupt, this fixed all the decryption on the SegaG80 games. So that's what "Spurious Interrupts" means in the M.A.M.E(tm) driver. :(
Redid most of the SegaG80 driver, all games should be working correctly. 
More Z80 changes, they are in their own changelog.
Changed the 6502 cpu core back to mame style address handling, had to touch most of the drivers to fix issues. Fixed a pokey addressing issue with Black Widow, did a lot of cleanup on Major Havoc. 
Started cleanup of the graphics code, what a mess. Lots more to do. 
General cleanup everywhere, touched nearly every driver. 
Added back Star Wars, had to use very old M.A.M.E.(TM) driver code to get Luke to speak every time, I have this running in AAE2014 with modern drivers in other, cycle accurate timing code, and it still doesn't work there 100% either. I'll come back to it, at least it's working now, if not very accurate. Added different CPU handling to accomidate this. 
Now that the games are working mostly correctly, it's back to cleanup, consolidation, improving accuracy, and getting the menu system back working for sound and video. 

11/28/24 to 12/01/24

Cleaned up and added a new AY8910 core, added it to Omega Race and added nvram handling to all drivers that support it. Omega Race should be at least fairly accurate now.  
Reorganized all the OpenGl code in an attempt to make some sense of it. Added the Mushai 68000 core to maybe use with Quantum.


12/08/24

Made "major" changes to the Major Havoc driver, fixed nvram saving, added Major Havoc - The Promised End v1.01 to the driver. Will further split out everything and refine at a later date. Going back to graphics fixes.

12/11/24

Corrected the Menu System, first pass. Sound and Graphics Menus are working again, more or less. Still more work to do here with individual game configuration saves. Moved everything around to make it look at least a little bit better. 

12/12/14 Increased rendering time with another FBO, but it worked around a major rendering flaw allowing me to composite the menu and FPS onto the screen without doing any crazy scaling everywhere to compensate for screen sizes.
The menu system still needs work, I recommend not touching it except on the gui screen.

12/17/24 Many config and Menu changes, hopefully all the menu issues are resolved. 

12/21/24 Fixed main volume being too low, made too many changes to the menu system to list. More config changes to fix sound and graphics setting issues. Now the screen resolution can only be set in the gui, or by manually editing the config file.
Adjusted sound in asteroids and asteroids deluxe. Added clipping to tempest to stop the lines from going around the bezel in certain resolutions. Made a few graphic code changes to prep for more changes. Moved the game specific INI files to their own folder. 

12/22/24 Squashed the last rendering bug affecting the vertical games and battlezone. At this point, I think all the rendering is fixed, time to add a couple of features before completely changing the cpu and atari vector rendering.

12/26/24
Cleaned up video tick counting Code, adjusted AVG timing code for Major Havoc, this seems to clear up the issue with not being able to put in the secret code in mhavocpe. Or i'm way off base and I've made things worse, I don't have a real major havoc pcb to test with.

12/27/24
Found a ridiclous bug with counting the total cycles executed in the 6502 core. It was consistently underreporting the number of executed cycles, throwing everything off. Corrected.  

12/28/24 More Major Havoc cleanups. At this point I'll call it "Meh". It works pretty well, it's only a couple of percent off now as far as cycle emulation compared to the real board. To get it near perfect is going to require a complete rewrite, working on it. 
Cleaned up and removed dead code from cpu_control.h, trying to narrow down requirements for said rewrite.

2/5/24
Major Rewrite phase 1. Consolidated DVG code, moved everything to the mame timing code, added mostly cycle accurate timing to most drivers. 
Started adding code to forklift to a complete newer core codeset. 






