Place your own Amiga ProTracker .mod files here:
  TRACK1.MOD = Magical Sound Shower
  TRACK2.MOD = Passing Breeze
  TRACK3.MOD = Splash Wave
  TRACK4.MOD = Last Wave (plays when you earn a place on the high score
               table at the end of a game - not selectable on the music
               screen like the other 3)
Enable with mod=1 in outrun.ini (needs music=1 too). A missing file falls
back to the game's normal FM music for that track.

Optionally also set mod_dsp=1 (or 2) to play these on the Falcon's DSP56001 instead
of mixing them on the main CPU (needs mod=1 too): 49170 Hz stereo, steady
tempo. Default is mod_dsp=0 (CPU mixing). See README_ATARI.md.
