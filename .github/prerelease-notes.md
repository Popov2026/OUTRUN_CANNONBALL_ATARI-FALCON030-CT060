**English** | [Français](#français)

## v0.31 beta — faster pictures, steadier picture rate

No ROMs and no `.mod` music are included: add your own ROM set (OutRun revision B) in `roms/`, and
optionally your `.mod` files in `Music/` (see the README).

### What's new since v0.30 beta
- **`fps` option**: a steady picture rate between the cadences, e.g. `fps = 25` or `fps = 20` (replaces
  `cadence`; the game slows down if the machine cannot draw that many pictures).
- **Steadier automatic picture rate** (`cadence = 0`): 30, 25, 20, 15, 10 or 7.5 pictures/s, down as soon as
  the current rate no longer fits, up only after about 3 s with 20 % to spare — no more back and forth.
- **Faster picture preparation** (68030 and 68060, identical pictures pixel for pixel): zoomed sprites drawn
  screen pixel by screen pixel, background tiles unpacked at start-up (opaque rows drawn two pixels at a
  time), screen written only where pixels changed. About **25 % less time** per picture in a race (Hatari,
  68060 at 32 MHz: ~40 ms → ~30 ms).

Everything from v0.30 beta is still there (FM music on the DSP, `mod_dsp`, joystick, `PADTEST.TOS`,
`ROMNAME.TOS`) — see below.

### v0.30 beta — FM music on the DSP (what's new since v0.27)
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
- `ROMNAME.TOS`: renames your ROM files to the short 8.3 names (plain TOS) or to the long names (FreeMiNT),
  recognising each ROM by its contents, and sets `freemint` in `outrun.ini` to match.

### `.mod` music: the `mod_dsp` setting
Only with `mod = 1` and your own `TRACK1.MOD` … `TRACK4.MOD` in `Music/` (4-channel ProTracker modules).
A missing file plays the original FM music for that track.

| `mod_dsp` | Who plays the `.mod` | Notes |
|---|---|---|
| `0` | the 68k (the port's own mixer) | works everywhere, costs CPU time |
| `1` | the DSP, with the SoundTracker replay by Simplet / ABSTRACT | 49 kHz stereo, steady tempo; **recommended**, especially on CT60/CT63 |
| `2` | the DSP, with DSPMOD 3.4 by bITmASTER of TCE | Falcon 030 only. With a 68040/68060 (DSPMOD is not 68060-safe: it froze or slowed the game), and for modules with more than 64 patterns (`M!K!`, DSPMOD hangs on them), the game uses the Simplet replay instead, i.e. the same as `1` |

While a `.mod` is played by the DSP, the game's FM sounds go back to the 68k (the DSP does one job at a time).

Tested in Hatari (Falcon 030 and 68060 with DSP emulation) and by users on Hatari 68060 + DSP and a real CT60
(joystick). On a 16 MHz Falcon 030 the game stays too slow to be really playable.

---

## Français

## v0.31 bêta — images plus rapides, cadence plus stable

Aucune ROM ni musique `.mod` n'est incluse : ajoute ton propre jeu de ROMs (OutRun révision B) dans `roms/`,
et éventuellement tes `.mod` dans `Music/` (voir le README).

### Nouveautés depuis la v0.30 bêta
- **Option `fps`** : un nombre d'images par seconde fixe entre les cadences, par ex. `fps = 25` ou `fps = 20`
  (remplace `cadence` ; le jeu ralentit si la machine ne peut pas dessiner autant d'images).
- **Mode automatique plus stable** (`cadence = 0`) : 30, 25, 20, 15, 10 ou 7,5 images/s ; descend dès que la
  cadence ne tient plus, ne remonte qu'après ~3 s avec 20 % de marge — plus de va-et-vient.
- **Préparation de l'image plus rapide** (68030 et 68060, images identiques au pixel près) : sprites zoomés
  dessinés pixel d'écran par pixel d'écran, tuiles du décor décodées au démarrage (lignes pleines dessinées
  2 pixels à la fois), écran réécrit seulement là où les pixels changent. Environ **25 % de temps en moins**
  par image en course (Hatari, 68060 à 32 MHz : ~40 ms → ~30 ms).

Tout ce qu'apportait la v0.30 bêta est toujours là (musique FM sur le DSP, `mod_dsp`, joystick,
`PADTEST.TOS`, `ROMNAME.TOS`) — voir ci-dessous.

### v0.30 bêta — musique FM sur le DSP (nouveautés depuis la v0.27)
- **Musique FM calculée par le DSP56001 du Falcon** (`fm_dsp = 1`, par défaut). La musique FM d'origine ne
  saccade plus : le YM2151 est calculé par un programme DSP, au son identique échantillon par échantillon à
  l'émulation CPU, pour une fraction du temps CPU.
- **Son produit par une interruption** sur 68040/68060 (`sound_irq = 2`, par défaut) : musique régulière même
  quand le nombre d'images par seconde baisse.
- **Musique `.mod` avec DSPMOD 3.4** (bITmASTER de TCE, `mod_dsp = 2`) sur Falcon 030. Avec un 68060, et pour les
  modules de plus de 64 patterns, c'est le replay DSP de Simplet qui est utilisé (DSPMOD n'est pas fiable sur 68060).
- **Joystick corrigé sur vrai CT60**, et **configurable** : `joy_accel`, `joy_brake`, `joy_gear` dans `outrun.ini`.
- `PADTEST.TOS` : test du joystick qui affiche ce que renvoie chaque prise.
- `ROMNAME.TOS` : renomme tes fichiers de ROM en noms courts 8.3 (TOS seul) ou en noms longs (FreeMiNT), en
  reconnaissant chaque ROM par son contenu, et règle `freemint` dans `outrun.ini` en conséquence.

### Musique `.mod` : le réglage `mod_dsp`
Seulement avec `mod = 1` et tes propres `TRACK1.MOD` … `TRACK4.MOD` dans `Music/` (modules ProTracker 4 voies).
Si un fichier manque, ce morceau joue la musique FM d'origine.

| `mod_dsp` | Qui joue le `.mod` | Remarques |
|---|---|---|
| `0` | le 68k (le mixeur du portage) | marche partout, coûte du temps CPU |
| `1` | le DSP, avec le replay SoundTracker de Simplet / ABSTRACT | 49 kHz stéréo, tempo régulier ; **conseillé**, surtout sur CT60/CT63 |
| `2` | le DSP, avec DSPMOD 3.4 de bITmASTER (TCE) | Falcon 030 seulement. Avec un 68040/68060 (DSPMOD n'est pas fiable sur 68060 : il bloquait ou ralentissait le jeu), et pour les modules de plus de 64 patterns (`M!K!`, DSPMOD se bloque dessus), le jeu utilise le replay Simplet, donc comme `1` |

Pendant qu'un `.mod` est joué par le DSP, les sons FM du jeu repassent sur le 68k (le DSP fait une seule chose à la fois).

Testé sous Hatari (Falcon 030 et 68060 avec émulation du DSP) et par des utilisateurs sous Hatari 68060 + DSP et
sur un vrai CT60 (joystick). Sur un Falcon 030 à 16 MHz, le jeu reste trop lent pour être vraiment jouable.
