A.A.E (Another Arcade Emulator)

AAE is a poorly written M.A.M.E (TM) derivitave based on early MAME code, 0.29 through .90 mixed with code of my own. This emulator was created solely for my amusement and learning and is provided only as an archival experience. 

AAE was designed to emulate Vector based arcade games as accurately as possible, visually. 

All MAME code used and abused in this emulator remains the copyright of the dedicated people who spend countless hours creating it. All MAME code should be annotated as belonging to the MAME TEAM. I thank everyone who has worked on MAME and I am in awe of all that they have created. 

While this code is based on M.A.M.E, the 6502 cpu core, Z80 cpu core, Window, Rendering, Windows Audio, Windows Input, File Loading, Artwork Handling, Memory Handling, all of these pieces are custom and are not part of the M.A.M.E code. 

While I am not busy working, I am reviving this 17 year old code and updating it with code I have written in the past several years, as well as fixing many of the glaring issues. I am still really happy with how the graphics look all this time later on an OLED screen with freesync, and I can't believe it still runs on modern hardware. 

!!!!!!Note: This code is currently undergoing a complete renovation, most command line options are not working and the GUI is removed for now. 

Please see CHANGELOG.TXT for all changes since I revived work on this emulator. 

Old Build:
To Build, add the included allegro include, lib and dll files and compile with Visual Studio 2022. See the project Include and Library folders for details.

New Build:

Requires Visual Studio 2022 and the Xaudio2, version 2.9 Microsoft.XAudio2.Redist.1.2.11 nuget package installed to build correctly. 

How to install the NuGet package:
https://learn.microsoft.com/en-us/windows/win32/xaudio2/xaudio2-redistributable

![Alt text](https://github.com/tcottrill/AAE/blob/main/images/mhavocpe.png)
![Alt text](https://github.com/tcottrill/AAE/blob/main/images/astdelux.png)
![Alt text](https://github.com/tcottrill/AAE/blob/main/images/graphics_menu.png)
![Alt text](https://github.com/tcottrill/AAE/blob/main/images/warrior.png)
