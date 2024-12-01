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






