# OutRun – Cannonball pour Atari Falcon 030 et Falcon CT60 / CT63 (68060)

[English](README.md) | **Français**

Portage du moteur **[Cannonball](https://github.com/djyt/cannonball)** de Chris White
(réécriture en C++ du code 68000/Z80 de la borne d'arcade **OutRun** de SEGA) sur
**Atari Falcon** :

| Machine | Exécutable | Processeur | État |
|---|---|---|---|
| Falcon 030 d'origine | `CB030.TOS` | 68030 @ 16 MHz | Fonctionne, mais trop lent pour être vraiment jouable (~35-55 % de la vitesse réelle) |
| Falcon + CT60 / CT63 | `CB060.TOS` | 68060 (+ Fast RAM) | **Pleine vitesse** (100 % du temps réel, mesuré sous Hatari) |

> **Sur le Falcon d'origine à 16 MHz**, `CB030.TOS` fait tourner le jeu, mais il n'est pas
> vraiment jouable (environ une image toutes les 1,3 s avec les réglages par défaut). Les
> réglages allégés indiqués dans la section [`outrun.ini`](#fichier-doptions-outrunini)
> aident. Pour la pleine vitesse, utilise `CB060.TOS` sur un Falcon équipé d'une CT60 / CT63.

**Version actuelle : v0.27**. Le journal complet des changements est dans
[`VERSION.txt`](VERSION.txt) (en anglais).

> ⚠️ **Les ROMs d'OutRun ne sont pas fournies** (elles appartiennent à SEGA). Il te faut ton
> propre dump du jeu, **révision B** : voir [Installer les ROMs](#2-installer-les-roms).

### Téléchargement

Le paquet prêt à l'emploi se trouve sur la page **[Releases](../../releases)** :
`cannonball_falcon_v0.30-beta.zip` contient `CB030.TOS`, `CB060.TOS`, `PADTEST.TOS`, `outrun.ini`, le dossier
`res/`, ainsi que des dossiers `roms/` et `Music/` vides accompagnés de leurs instructions.
Décompresse-le, ajoute tes ROMs (et éventuellement tes `.mod`), puis lance le jeu. Aucune ROM
ni aucune musique `.mod` n'est incluse.

---

## Sommaire

1. [Contenu du dépôt](#contenu-du-dépôt)
2. [Démarrage rapide](#démarrage-rapide)
3. [Commandes](#commandes)
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
│   ├── CB030.TOS            Falcon 030 d'origine (68030 @ 16 MHz, lent)
│   ├── CB060.TOS            Falcon + CT60 / CT63 (68060, pleine vitesse)
│   ├── outrun.ini           options pré-réglées (musique .mod jouée par le DSP)
│   ├── VERSION.txt
│   ├── roms/                ← à remplir avec TES ROMs (voir roms.txt / README.txt)
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
├── cmake/                   build CMake d'origine (Windows / Linux / Pi4, pas Atari)
├── docs/license.txt         licence Cannonball
│
├── build_atari.sh           ★ script de build principal (cross-compilateur m68k-atari-mint)
├── build_release.sh         construit CB030.TOS + CB060.TOS
├── build_*test.sh           builds de diagnostic (clavier, perf, musique, sortie…)
├── Makefile.atari           liste des sources compilées pour l'Atari
├── outrun.ini.example       toutes les options, commentées
│
├── README.md                version anglaise de ce README
├── README_ATARI.md          documentation technique du portage (en anglais)
├── ATARI_PORT_FILES.md      rôle de chaque fichier du portage (en anglais)
├── DSP_NOTES.md             journal du travail sur le DSP56001 (en anglais)
└── README_CANNONBALL.md     README d'origine du projet Cannonball
```

---

## Démarrage rapide

### 1. Copier les fichiers

Le plus simple est de décompresser le paquet de la page [Releases](../../releases) : il a déjà
l'organisation ci-dessous. Sinon, depuis ce dépôt, copie sur le disque de l'Atari, dans un
même dossier :

- le contenu de `dist/` : `CB030.TOS` et/ou `CB060.TOS`, `outrun.ini`, `roms/`, `Music/` ;
- le dossier `res/` qui se trouve à la racine de ce dépôt.

```
C:\OUTRUN\
    CB030.TOS
    CB060.TOS
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

**Noms longs et FreeMiNT.** Les noms d'origine (`epr-10380b.133`…) dépassent la limite 8.3
du GEMDOS. Tu as deux possibilités :

- **`freemint = 1`** (par défaut) : garde les noms d'origine. Sur une vraie machine, il faut
  **FreeMiNT** (ou un autre système de fichiers qui gère les noms longs).
- **`freemint = 0`** : fonctionne sous TOS seul, mais il faut renommer les ROMs en 8.3 :
  `epr-` devient `E`, `mpr-` devient `M`, `opr-` devient `O`. Par exemple, `epr-10380b.133`
  devient `E10380b.133` et `mpr-10371.9` devient `M10371.9`. La table complète est dans
  [`README_ATARI.md`](README_ATARI.md).

**`ROMNAME.TOS`** fait le renommage pour toi : mets-le à côté de `CB030.TOS` / `CB060.TOS`, lance-le
et appuie sur **S** pour les noms courts 8.3 (TOS seul) ou sur **L** pour les noms longs (FreeMiNT
uniquement). Il reconnaît chaque ROM par son contenu (CRC32), quel que soit son nom actuel - même
l'alias tronqué que TOS montre pour un nom long copié depuis un PC - et règle `freemint` dans
`outrun.ini` en conséquence.

### 3. Lancer

Double-clique sur **`CB030.TOS`** (Falcon 030 d'origine) ou **`CB060.TOS`** (Falcon avec
CT60 / CT63). Le chargement des ROMs prend quelques secondes, puis le jeu démarre directement
en mode démo, sans menu. Appuie sur **Entrée** pour mettre une pièce et lancer une partie.

---

## Commandes

### Clavier

| Touche | Action |
|---|---|
| ← / → | Diriger |
| Espace | Accélérer |
| Ctrl gauche | Freiner |
| Alt gauche / Shift gauche | Changer de vitesse (LOW / HIGH) |
| Entrée | Pièce / Start |
| V | Changer de vue |
| P | Pause (affiche « PAUSE ») |
| F9 | Capture d'écran (`SHOTnnnn.PNG` dans le dossier du jeu) |
| Échap / F10 | Quitter (confirmer avec ↑, annuler avec ↓) |

### Joystick standard (prise DB9, port joystick, pas le port souris)

Gauche / droite pour diriger, avant pour accélérer, arrière pour freiner, bouton pour changer
de vitesse. Le clavier et le joystick fonctionnent en même temps.

### Ports joystick étendus (les deux prises 15 broches du Falcon), nouveau en v0.27

Ces ports acceptent un pad Jaguar, ou un joystick classique branché sur un adaptateur :

| Pad | Action |
|---|---|
| Gauche / droite | Diriger |
| Haut ou **B** | Accélérer |
| Bas ou **C** | Freiner |
| **A** (le bouton d'un joystick simple) | Changer de vitesse |
| **Option** | Pièce / Start |
| **Pause** | Pause |

> Sous Hatari, la détection des ports a été vérifiée (ils lisent « rien d'appuyé » quand rien
> n'est branché). **Ce n'est pas encore testé avec un vrai pad ou joystick branché.**

---

## Fichier d'options `outrun.ini`

Le jeu le lit au démarrage, à côté de l'exécutable. Toutes les lignes sont facultatives.
Chaque option est décrite dans [`outrun.ini.example`](outrun.ini.example) (en anglais).

| Option | Valeurs | Défaut | Effet |
|---|---|---|---|
| `shadows` | 0 / 1 | 1 | Ombres sous les voitures et le décor (0 donne environ +4 % d'images/s) |
| `shadow_min_z` | 0..0x1ff | 0 | Avec les ombres, n'en dessine que pour les objets proches |
| `scenery` | 0 / 1 | 1 | 0 = ni arbres, ni panneaux, ni bâtiments. **De loin le plus gros gain** |
| `vscale` | 50..100 | 100 | % des 224 lignes calculées (67 donne +17 %, 50 donne +26 %) |
| `road_hres` | 0 / 1 | 0 | Route calculée en demi-résolution horizontale (+5 %) |
| `cadence` | 0..4 | 0 | Pas de jeu entre deux images : 0 = auto, 1 = 30 i/s, 2 = 15, 3 = 10, 4 = 7,5 |
| `sound` | 0 / 1 / 2 | 2 | 0 = muet, 1 = son toujours calculé, 2 = son calculé seulement s'il reste du temps CPU |
| `music` | 0 / 1 | 1 | Coupe la musique sans toucher aux bruitages |
| `fm_half` | 0 / 1 | 0 | Puce FM émulée à mi-fréquence : moins coûteux, mais son plus terne (avec `fm_dsp=0` seulement) |
| `sound_irq` | 0 / 1 / 2 | 2 | 1 = son produit par une interruption : la musique garde son tempo et n'a pas de trous même quand le jeu tourne plus lentement que le temps réel ; 2 = automatique (activé avec un 68040/68060, désactivé avec un 68030, qui n'a pas le temps pour ça) |
| `fm_dsp` | 0 / 1 | 1 | La puce FM (YM2151) est calculée par le **DSP56001** du Falcon : même son, au bit près, pour une fraction du temps CPU (voir plus bas) |
| `mod` | 0 / 1 | 0 | Remplace la musique FM par des fichiers `.mod` (voir plus bas) |
| `mod_dsp` | 0 / 1 / 2 | 0 | Fait jouer les `.mod` par le **DSP56001** du Falcon au lieu du CPU : 1 = replay SoundTracker (Simplet / ABSTRACT), 2 = DSPMOD 3.4 (bITmASTER of TCE ; les modules de plus de 64 patterns, et tous les modules sur 68040/68060, passent au replay Simplet : DSPMOD se bloque sur les premiers et n'est pas fiable sur 68060) |
| `freemint` | 0 / 1 | 1 | Noms de ROM longs (1) ou renommés en 8.3 (0) |
| `joy_accel` | `up`, `down`, `fire`, `b`, `c`, `none` (à combiner avec `+`) | `up+b` | Ce qui accélère au joystick / pad |
| `joy_brake` | idem | `down+c` | Ce qui freine |
| `joy_gear` | idem | `fire` | Ce qui change de vitesse |

Mots pour le joystick : `up` / `down` = le manche (avant / arrière), `fire` = le bouton d'un joystick
DB9 ou le bouton A d'un pad Jaguar, `b` / `c` = les boutons B et C d'un pad Jaguar. Exemple, accélérer
avec le bouton : `joy_accel = fire`, `joy_brake = down`, `joy_gear = up`. Gauche/droite dirigent
toujours, et le clavier marche toujours en même temps.

Le `dist/outrun.ini` fourni règle `sound=1`, `fm_dsp=1`, `mod=1` et `mod_dsp=1`.
**Réglages allégés pour le Falcon 030 d'origine :** `scenery=0`, `vscale=67` (ou 50), `mod=1`,
`mod_dsp=1`.

---

## Musique `.mod` (option)

Avec `mod=1`, les musiques FM d'origine sont remplacées par des modules **ProTracker 4 voies**
que tu fournis toi-même, dans `Music\` :

| Fichier | Morceau |
|---|---|
| `TRACK1.MOD` | Magical Sound Shower |
| `TRACK2.MOD` | Passing Breeze |
| `TRACK3.MOD` | Splash Wave |
| `TRACK4.MOD` | Last Wave (écran des meilleurs scores) |

Si un fichier manque, le morceau correspondant repasse en musique FM d'origine. Les `.mod`
coûtent beaucoup moins de temps CPU que l'émulation du YM2151.

- **`mod_dsp=0`** : le lecteur 4 voies écrit pour ce portage (`atari/modplayer.cpp`) mixe la
  musique sur le CPU.
- **`mod_dsp=1`** : la musique est jouée par le **DSP56001**, avec le replay SoundTracker de
  **Simplet / ABSTRACT** (archive `dsptrack` de dhs.nu, utilisé sans modification), en
  49 170 Hz stéréo, au bon tempo quelle que soit la cadence d'affichage. Le mixage FM + PCM du
  jeu passe alors par le DSP sur deux voies supplémentaires. Si le DSP ne répond pas, le jeu
  repasse tout seul en mode CPU.

> Les fichiers `.mod` ne sont pas inclus dans ce dépôt (reprises de morceaux protégés).

---

## Lancer sous l'émulateur Hatari

Mets `CB030.TOS` (ou `CB060.TOS`), `roms/`, `res/` et `outrun.ini` dans un même dossier, puis
monte ce dossier comme disque GEMDOS :

```bash
# Falcon 030 d'origine
hatari --machine falcon --memsize 14 --dsp emu --tos tos.img \
       --harddrive <dossier>

# Falcon + 68060 avec Fast RAM (configuration type CT60)
hatari --machine falcon --memsize 14 --ttram 32 --cpulevel 6 --cpuclock 32 \
       --addr24 false --dsp emu --tos tos.img --harddrive <dossier>
```

`--dsp emu` est nécessaire pour la FM sur le DSP (`fm_dsp=1`) et pour la musique `.mod` jouée
par le DSP. Avec `--dsp none`, le jeu repasse de lui-même sur le CPU.

Ces commandes ont été testées avec `tos.img` = EmuTOS 1.4, fourni avec Hatari.

**Attention au piège de `--auto`.** `--auto FICHIER` ne lance pas le programme : Hatari
*tape* le nom du fichier sur le bureau, après le démarrage. Pour lancer le jeu
automatiquement, mets-le dans le dossier `AUTO\` en le renommant en `.PRG`. Si tu utilises
quand même `--auto`, donne-lui un nom de fichier qui n'existe pas (`--auto NOFILE.TOS`).
Sinon, le jeu se relance tout seul dès que tu le quittes. Les détails sont dans
`README_ATARI.md`.

Pour les joysticks, associe une manette du PC au **port joystick 1**, et aux ports joystick
**2 / 3** de Hatari pour les ports étendus.

---

## Compiler depuis les sources

### Prérequis

- Le cross-compilateur **`m68k-atari-mint` GCC 4.6.4** de Vincent Rivière.
  Le développement a été fait sous Cygwin, avec le compilateur installé dans `/opt/cross-mint`.
- `bash`, `grep` et `sed`. `make` n'est pas nécessaire : `build_atari.sh` s'en passe.

### Commandes

```bash
# Falcon 030 d'origine -> CB030.TOS
CPU=68030 EXTRA="-msoft-float -DPLATFORM_FALCON" LINKCPU=68000 OUT=CB030.TOS bash ./build_atari.sh

# Falcon + 68060 -> CB060.TOS
CPU=68060 EXTRA="-msoft-float -DPLATFORM_FALCON" LINKCPU=68000 OUT=CB060.TOS bash ./build_atari.sh

# Les deux binaires de release d'un coup
# (adapter d'abord le chemin "cd /cygdrive/c/claude/cannonball" dans le script)
bash ./build_release.sh
```

Le script affiche `BUILD_OK` quand l'édition de liens réussit.

Points importants :

- **`LINKCPU=68000` est obligatoire**, même pour un build 030 ou 060. Les bibliothèques
  68020-60 du toolchain supposent un FPU, que le Falcon n'a pas. Avec la mauvaise
  bibliothèque, le binaire plante juste après `Pexec`, avant `main()`.
- **`OUT` doit rester au format 8.3** : 8 caractères au maximum, plus 3 pour l'extension.

### Options de compilation utiles (à passer dans `EXTRA`)

| Option | Effet |
|---|---|
| `-DLOWRES` | Rendu en 160×112, doublé à l'écran |
| `-DNO_SOUND` | Aucun son (comparaisons de vitesse) |
| `-DSOUND_RATE=25033` | Fréquence de mixage DMA autre que les 12 517 Hz par défaut |
| `-DMOVE16` | 68060 : chaque ligne est copiée vers l'écran en rafales MOVE16 |
| `-DBENCH_N=400` | Chronomètre 400 images et affiche une ligne de résultat |
| `-DPERF_PRINT` | Temps par étape, écrits dans `PERFLOG.TXT` |
| `-DAUTOPLAY` | Pièce, start et accélérateur automatiques (mesures) |
| `-DKEYTRACE` / `-DPADTRACE_FILE` | Trace du clavier / des ports joystick étendus |

La liste complète (une trentaine d'aides au diagnostic : `ROWCHECK`, `ROADCHECK`,
`PCMCHECK`, `MUSIC_RENDER`…) est dans [`README_ATARI.md`](README_ATARI.md#building).

---

## Fonctionnement du portage

Le moteur, l'émulation des puces (`hwvideo/`, `hwaudio/`) et la logique de jeu sont le code
C++ d'origine de Cannonball. Le portage ajoute trois choses :

- une **couche plateforme Atari** (`src/main/atari/`, `main_atari.cpp`) ;
- les **adaptations nécessaires au cross-compilateur MiNT** ;
- des **routines assembleur 68k** pour les boucles les plus coûteuses.

En résumé :

- **Vidéo.** Le moteur compose une image 320×224 en indices de palette. Chaque pixel passe
  par une table vers du RGB565 16 bits (`truecolor_asm.S`). Les lignes qui n'ont pas changé ne
  sont pas reconverties (cache de lignes). L'écran est en triple buffer, et le mode vidéo est
  choisi selon la taille disponible : VGA 320×240 ou RVB/TV 384×240 en overscan, sinon
  320×400 entrelacé.
- **Cadence.** La logique de jeu tourne toujours à 30 pas par seconde en temps réel. Quand
  l'affichage est plus lent, plusieurs pas sont calculés par image : le jeu garde sa vitesse,
  seule la fluidité baisse.
- **Son.** Le YM2151 (FM) et le SegaPCM sont émulés, puis mixés et envoyés au son DMA du
  Falcon en 12 517 Hz, avec quatre buffers. En option, les `.mod` sont joués par le CPU ou
  par le DSP56001 (voir plus haut).
- **FM sur le DSP** (`fm_dsp=1`). L'émulation du YM2151 est la partie la plus coûteuse du
  son : avec la musique FM, un 68030 a besoin d'environ 0,3 s de temps CPU par pas de jeu
  (1/30 s), d'où la musique qui saccade. La synthèse FM (enveloppes, les 4 opérateurs des
  8 canaux, les 8 algorithmes, le feedback, la stéréo) est faite à la place par un programme
  DSP56001 (`tools/fmdsp/fm_dsp.asm`). Le 68k ne fait plus que décoder les écritures de
  registres, envoyer ce qui a changé par le port hôte du DSP et relire les échantillons :
  environ 15 ms par pas sur un Falcon 030 pour tout le son, au lieu de plus de 300 ms. La
  sortie du DSP est vérifiée **identique échantillon par échantillon** à l'émulation CPU (les
  trois musiques en entier sur le modèle hôte et dans l'émulation DSP de Hatari, puis le jeu
  lui-même), et elle demande en moyenne 11 ms de temps DSP par pas, 26 ms au pire. Le son FM
  et PCM sort avec un pas (1/30 s) de retard. Pendant qu'un `.mod` est joué par le DSP, la FM
  repasse sur le CPU.
- **Entrées.** Un gestionnaire d'interruption clavier en assembleur (`kbd_asm.S`) remplace le
  vecteur ACIA pendant la partie et le restaure en sortie. Il lit le joystick IKBD et les
  ports joystick étendus.
- **Assembleur 68k** (`src/main/atari/*.S`) : lignes de sprites (avec un chemin rapide
  030/060), segments de route, tuiles 8×8, canaux SegaPCM, conversion 16 bits, replay DSP.
- **Mémoire.** L'opérateur `new` remet la mémoire à zéro, car TOS ne le fait pas alors que le
  moteur en a besoin. L'écran et les buffers son sont en ST-RAM, tout le reste peut aller en
  Fast RAM.

Le rôle de chaque fichier est détaillé dans [`ATARI_PORT_FILES.md`](ATARI_PORT_FILES.md)
(en anglais).

---

## Performances mesurées

Mesures faites sous Hatari, en temps émulé, avec le jeu qui roule tout seul en mode démo, en
pleine résolution :

| Machine | Temps par image | Vitesse du jeu |
|---|---|---|
| Falcon 030 d'origine @ 16 MHz | ~1,3 s | ~35-55 % du temps réel |
| 68060 @ 32 MHz + Fast RAM (CT60) | ~0,065 s (15 images/s) | **100 %** du temps réel |

Effet de `vscale` sur le 68060 émulé : 100 donne 18,3 i/s, 67 donne 21,5 i/s et 50 donne
23,1 i/s.

---

## Limites connues et points non testés

- **Rien n'a encore été vérifié sur une vraie machine.** Tous les tests ont été faits sous
  Hatari, où l'émulation du 68060 est marquée « expérimentale ».
- Les ports joystick étendus (v0.27) n'ont pas été testés avec un pad réellement branché.
- Les menus frontend de Cannonball (réglages, Time Trial…) ne sont pas inclus : le jeu démarre
  directement.
- Pas de volant ni de pédales analogiques.
- Sur une vraie machine sans FreeMiNT, il faut `freemint=0` et des ROMs renommées en 8.3.
- Clavier : le gestionnaire désactive les paquets souris et joystick de l'IKBD au démarrage.
  Si une touche semble rester « enfoncée », c'est le premier endroit à regarder
  (`atari/input.cpp`).

Les retours sont bienvenus, surtout sur vrai matériel (Falcon 030 d'origine, CT60/CT63, pads
Jaguar).

---

## Documentation détaillée

| Fichier | Contenu |
|---|---|
| [`README_ATARI.md`](README_ATARI.md) | Documentation technique complète du portage : build, options de compilation, Hatari, pièges, mesures (en anglais) |
| [`ATARI_PORT_FILES.md`](ATARI_PORT_FILES.md) | Rôle de chaque fichier ajouté ou modifié, points d'accroche dans le moteur, bugs corrigés (en anglais) |
| [`DSP_NOTES.md`](DSP_NOTES.md) | Journal du travail sur le DSP56001 (en anglais) |
| [`VERSION.txt`](VERSION.txt) | Changements de la v0.27 (en anglais) |
| [`outrun.ini.example`](outrun.ini.example) | Toutes les options d'exécution (en anglais) |
| [`README_CANNONBALL.md`](README_CANNONBALL.md) | README d'origine de Cannonball (build Windows / Linux / Pi) |

---

## Crédits et licence

- **Cannonball** © Chris White et l'équipe Cannonball : moteur et reverse engineering
  d'OutRun ([github.com/djyt/cannonball](https://github.com/djyt/cannonball),
  [blog Reassembler](http://reassembler.blogspot.com/)). Ce portage n'existerait pas sans
  leur travail.
- **Replay DSP SoundTracker** : Simplet / ABSTRACT (archive `dsptrack`, dhs.nu).
- **DSPMOD 3.4** : bITmASTER de TCE.
- **Émulation du YM2151** : Jarek Burczynski (MAME), telle qu'utilisée par Cannonball ; le
  programme FM DSP56001 la suit exactement.
- **Portage Atari Falcon** (Falcon 030 / CT60 / CT63) : Popov2026.

Ce dépôt est distribué sous la **licence Cannonball** ([`docs/license.txt`](docs/license.txt)) :

- redistribution **non commerciale** uniquement ;
- toute version modifiée doit être accompagnée de **ses sources complètes**, ce que fait ce
  dépôt ;
- la notice de copyright doit être conservée.

*OutRun est une marque de SEGA Corporation. Ce projet n'est pas affilié à SEGA. Aucune ROM ni
musique protégée n'est incluse.*
