-> Put your 31 OutRun revision B ROM files here (see the list in roms.txt or src/main/roms.cpp).
-> The original names (e.g. epr-10380b.133) are required. On real hardware this needs
   FreeMiNT for the long names (see README_ATARI.md), or set freemint=0 in outrun.ini
   and rename the ROMs to 8.3 names (table in README_ATARI.md).
-> ROMNAME.TOS (next to CB030.TOS / CB060.TOS) renames the ROMs for you: L = long names
   (FreeMiNT, freemint=1), S = short 8.3 names (plain TOS, freemint=0). It recognises each
   ROM by its contents, whatever its current name, and sets freemint in outrun.ini.
