# OutRun – Cannonball pour Atari Falcon 030 / CT60 (68060) / Mega STE

Portage du moteur **[Cannonball](https://github.com/djyt/cannonball)** de Chris White
(réécriture en C++ du code 68000/Z80 de la borne d'arcade **OutRun** de SEGA) vers les
ordinateurs **Atari** :

| Cible | Exécutable | Processeur | Vidéo | État |
|---|---|---|---|---|
| Falcon 030 | `CB030.TOS` | 68030 @ 16 MHz | 16 bits true color (RGB565) | Fonctionne, lent (~35-55 % de la vitesse réelle) |
| Falcon + CT60 | `CB060.TOS` | 68060 (+ Fast RAM) | 16 bits true color (RGB565) | **Plein régime** (100 % vitesse réelle mesurée sous Hatari) |
| Mega STE | `OUTRUN.TOS` | 68000 | 16 couleurs, bitplanes | Compile et tourne, mais injouable (< 1 image/s) |

**Version actuelle : v0.27**. Le journal complet des changements est dans [`VERSION.txt`](VERSION.txt).

> ⚠️ **Les ROMs d'OutRun ne sont pas fournies** (propriété de SEGA). Tu dois utiliser ton
> propre dump du jeu, **révision B** : voir [Installer les ROMs](#2-installer-les-roms).

---

## Sommaire

1. [Contenu du dépôt](#contenu-du-dépôt)
2. [Démarrage rapide](#démarrage-rapide)
3. [Commandes de jeu](#commandes-de-jeu)
4. [Fichier d'options `outrun.ini`](#fichier-doptions-outrunini)
5. [Musique `.mod` (option)](#musique-mod-option)
6. [Lancer sous l'émulateur Hatari](#lancer-sous-lémulateur-hatari)
7. [Compiler depuis les sources](#compiler-depuis-les-sources)
8. [Fonctionnement du portage](#fonctionnement-du-portage)
9. [Performances mesurées](#performances-mesurées)
10. [Limites connues et points non testés](#limites-connues-et-points-non-testés)
11. [Documentation détaillée](#documentation-détaillée)
12. [Crédits et licence](#crédits-et-licence)

---

## Contenu du dépôt

```
.
├── dist/                    ← binaires prêts à l'emploi (v0.27)
│   ├── CB030.TOS            Falcon 68030
│   ├── CB060.TOS            Falcon + CT60 / 68060 (à essayer en premier)
│   ├── outrun.ini           options pré-réglées (musique .mod jouée par le DSP)
│   ├── VERSION.txt
│   ├── roms/                ← à remplir avec TES ROMs (voir roms.txt / LISEZMOI.txt)
│   └── Music/               ← y déposer TES fichiers TRACK1..4.MOD (voir README.txt)
│
├── src/main/                ← sources C++ du moteur Cannonball
│   ├── engine/              logique du jeu OutRun (code d'origine + points d'accroche Atari)
│   ├── hwvideo/ hwaudio/    émulation des puces vidéo et son (YM2151, SegaPCM)
│   ├── frontend/            configuration (branche PLATFORM_ATARI sans Boost/XML)
│   ├── atari/               ★ couche plateforme Atari (vidéo, son DMA, DSP, clavier, joysticks, assembleur 68k)
│   ├── main_atari.cpp       ★ point d'entrée Atari (remplace main.cpp / SDL2)
│   └── sdl2/ directx/       backends d'origine (non utilisés sur Atari, sauf le stub ffeedback)
│
├── res/                     tilemap.bin, tilepatch.bin, config.xml… (nécessaires à l'exécution)
├── cmake/                   build CMake d'origine (Windows / Linux / Pi4, non Atari)
├── docs/license.txt         licence Cannonball
│
├── build_atari.sh           ★ script de build principal (cross-compilateur m68k-atari-mint)
├── build_release.sh         construit CB030.TOS + CB060.TOS
├── build_*test.sh           builds de diagnostic (clavier, perf, musique, sortie…)
├── Makefile.atari           liste des sources compilées pour Atari
├── outrun.ini.example       toutes les options, commentées
│
├── README_ATARI.md          doc technique du portage (en anglais)
├── ATARI_PORT_FILES.md      rôle de chaque fichier du portage (en français)
├── DSP_NOTES.md             journal du travail sur le DSP56001
└── README_CANNONBALL.md     README d'origine du projet Cannonball
```

---

## Démarrage rapide

### 1. Copier les fichiers

Copie sur le disque de l'Atari, dans un même dossier :

- le contenu de `dist/` : `CB030.TOS` et/ou `CB060.TOS`, `outrun.ini`, `roms/`, `Music/` ;
- le dossier `res/` qui se trouve à la racine de ce dépôt.

```
C:\OUTRUN\
    CB060.TOS
    CB030.TOS
    outrun.ini
    res\
    roms\
    Music\
```

### 2. Installer les ROMs

Place dans `roms\` les **31 fichiers ROM d'OutRun révision B**. La liste figure dans
[`dist/roms/roms.txt`](dist/roms/roms.txt) et dans `src/main/roms.cpp` :

```
epr-10187.88    epr-10327a.76   epr-10328a.75   epr-10329a.58   epr-10330a.57
epr-10380b.133  epr-10381b.132  epr-10382b.118  epr-10383b.117
mpr-10371.9     mpr-10372.13    mpr-10373.10    mpr-10374.14
mpr-10375.11    mpr-10376.15    mpr-10377.12    mpr-10378.16
opr-10185.11    opr-10186.47    opr-10188.71    opr-10189.70    opr-10190.69
opr-10191.68    opr-10192.67    opr-10193.66    opr-10230.104   opr-10231.103
opr-10232.102   opr-10266.101   opr-10267.100   opr-10268.99
```

**Noms longs et FreeMiNT.** Les noms d'origine (`epr-10380b.133`…) dépassent la limite
8.3 du GEMDOS. Tu as deux possibilités :

- **`freemint = 1`** (par défaut) : garde les noms d'origine. Sur une vraie machine, il faut
  **FreeMiNT** (ou un autre système de fichiers qui gère les noms longs).
- **`freemint = 0`** : fonctionne sous TOS seul, mais il faut renommer les ROMs en 8.3 :
  `epr-` devient `E`, `mpr-` devient `M`, `opr-` devient `O`. Par exemple, `epr-10380b.133`
  devient `E10380b.133` et `mpr-10371.9` devient `M10371.9`. La table complète est dans
  [`README_ATARI.md`](README_ATARI.md).

### 3. Lancer

Double-clique sur **`CB060.TOS`** (Falcon avec CT60) ou sur **`CB030.TOS`** (Falcon de série).
Le chargement des ROMs prend quelques secondes, puis le jeu démarre directement en mode
attract, sans menu. Appuie sur **Entrée** pour mettre une pièce et lancer la partie.

---

## Commandes de jeu

### Clavier

| Touche | Action |
|---|---|
| ← / → | Diriger |
| Espace | Accélérer |
| Ctrl gauche | Freiner |
| Alt gauche / Shift gauche | Changer de vitesse (LOW / HIGH) |
| Entrée | Pièce / Start |
| V | Changer de vue |
| P | Pause (affiche « PAUSE », Falcon uniquement) |
| F9 | Capture d'écran (`SHOTnnnn.PNG` dans le dossier du jeu) |
| Échap / F10 | Quitter (confirmer avec ↑, annuler avec ↓) |

### Joystick standard (prise DB9, port joystick, pas le port souris)

Gauche / droite pour diriger, avant pour accélérer, arrière pour freiner, bouton pour changer
de vitesse. Le clavier et le joystick marchent en même temps.

### Ports joystick étendus (les 2 prises 15 broches du STE / Falcon), nouveau en v0.27

Ces ports acceptent un pad Jaguar, ou un joystick classique branché sur un adaptateur :

| Pad | Action |
|---|---|
| Gauche / droite | Diriger |
| Haut ou **B** | Accélérer |
| Bas ou **C** | Freiner |
| **A** (le bouton d'un joystick simple) | Changer de vitesse |
| **Option** | Pièce / Start |
| **Pause** | Pause |

> Sous Hatari, la détection des ports a été vérifiée (ils lisent bien « rien d'appuyé » quand
> rien n'est branché). **Ce n'est pas encore testé avec un vrai pad ou joystick branché.**

---

## Fichier d'options `outrun.ini`

Le jeu le lit au démarrage, à côté de l'exécutable. Toutes les lignes sont facultatives.
Chaque option est commentée dans [`outrun.ini.example`](outrun.ini.example).

| Option | Valeurs | Défaut | Effet |
|---|---|---|---|
| `shadows` | 0 / 1 | 1 | Ombres sous les voitures et le décor (0 donne environ +4 % d'images/s) |
| `shadow_min_z` | 0..0x1ff | 0 | Avec les ombres, n'en dessine que pour les objets proches |
| `scenery` | 0 / 1 | 1 | 0 = ni arbres, ni panneaux, ni bâtiments. **De loin le meilleur gain**, conseillé sur 68030 |
| `vscale` | 50..100 | 100 | % des 224 lignes calculées (67 donne +17 %, 50 donne +26 %) |
| `road_hres` | 0 / 1 | 0 | Route calculée en demi-résolution horizontale (+5 %) |
| `cadence` | 0..4 | 0 | Pas de jeu entre deux images : 0 = auto, 1 = 30 i/s, 2 = 15, 3 = 10, 4 = 7,5 |
| `sound` | 0 / 1 / 2 | 2 | 0 = muet, 1 = son toujours calculé, 2 = son calculé seulement s'il reste du temps CPU |
| `music` | 0 / 1 | 1 | Coupe la musique sans toucher aux bruitages |
| `fm_half` | 0 / 1 | 0 | Puce FM émulée à mi-fréquence : moins coûteux, mais son plus terne |
| `mod` | 0 / 1 | 0 | Remplace la musique FM par des fichiers `.mod` (voir plus bas) |
| `mod_dsp` | 0 / 1 | 0 | Fait jouer les `.mod` par le **DSP56001** du Falcon au lieu du CPU |
| `freemint` | 0 / 1 | 1 | Noms de ROM longs (1) ou renommés en 8.3 (0) |

Le `dist/outrun.ini` fourni règle `sound=1`, `mod=1` et `mod_dsp=1`.
**Conseils pour le Falcon 030 de série :** `scenery=0`, `vscale=67`, `mod=1`.

---

## Musique `.mod` (option)

Avec `mod=1`, les musiques FM d'origine sont remplacées par des modules **ProTracker
4 voies** que tu fournis toi-même, dans `Music\` :

| Fichier | Morceau |
|---|---|
| `TRACK1.MOD` | Magical Sound Shower |
| `TRACK2.MOD` | Passing Breeze |
| `TRACK3.MOD` | Splash Wave |
| `TRACK4.MOD` | Last Wave (écran des meilleurs scores) |

Si un fichier manque, le morceau correspondant repasse en musique FM d'origine. Les `.mod`
coûtent beaucoup moins de CPU que l'émulation du YM2151.

- **`mod_dsp=0`** : le lecteur 4 voies écrit pour ce portage (`atari/modplayer.cpp`) mixe la
  musique sur le CPU.
- **`mod_dsp=1`** : la musique est jouée par le **DSP56001** avec le replay SoundTracker de
  **Simplet / ABSTRACT** (archive `dsptrack` de dhs.nu, utilisé sans modification), en
  49 170 Hz stéréo, au bon tempo quelle que soit la cadence d'affichage. Le mixage FM + PCM du
  jeu passe alors par le DSP sur deux voies supplémentaires. Si le DSP ne répond pas, le jeu
  revient tout seul en mode CPU.

> Les fichiers `.mod` ne sont pas inclus dans ce dépôt (reprises de morceaux protégés).

---

## Lancer sous l'émulateur Hatari

Mets `CB030.TOS` (ou `CB060.TOS`), `roms/`, `res/` et `outrun.ini` dans un même dossier,
puis monte ce dossier comme disque GEMDOS :

```bash
# Falcon 030
hatari --machine falcon --memsize 14 --dsp none --tos tos.img \
       --harddrive <dossier>

# Falcon + 68060 avec Fast RAM (configuration la plus rapide)
hatari --machine falcon --memsize 14 --ttram 32 --cpulevel 6 --cpuclock 32 \
       --addr24 false --tos tos.img --harddrive <dossier>

# Pour la musique .mod jouée par le DSP : remplacer --dsp none par --dsp emu

# Mega STE
hatari --machine megaste --memsize 10 --tos tos.img --harddrive <dossier>
```

Ces commandes ont été testées avec `tos.img` = EmuTOS 1.4, fourni avec Hatari.

**Attention au piège de `--auto`.** `--auto FICHIER` ne lance pas le programme : Hatari
*tape* le nom du fichier sur le bureau, après le boot. Pour démarrer le jeu automatiquement,
mets-le dans le dossier `AUTO\` en le renommant en `.PRG`. Si tu utilises quand même
`--auto`, donne-lui un nom de fichier qui n'existe pas (`--auto NOFILE.TOS`). Sinon, le jeu
se relance tout seul dès que tu le quittes. Les détails sont dans `README_ATARI.md`.

Pour les joysticks, associe une manette du PC au **port joystick 1** de l'ST, et aux ports
**2 / 3** (« STE joypad A/B ») pour les ports étendus.

---

## Compiler depuis les sources

### Prérequis

- Le cross-compilateur **`m68k-atari-mint` GCC 4.6.4** de Vincent Rivière.
  Développement fait sous Cygwin, avec le compilateur installé dans `/opt/cross-mint`.
- `bash`, `grep` et `sed`. `make` n'est pas nécessaire : `build_atari.sh` s'en passe.

### Commandes

```bash
# Mega STE (68000, 16 couleurs) -> OUTRUN.TOS
bash ./build_atari.sh

# Falcon 030 -> CB030.TOS
CPU=68030 EXTRA="-msoft-float -DPLATFORM_FALCON" LINKCPU=68000 OUT=CB030.TOS bash ./build_atari.sh

# Falcon + 68060 -> CB060.TOS
CPU=68060 EXTRA="-msoft-float -DPLATFORM_FALCON" LINKCPU=68000 OUT=CB060.TOS bash ./build_atari.sh

# Les deux binaires de release d'un coup
# (adapter d'abord le chemin cd /cygdrive/c/claude/cannonball dans le script)
bash ./build_release.sh
```

Le script affiche `BUILD_OK` quand l'édition de liens réussit.

Points importants :

- **`LINKCPU=68000` est obligatoire**, même pour un build 030 ou 060. Les bibliothèques
  68020-60 du toolchain supposent un FPU, que le Falcon n'a pas. Avec la mauvaise
  bibliothèque, le binaire plante juste après `Pexec`, avant `main()`.
- **`OUT` doit rester au format 8.3** : 8 caractères au maximum, plus 3 pour l'extension.
- On peut aussi utiliser `make -f Makefile.atari`, qui compile les mêmes sources pour le
  Mega STE.

### Options de compilation utiles (à passer dans `EXTRA`)

| Option | Effet |
|---|---|
| `-DLOWRES` | Rendu en 160×112 doublé à l'écran (Falcon) |
| `-DNO_SOUND` | Aucun son (comparaisons de vitesse) |
| `-DSOUND_RATE=25033` | Fréquence de mixage DMA différente des 12 517 Hz par défaut |
| `-DMOVE16` | 68060 : copie de chaque ligne vers l'écran en rafales MOVE16 |
| `-DBENCH_N=400` | Mesure de la cadence sur 400 images, avec une ligne de résultat |
| `-DPERF_PRINT` | Temps par étape, écrits dans `PERFLOG.TXT` |
| `-DAUTOPLAY` | Pièce, start et accélérateur automatiques (mesures) |
| `-DKEYTRACE` / `-DPADTRACE_FILE` | Trace du clavier / des ports joystick étendus |

La liste complète (une trentaine d'aides au diagnostic : `ROWCHECK`, `ROADCHECK`,
`PCMCHECK`, `MUSIC_RENDER`…) est dans [`README_ATARI.md`](README_ATARI.md#building).

---

## Fonctionnement du portage

Le moteur, l'émulation des puces (`hwvideo/`, `hwaudio/`) et la logique de jeu sont le code
C++ d'origine de Cannonball. Le portage apporte trois choses :

- une **couche plateforme Atari** (`src/main/atari/`, `main_atari.cpp`) ;
- les **adaptations au cross-compilateur MiNT** ;
- des **routines assembleur 68k** pour les boucles les plus coûteuses.

En résumé :

- **Vidéo.** Le moteur compose une image 320×224 en indices de palette.
  - Sur **Falcon**, chaque pixel passe par une table vers du RGB565 16 bits (`truecolor_asm.S`).
    Les lignes qui n'ont pas changé ne sont pas reconverties (cache de lignes). L'écran est en
    triple buffer, et le mode vidéo est choisi selon la taille disponible : VGA 320×240 ou
    RVB/TV 384×240 en overscan, sinon 320×400 entrelacé.
  - Sur **Mega STE**, une palette de 16 couleurs est choisie à chaque image par histogramme,
    puis l'image est convertie en bitplanes.
- **Cadence.** La logique de jeu tourne toujours à 30 pas par seconde en temps réel. Quand
  l'affichage est plus lent, plusieurs pas sont calculés par image, de sorte que le jeu garde
  sa vitesse et que seule la fluidité baisse.
- **Son.** Le YM2151 (FM) et le SegaPCM sont émulés, puis mixés et envoyés au DMA son du
  STE / Falcon en 12 517 Hz, avec quatre buffers. En option, les `.mod` sont joués par le CPU
  ou par le DSP56001 (voir plus haut).
- **Entrées.** Une interruption clavier en assembleur (`kbd_asm.S`) remplace le vecteur ACIA
  pendant la partie et le restaure en sortie. Elle lit le joystick IKBD et les ports joystick
  étendus.
- **Assembleur 68k** (`src/main/atari/*.S`) : lignes de sprites (versions 68000 et 030/060),
  segments de route, tuiles 8×8, canaux SegaPCM, conversion chunky vers planar et vers 16 bits,
  replay DSP.
- **Mémoire.** L'opérateur `new` remet la mémoire à zéro, car TOS ne le fait pas alors que le
  moteur en a besoin. L'écran et les buffers son sont en ST-RAM, tout le reste peut aller en
  Fast RAM.

Le rôle de chaque fichier est détaillé dans [`ATARI_PORT_FILES.md`](ATARI_PORT_FILES.md).

---

## Performances mesurées

Mesures faites sous Hatari, en temps émulé, pendant que le jeu roule en démo et en pleine
résolution :

| Machine | Temps par image | Vitesse du jeu |
|---|---|---|
| Falcon 030 @ 16 MHz | ~1,3 s | ~35-55 % du temps réel |
| 68060 @ 32 MHz + Fast RAM | ~0,065 s (15 images/s) | **100 %** du temps réel |
| Mega STE | plusieurs secondes | injouable |

Effet de `vscale` sur le 68060 émulé : 100 donne 18,3 i/s, 67 donne 21,5 i/s et 50 donne
23,1 i/s.

---

## Limites connues et points non testés

- **Rien n'a encore été vérifié sur une vraie machine.** Tous les tests ont été faits sous
  Hatari, et l'émulation du 68060 y est marquée « expérimentale ».
- Les ports joystick étendus (v0.27) n'ont pas été testés avec un pad réellement branché.
- Les menus frontend de Cannonball (réglages, Time Trial…) ne sont pas inclus : le jeu démarre
  directement.
- La pause n'est disponible que sur Falcon.
- Il n'y a pas de volant ni de pédales analogiques.
- Sur vraie machine sans FreeMiNT, il faut `freemint=0` et des ROMs renommées en 8.3.
- Clavier : le gestionnaire désactive les paquets souris et joystick de l'IKBD au démarrage.
  Si une touche semble rester « enfoncée », c'est le premier endroit à regarder
  (`atari/input.cpp`).

Retours bienvenus, surtout sur vrai matériel (Falcon 030, CT60/CT63, pads Jaguar).

---

## Documentation détaillée

| Fichier | Contenu |
|---|---|
| [`README_ATARI.md`](README_ATARI.md) | Doc technique complète du portage (EN) : build, options de compilation, Hatari, pièges, mesures |
| [`ATARI_PORT_FILES.md`](ATARI_PORT_FILES.md) | Rôle de chaque fichier ajouté ou modifié, points d'accroche dans le moteur, bugs corrigés (FR) |
| [`DSP_NOTES.md`](DSP_NOTES.md) | Journal du travail sur le DSP56001 |
| [`VERSION.txt`](VERSION.txt) | Changements de la v0.27 |
| [`outrun.ini.example`](outrun.ini.example) | Toutes les options d'exécution |
| [`README_CANNONBALL.md`](README_CANNONBALL.md) | README d'origine de Cannonball (build Windows / Linux / Pi) |

---

## Crédits et licence

- **Cannonball** © Chris White et l'équipe Cannonball : moteur, reverse engineering d'OutRun
  ([github.com/djyt/cannonball](https://github.com/djyt/cannonball),
  [blog Reassembler](http://reassembler.blogspot.com/)). Ce portage n'existerait pas sans
  leur travail.
- **Replay DSP SoundTracker** : Simplet / ABSTRACT (archive `dsptrack`, dhs.nu).
- **Portage Atari** Falcon 030 / CT60 / Mega STE : Popov2026.

Ce dépôt est distribué sous la **licence Cannonball** ([`docs/license.txt`](docs/license.txt)) :

- redistribution **non commerciale** uniquement ;
- toute version modifiée doit être accompagnée de **ses sources complètes**, ce que fait ce
  dépôt ;
- la notice de copyright doit être conservée.

*OutRun est une marque de SEGA Corporation. Ce projet n'est pas affilié à SEGA. Aucune ROM ni
musique protégée n'est incluse.*
