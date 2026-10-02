# Fichiers du portage Atari — rôle de chacun

Ce document couvre uniquement le code écrit/modifié pour le portage Atari
(Mega STE + Falcon 030/060). Le reste du moteur Cannonball (`src/main/engine/`,
`src/main/frontend/`, etc., hors points d'accroche listés en bas de page) est
le code d'origine du projet, non documenté ici — tout le crédit pour le
moteur, le reverse engineering d'OutRun et Cannonball lui-même revient à
**Chris White** (créateur du projet, [github.com/djyt/cannonball](https://github.com/djyt/cannonball)),
sans qui ce portage Atari n'aurait tout simplement pas de base sur laquelle
partir. Merci à lui. Licence non-commerciale : voir `license.txt` — toute
redistribution d'un dérivé (comme ce portage) doit inclure les sources
complètes.

## Scripts de build (racine)

| Fichier | Rôle |
|---|---|
| `build_atari.sh` | Script de build principal. Compile tous les `.cpp`/`.S` listés dans `Makefile.atari` avec `-mcpu=$CPU`, puis lie avec `-mcpu=${LINKCPU:-$CPU}`. **`LINKCPU` doit être `68000`** même pour un build 68030/68060 : le cross-compilateur choisit un jeu de libc/libstdc++ différent (`multilib`) selon le CPU cible à l'édition de liens, et seul le multilib de base correspond au `crt0.o` générique fourni — un mismatch plante le binaire juste après `Pexec`, avant même `main()`. Voir `README_ATARI.md`. |
| `build_release.sh` | Construit les deux binaires de release propres (sans instrumentation de debug) : `CB030.TOS` et `CB060.TOS` (noms 8.3, indiquant le CPU cible). Chiffres dans le nom : peut gêner l'argument `--auto` de Hatari lui-même (`hatari --auto CB030.TOS` peut échouer "cannot find the folder or file"), mais n'affecte pas le lancement réel du jeu, qui passe toujours par le dossier `AUTO\` natif (`CB030.PRG`) et non par `--auto` ; voir `README_ATARI.md`. |
| `Makefile.atari` | Liste des fichiers source à compiler pour la cible Atari (lue par `build_atari.sh` via `grep`). |

## `src/main/main_atari.cpp`

Point d'entrée du programme pour cette cible, remplace `main.cpp` (SDL2,
non compilé ici). Contient :
- `operator new/delete` : TOS ne fournit pas de pages mémoire mises à zéro
  contrairement à un OS de bureau ; on force le zéro-fill explicitement.
- `tick()` : une frame de logique de jeu (équivalent du corps de boucle SDL2).
- Trois variantes de `main_loop()` selon les flags de compilation :
  - `PLATFORM_FALCON` + pas de `OLD_PACING` (celle utilisée en pratique) :
    boucle à cadence fixe, 30 pas de logique/s, une image dessinée toutes les
    K pas (K auto-ajusté pour tenir le budget de temps — voir commentaire
    en tête de fonction).
  - `PLATFORM_FALCON` + `OLD_PACING` : ancienne version de la boucle,
    conservée pour comparaison.
  - Sans `PLATFORM_FALCON` (Mega STE) : boucle simple calée sur le VBL.
- `main()` : séquence de boot (mode superviseur, détection Mega STE via
  cookie machine, chargement config/options/ROMs, init vidéo/son/input,
  lancement direct en jeu — pas de menu frontend dans ce portage).
- Aides de diagnostic activables par flag de compilation (aucune n'est
  active par défaut) : `STARTUP_DEBUG` (checkpoints imprimés à chaque étape
  du boot), `EARLY_RETURN_TEST` (sort de `main()` immédiatement, pour
  distinguer un crash dans le code applicatif d'un crash dans le runtime C
  avant `main()`), `BENCH_N=<n>` (mesure la cadence sur n images et imprime
  une ligne `BENCH pictures=...`), `KEYTRACE` (voir `atari/input.cpp`),
  `MUSIC_RENDER` (outil hors-jeu qui rejoue une commande son et écrit le
  résultat dans des fichiers `.raw`).

## `src/main/atari/` — backends spécifiques à la plateforme

| Fichier | Rôle |
|---|---|
| `input.cpp` / `.hpp` | Clavier uniquement (pas de manette). Un gestionnaire d'interruption (`kbd_asm.S`) remplace le vecteur ACIA clavier pour maintenir une table d'état touche-appuyée sans bloquer. `poll()` copie cet état dans `keys[]` selon un mapping de scancodes fixe. `frame_done()` sauvegarde `keys_old` **avant** de relire l'état courant — l'ordre inverse (corrigé cette session) empêchait `has_pressed()` de détecter un front montant. |
| `kbd_asm.S` | Interruption clavier bas niveau : lit les octets de l'ACIA, met à jour `atari_scan[scancode]` (1=appuyée/0=relâchée), ignore les paquets souris/joystick/horloge par leur longueur. |
| `options.cpp` / `.hpp` | Lit `outrun.ini` (à côté de `roms\` — nom choisi exprès pour rester en 8.3 : 6+3 caractères, voir `freemint` ci-dessous) : ombres, distance de rendu, cadence (`cadence`), son (`sound`), `music` (0/1, coupe la musique FM sans toucher aux bruitages), `freemint` (0/1, défaut 1 : garde les noms de ROM d'origine, nécessite FreeMiNT sur vrai matériel ; 0 = attend un jeu de ROMs renommé en 8.3, voir `romloader.cpp` et la table dans `README_ATARI.md`), et `road_hres` (voir plus bas — **doit rester à 0 par défaut**, c'est une optimisation visuellement risque non encore validée). |
| `src/main/romloader.cpp` | `RomLoader::load_rom()` : quand `atari_opt.freemint==0`, remappe les noms de ROM (`epr-10380b.133` → `E10380b.133`) avant l'ouverture du fichier — voir `atari_short_name()`. Par défaut (`freemint=1`) les noms d'origine sont utilisés tels quels. |
| `timer.cpp` / `.hpp` | Pacing basé sur le compteur système 200Hz (`_hz_200`, adresse 0x4BA) plutôt que sur `SDL_GetTicks`. `frame_pace()` plafonne à 60Hz sans jamais rattraper un retard par une rafale. |
| `audio.cpp` / `.hpp` | Backend DMA sound de la STE — récupère les buffers déjà synthétisés par le moteur (YM2151 + SegaPCM, non touchés), les mixe avec le lecteur `.mod` si actif (`-DMOD_MUSIC`), et pousse le résultat vers le DMA STE. |
| `modplayer.cpp` / `.hpp` | Lecteur de fichiers `.mod` (Amiga ProTracker, 4 canaux) qui joue la musique du jeu à la place de la puce FM quand `mod=1` (`Music\TRACK1.MOD` à `TRACK4.MOD`, fournis par l'utilisateur). Détecte les deux variantes d'en-tête (15 et 31 échantillons), gère les effets courants (arpège, portamento, vibrato, volume slide, saut de position, tempo). Mixage sur le CPU par défaut. Avec `mod_dsp=1`, le morceau est joué par le DSP (`dsp_replay.*`) : l'interruption du replay fait avancer la partition et lit les échantillons, `mix()` ne fait alors rien. |
| `dsp_replay.cpp` / `.hpp` / `dsp_replay_asm.S` / `dsp_tracker_p56.h` | Lecture des `.mod` par le DSP56001 (option `mod_dsp`). Le côté DSP est le replay SoundTracker de Simplet / ABSTRACT (archive `dsptrack` de dhs.nu), non modifié (`dsp_tracker_p56.h`). Le côté 68k tourne sous interruption Timer A à 50 Hz : à chaque trame il envoie au DSP le volume et la hauteur de chaque voie puis les octets d'échantillon demandés (rien n'est stocké sur le DSP, donc aucune limite de taille de module). Six voies : les 4 du module, plus une paire stéréo qui transporte le mixage FM + PCM du jeu, puisque le DAC n'écoute que le DSP tant que ce mode est actif. Vérifié sous Hatari `--dsp emu` (capture des échantillons envoyés au DAC). |
| `gemredraw.cpp` | Demande à l'AES de redessiner tout l'écran à la sortie du jeu (appelé seulement sous MiNT, donc sous un AES multitâche comme XaAES, qui ne redessine pas le bureau tout seul). |
| `video.cpp` / `.hpp` | Backend vidéo Mega STE (16 couleurs, réduction par histogramme + conversion chunky-to-planar). Le moteur compose une image en indices de palette dans un buffer partagé (`src/main/video.cpp`, non modifié) ; ce fichier fait la conversion vers le matériel réel. |
| `video_falcon.cpp` | Backend vidéo Falcon : 16 bits vrais couleurs (RGB565), une seule table de correspondance 65536 couleurs, pas de réduction de palette ni de packing bitplane (contrairement à la STE). |
| `road_asm.S` | Boucles internes du rendu de route (`atari_fill16`, `atari_road_copy1`, `atari_road_copy2` + variantes `_half` pour `road_hres`). Appelé depuis `hwvideo/hwroad.cpp`. |
| `sprite_asm.S` | Version 68000 de la boucle interne de rendu sprite (une ligne d'un sprite, zoom horizontal). |
| `sprite_asm030.S` | Même contrat que `sprite_asm.S`, avec un chemin rapide pour 68030/68060 (choisi automatiquement selon le CPU cible). |
| `tile_asm.S` | Dessine une tuile 8x8 (4 bits/pixel, 0 = transparent) dans le buffer d'indices. |
| `truecolor_asm.S` | Conversion finale indices → RGB565 (`dst[i] = pal[px[i]]`), spécifique Falcon. |
| `pcm_asm.S` | Boucle interne d'un canal SegaPCM (lecture d'échantillon, volume, bouclage). |
| `video_asm.S` | Histogramme de palette + conversion chunky-to-planar, 68000 pur (pas de multiplication 32 bits, pas d'instructions 020+) pour la STE. |

## Points d'accroche dans le moteur partagé (`#ifdef PLATFORM_ATARI` / `PLATFORM_FALCON`)

Ces fichiers appartiennent au moteur Cannonball d'origine ; seules les
sections indiquées ont été touchées pour le portage.

| Fichier | Ce que fait la section Atari |
|---|---|
| `src/main/frontend/config.cpp` | Branche `PLATFORM_ATARI` de `Config::load()` : pas de Boost/XML disponible, applique les valeurs par défaut directement en dur (voir aussi `ostats.cpp`/`save_tiletrial_scores` skip). `data.rom_path="roms/"`, `data.crc32=0` (un `opendir`+CRC32 bloque indéfiniment sous ce toolchain+GEMDOS émulé, remplacé par un `fopen` direct par nom). |
| `src/main/engine/oinputs.hpp` | Redéfinit la lecture des entrées analogiques pour cette cible (clavier uniquement, pas de volant/pédales physiques). |
| `src/main/engine/oroad.cpp` | Appelle les routines assembleur de route (`atari_road_copy*`) à la place du C++ générique quand `PLATFORM_ATARI` est défini. |
| `src/main/engine/osprites.cpp` | Appelle `atari_sprite_line` à la place de la boucle C++ générique. |
| `src/main/hwvideo/hwroad.cpp` | Bascule entre `atari_road_copy1/2` et leurs variantes `_half` selon `atari_opt.road_hres` (voir tableau ci-dessus). |
| `src/main/hwvideo/hwsprites.cpp` | Appelle les routines assembleur sprite (`sprite_asm.S`/`sprite_asm030.S`). |
| `src/main/engine/osprites.cpp` | `finalise_sprites()` a déjà sa propre mesure fine (`dosprite`/`blit`/`trafficlogic`/`trafficsnd`) sous `-DPERF_PRINT` ; `-DLOGIC50_FILE` l'écrit aussi dans `SPR.TXT` avec le nombre de sprites actifs. Piste trouvée cette session : `nsprites` saute de ~4-22 (niveau 1) à 63-64 (niveau 2, la zone signalée comme lente) — confirme "trop de sprites" sans encore identifier précisément quelle boucle dans `sprite_copy()` (au-delà de ce que `dosprite`/`blit` mesurent déjà) domine le coût. |
| `src/main/hwvideo/hwtiles.cpp` | Appelle `atari_tile8` (Falcon uniquement) à la place du rendu C++ générique. |
| `src/main/hwaudio/segapcm.cpp` | Appelle `atari_pcm_channel` à la place de la boucle C++ générique pour chaque canal PCM. Contient aussi la mesure `-DPERF_PRINT -DPCM_MEASURE_FILE` du coût par canal (écrite dans `PCMLOG.TXT`, pas sur la console — voir note méthodo ci-dessous). |
| `src/main/engine/omusic.cpp` | `play_music()` : en build `-DMOD_MUSIC`, charge le lecteur `.mod` à la place de la commande YM2151 pour les 3 pistes sélectionnables (pas Last Wave, déclenchée ailleurs). Sinon inchangé (commande YM2151 normale), avec le contrôle `atari_opt.music` (ajouté cette session) pour couper la musique sans toucher aux bruitages. |
| `src/main/engine/ostats.cpp` | `OStats::init()` : aide de test `-DFORCE_CREDIT_TEST` (crédit=1 au boot, jamais en build normal) — utilisée cette session car l'injection de touche synthétique dans Hatari ne s'est jamais montrée fiable pour valider automatiquement un écran nécessitant un crédit. |
| `src/main/main.hpp` | Déclarations partagées spécifiques à la cible Atari (types, macros). |
| `src/main/engine/outrun.cpp` | `jump_table()` : ajout de `-DLOGIC50_FILE` (avec `-DPERF_PRINT`) qui écrit le détail des coûts (switch/inputs/sprites/objets/trafic/ferrari/crash/copy) dans `LOGIC.TXT`, corrélé à l'étage et la position sur la piste — utilisé pour investiguer le ralentissement rapporté dans le tunnel du niveau 2. |
| `src/main/video.cpp` | Point de sortie du buffer d'indices composé par le moteur vers le backend `atari/video.cpp` ou `atari/video_falcon.cpp` selon la cible. |

## Documentation

| Fichier | Rôle |
|---|---|
| `README_ATARI.md` | Commandes de build exactes (dont le piège `LINKCPU`), options de compilation (`-DBENCH_N`, `-DMOVE16`, `-DCOVERSKIPCHECK`, etc.), commandes de lancement Hatari, résultats de mesure connus. |
| `DSP_NOTES.md` | Notes sur le travail DSP56001 (encodeur assembleur maison, vérification du round-trip host-port, bug `jclr_reg` trouvé et corrigé) — sans rapport avec le rendu vidéo/route ci-dessus. |
| `ATARI_PORT_FILES.md` | Ce fichier. |

## Bugs corrigés cette session (déjà appliqués dans les sources livrées)

1. **`input.cpp` — `frame_done()`** : l'ordre `poll()` puis copie dans
   `keys_old` rendait `keys_old` toujours égal à `keys`, donc
   `has_pressed()` (front montant) ne pouvait jamais renvoyer vrai — la
   touche Entrée (crédit/start) ne faisait rien. Corrigé en sauvegardant
   `keys_old` **avant** `poll()`, comme le fait la version SDL2 de référence
   (`src/main/sdl2/input.cpp`).
2. **`outrun.ini` livré avec `road_hres=1` par défaut** — ce réglage
   active un chemin assembleur (`atari_road_copy*_half`) pas encore validé
   visuellement, à l'origine d'un rendu de route déformé signalé en test.
   Remis à `road_hres=0` par défaut ; à ne réactiver qu'après validation
   visuelle explicite.
3. **Son très pauvre/haché** — `sound=2` (défaut) coupe toute synthèse audio
   dès que le CPU est chargé (voir `atari/options.hpp`) ; le jeu étant
   souvent limite en CPU, le son se coupait la plupart du temps. `sound=1`
   dans `outrun.ini` force la synthèse en continu (contrepartie : un peu
   plus de charge CPU dans les scènes déjà lourdes).

## Note méthodologique : capture de sortie console peu fiable

Plusieurs diagnostics cette session ont montré que la sortie console du jeu
(`printf`/`Cconws`) redirigée vers un fichier host via `hatari ... > out.log`
**n'arrive pas de façon fiable tant que le processus tourne** — ni `fflush()`
ni même tuer le process ne l'ont fait apparaître dans plusieurs tests, alors
que la même ligne finit par apparaître après une terminaison propre dans
d'autres cas (mécanisme non totalement compris). Pour tout futur diagnostic
qui doit lire un résultat pendant que le jeu tourne : **écrire dans un
fichier sur le disque monté** (`fopen(...,"a")` + `fclose()`, qui force un
vrai `Fwrite`/`Fclose` GEMDOS) plutôt que sur la console — fiable à chaque
test cette session, contrairement à la redirection stdout. Voir
`PCM_MEASURE_FILE` dans `hwaudio/segapcm.cpp` pour un exemple.

Autre piège rencontré : certains builds de diagnostic échouaient à charger
les ROMs (`cannot open rom`) alors que les fichiers étaient présents et
identiques à un déploiement qui fonctionnait — la cause exacte n'a pas été
isolée (pas un problème de contenu des fichiers), mais dupliquer `roms/`,
`res/` et `outrun.ini` **à la fois à la racine du lecteur GEMDOS monté
et dans `AUTO\`** a résolu le problème à chaque fois où il s'est produit.
