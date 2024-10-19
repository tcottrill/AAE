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





