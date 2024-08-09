AAE (Another Arcade Emulator) 2007 Tim   

AAE is a vector only arcade system emulator. It strives to recreate the experience of playing the original arcade games in the arcade,and requires the roms for the arcade machine in order to run. In some cases copies of the original sound files and artwork are required to complete the emulation experience.

It provides for simulation options that are not available in mame, and offers several features that are not legal in Mame due to their arcade preserviation charter.

Things supported here that are NOT in mame:

1. Textured dots to simulate the over bright blooming in arcade monitors.
2. Extra background sounds, such as vector chatter, hum and hiss that was always present on the real systems.
3. Cocktail screen flipping in games that need special attention, such as Asteroids, Asteroids Deluxe, and Omega Race.
5. Convergence and bowing problem simulation. (Coming Soon)
6. Vector Line bloom.


AAE is a work in progress emulator, and no games are guaranteed to run correctly or even at all. Some games run (in my opinion) quite well, and provide an excellent representation of the arcade experience.


USAGE:

AAE gamename

Command Line Options:

aae -listromstotxt will provide a text file listing all the required roms for all emulated games. 

aae gamename -listroms will list the required roms for that game to the screen.

aae gamename -verifyroms will list to the screen an audit of the roms for the game.

aae gamename -listsamples will list the required samples for that game to the screen.

aae gamename -verifysamples will list to the screen an audit of the samples for the game.

aae gamename -XRESxYRES will override the config file and start the program with the given resolution.


Running AAE from a command prompt with no game will bring up the builtin GUI. This is a simple select screen, and by pressing "Start 1" (usually 1 on the keyboard) you can start the selected game. 

Default Keys are modeled after those in Mame for ease of use:

TAB - Enter configuration Menu
F1 Service Step
F2 Service Mode
F3 Reset
F5 Load Save State
F6 Save current state
F10 Show Framerate
F12 Take snapshot

1 Start 1 player Game
2 Start 2 player Game
5 insert coin
P Pause Emulation

The rest of the keys are based on the game being played, such as left, right, left control, etc...


Nearly all game and Emu settings are accessed by pressing "TAB" and entering the configuration menu.
Dipswitch Settings, Key Configuration, various hacks, emulation options, etc are all available here. 
Each games settings are saved individually.

GETTING STARTED:

You will need mame compatible ROM files in order to run these programs. They can either be placed in the ROMS Directory, or the program can be pointed to your mame rom directory by manually editing the aae.ini file.





 

 





