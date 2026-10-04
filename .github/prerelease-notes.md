**English** | [Français](#français)

## v0.30 beta — FM music on the DSP

No ROMs and no `.mod` music are included: add your own ROM set (OutRun revision B) in `roms/`, and
optionally your `.mod` files in `Music/` (see the README).

### What's new since v0.27
- **FM music computed by the Falcon's DSP56001** (`fm_dsp = 1`, default). The original FM music no
  longer stutters: the YM2151 is computed by a DSP program, sample for sample the same sound as the
  CPU emulation, for a fraction of the CPU time.
- **Sound from a timer interrupt** on 68040/68060 (`sound_irq = 2`, default): steady music even when the
  picture rate drops.
- **`.mod` music with DSPMOD 3.4** (bITmASTER of TCE, `mod_dsp = 2`) on the Falcon 030. With a 68060, and for
  modules with more than 64 patterns, the Simplet DSP replay is used instead (DSPMOD is not 68060-safe).
- **Joystick fixed on real CT60 hardware**, and **configurable**: `joy_accel`, `joy_brake`, `joy_gear` in
  `outrun.ini`.
- `PADTEST.TOS`: joystick test showing what each socket returns.

Tested in Hatari (Falcon 030 and 68060 with DSP emulation) and by users on Hatari 68060 + DSP and a real CT60
(joystick). On a 16 MHz Falcon 030 the game stays too slow to be really playable.

---

## Français

## v0.30 bêta — musique FM sur le DSP

Aucune ROM ni musique `.mod` n'est incluse : ajoute ton propre jeu de ROMs (OutRun révision B) dans `roms/`,
et éventuellement tes `.mod` dans `Music/` (voir le README).

### Nouveautés depuis la v0.27
- **Musique FM calculée par le DSP56001 du Falcon** (`fm_dsp = 1`, par défaut). La musique FM d'origine ne
  saccade plus : le YM2151 est calculé par un programme DSP, au son identique échantillon par échantillon à
  l'émulation CPU, pour une fraction du temps CPU.
- **Son produit par une interruption** sur 68040/68060 (`sound_irq = 2`, par défaut) : musique régulière même
  quand le nombre d'images par seconde baisse.
- **Musique `.mod` avec DSPMOD 3.4** (bITmASTER de TCE, `mod_dsp = 2`) sur Falcon 030. Avec un 68060, et pour les
  modules de plus de 64 patterns, c'est le replay DSP de Simplet qui est utilisé (DSPMOD n'est pas fiable sur 68060).
- **Joystick corrigé sur vrai CT60**, et **configurable** : `joy_accel`, `joy_brake`, `joy_gear` dans `outrun.ini`.
- `PADTEST.TOS` : test du joystick qui affiche ce que renvoie chaque prise.

Testé sous Hatari (Falcon 030 et 68060 avec émulation du DSP) et par des utilisateurs sous Hatari 68060 + DSP et
sur un vrai CT60 (joystick). Sur un Falcon 030 à 16 MHz, le jeu reste trop lent pour être vraiment jouable.
