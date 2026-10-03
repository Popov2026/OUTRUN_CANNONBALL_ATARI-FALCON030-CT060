**English** | [Français](#français)

## Test version for real hardware (Falcon 030 / CT60 / CT63)

This is a **test version**. The stable package is still [v0.27](https://github.com/Popov2026/outrun_cannonball_atari-falcon030-ct060/releases/tag/v0.27).
No ROMs and no `.mod` music are included (same installation as v0.27).

### Changes since v0.27
- **Joystick on the 15-pin enhanced ports:** after selecting a group of lines the game now waits briefly before
  reading them. A 68060 read them before they had settled (a 68030 and Hatari never show this).
- **DB9 joystick ports:** both are read (the mouse port and joystick port 1).
- **IKBD setup:** the "disable mouse / report joystick" commands are now sent through TOS (`Ikbdws`) instead of
  direct writes that could silently time out.
- **New: `PADTEST.TOS`**, a small joystick test that shows live what each socket returns.

### Please test on a real Falcon / CT60 and report back
1. Run `PADTEST.TOS`, move the joystick, press the buttons, and take a photo of the screen (Esc quits).
2. Run `CB060.TOS` (CT60/CT63) or `CB030.TOS` (Falcon 030): does the joystick work in the game?
3. On a CT60 normally booted with FreeMiNT, also try **without FreeMiNT** (plain TOS): set `freemint = 0` in
   `outrun.ini` and rename the ROMs to 8.3 names (table in `README_ATARI.md`). Is it smoother, is the sound cleaner?

---

## Français

## Version de test pour vrai matériel (Falcon 030 / CT60 / CT63)

Ceci est une **version de test**. Le paquet stable reste la [v0.27](https://github.com/Popov2026/outrun_cannonball_atari-falcon030-ct060/releases/tag/v0.27).
Aucune ROM ni musique `.mod` n'est incluse (même installation que la v0.27).

### Changements depuis la v0.27
- **Joystick sur les ports 15 broches :** après avoir choisi une rangée de lignes, le jeu attend un court instant
  avant de les lire. Un 68060 les lisait avant qu'elles soient stables (ni un 68030 ni Hatari ne montrent ce défaut).
- **Prises joystick 9 broches :** les deux sont lues (port souris et port joystick 1).
- **Réglage du clavier (IKBD) :** les commandes « couper la souris / signaler le joystick » passent maintenant par
  TOS (`Ikbdws`) au lieu d'écritures directes qui pouvaient échouer sans rien dire.
- **Nouveau : `PADTEST.TOS`**, un petit test qui affiche en direct ce que renvoie chaque prise joystick.

### Merci de tester sur un vrai Falcon / CT60 et de faire un retour
1. Lance `PADTEST.TOS`, bouge le joystick, appuie sur les boutons et prends une photo de l'écran (Échap pour quitter).
2. Lance `CB060.TOS` (CT60/CT63) ou `CB030.TOS` (Falcon 030) : le joystick marche-t-il dans le jeu ?
3. Sur un CT60 qui démarre normalement avec FreeMiNT, essaie aussi **sans FreeMiNT** (TOS seul) : mets
   `freemint = 0` dans `outrun.ini` et renomme les ROMs en 8.3 (table dans `README_ATARI.md`). Est-ce plus fluide,
   le son est-il plus propre ?
