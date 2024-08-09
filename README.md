AAE is a poorly written M.A.M.E (TM) derivitave based on early MAME code, 0.29 through .90 mixed with code of my own. This emulator was created solely for my amusement and learning and is provided only as an archival experience. <br/>
<br/>
All MAME code used and abused in this emulator remains the copyright of the dedicated people who spend countless hours creating it. All MAME code should be annotated as belonging to the MAME TEAM. I thank everyone who has worked on MAME and I am in awe of all that they have created. <br/>
<br/>
That being said, <br/>
<br/>
While I am not busy working, I am reviving this 17 year old code and updating it with code I have written in the past several years, as well as fixing many of the glaring issues. I am still really happy with how the graphics look all this time later on an OLED screen with freesync, and I can't believe it still runs on modern hardware. <br/>
<br/>
To Build, add the included allegro include, lib and dll files and compile with Visual Studio 2022. See the project Include and Library folders for details.
Note: Windows 7 build and compatibility has not been tested. <br/>
<br/>
Started:
Code cleanup, moving to a more sane directory structure<br/>
Build the last revision of allegro 4 and added it to the build. (This should get completely removed at a later date)<br/>
Updated/Changed the input with code from an early revision of MAME. This fixes many of the biggest play issues. Some fixes still need to be implemented.<br/>
Added the 6809 CPU Class<br/>
Started updating and fixing the CPU code and adding the timer code.<br/>
Updating all the sound code. <br/>
<br/>
TBD: 8092024
Add back in StarWars.<br/>
Fix the remaing issues with input.<br/>
Finish fixing the menu and config options code. <br/>
Add in more driver settings so my newer code will work with what's here.<br/>
Add in my 6502 CPU Class.  (Requires the cpu_control code to be rewritten, and this will require all the atari drivers to completely reworked.)<br/>
Add in the Z80 class I am still working on. <br/>
Fix emulation in Omega Race to be real. <br/>
Add in the later MAME (TM) avg vector code. This requires just about all the Atari drivers to be completely rebuilt mame style, and the cpu_control code to be completely changed. See Star Wars above. This is a major cleanup effort and will take a while.<br/>
Add in the correct widescreen support class, cleanup the gl code. <br/>
Update all ROM files to match current MAME naming.<br/>





