# Backdraft — NINE HANDS | DSP

Riser « reverse reverb » en un clic, VST3 pour Windows et macOS, inspiré d'AtmosRiser (WaveAtmos). Backdraft capture une phrase sur la piste (ou charge un fichier audio), fabrique la montée de reverb inversée qui finit exactement là où la phrase commence, et la donne à glisser directement dans la timeline du DAW.

<p align="center">
  <a href="https://github.com/Nine-Hands-Dsp/Backdraft/releases/latest/download/Backdraft-VST3-Windows.zip"><img alt="Télécharger pour Windows" src="https://img.shields.io/badge/%E2%AC%87%20T%C3%A9l%C3%A9charger-Windows-0078D6?style=for-the-badge"></a>
  <a href="https://github.com/Nine-Hands-Dsp/Backdraft/releases/latest/download/Backdraft-VST3-macOS.zip"><img alt="Télécharger pour macOS" src="https://img.shields.io/badge/%E2%AC%87%20T%C3%A9l%C3%A9charger-macOS-111111?style=for-the-badge&logo=apple&logoColor=white"></a>
  <br>
  <a href="https://github.com/Nine-Hands-Dsp/Backdraft/releases/latest/download/Backdraft-Notice.pdf"><img alt="Télécharger la notice (PDF)" src="https://img.shields.io/badge/%F0%9F%93%96%20Notice-PDF-919496?style=for-the-badge"></a>
  <br>
  <a href="https://github.com/Nine-Hands-Dsp/Backdraft/releases/latest"><img alt="Dernière version" src="https://img.shields.io/github/v/release/Nine-Hands-Dsp/Backdraft?label=derni%C3%A8re%20version&color=ff7a26"></a>
</p>

## Fonctions

- **CAPTURE** : enregistre ce qui passe sur la piste (lancer la lecture, cliquer CAPTURE, puis STOP ; 30 s au plus). Le signal de la piste traverse le plugin sans modification.
- **IMPORT** : charge un fichier WAV, AIFF, FLAC, MP3 ou OGG (ou le déposer sur l'écran).
- **Riser en un clic** : l'attaque de la phrase est retournée, passée dans une grande reverb (8 lignes à retard), puis la queue est retournée à son tour : la montée finit pile au début de la phrase.
- **REVERSE** : potard cranté (résistance entre deux crans) pour la longueur de la montée, calée sur le tempo du projet (1 temps, 2 temps, 1, 2, 4 ou 8 mesures).
- **VOX / INST** : VOX filtre le grave et active l'anti-sifflantes automatique (les « S » ne repartent pas à l'envers dans la montée) ; INST garde tout le spectre pour les instruments.
- **Effets en un clic** : PHONE, LO-FI, CHORUS, REVERB (espace à l'endroit sur la montée), DRIVE (saturation qui monte avec le riser), TREMOLO (qui accélère).
- **LISTEN / LOOP** : écoute de la montée suivie de la phrase d'origine, dans la sortie du plugin.
- **DRAG** : glisser le riser de la fenêtre vers la timeline de Reaper (fichier WAV 24 bits, rangé dans `Documents/Nine Hands DSP/Backdraft/Risers`).
- 13 presets d'usine, sauvegarde et chargement de vos presets (`Documents/Nine Hands DSP/Backdraft/Presets`).
- MIDI Learn sur tous les réglages (aucun CC par défaut), fenêtre redimensionnable de 60 à 150 %, réglage Glow, anti-gel v2.

## Installation

Les boutons ci-dessus téléchargent toujours la dernière version.

- **Windows** : décompresser et copier `Backdraft.vst3` dans `C:\Program Files\Common Files\VST3`.
- **macOS** : décompresser et copier `Backdraft.vst3` dans `~/Library/Audio/Plug-Ins/VST3` (le plugin est signé ad hoc ; au premier lancement, autoriser si besoin dans Réglages Système > Confidentialité et sécurité).

Dans Reaper, le plugin apparaît sous le nom **Backdraft (NINE HANDS | DSP)**.

## Compilation

JUCE 8.0.15 (récupéré par CMake) :

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

`tools/render` compile le plugin sous Linux, teste le moteur sur une phrase de synthèse (montée, anti-sifflantes, capture, écoute, export) et exporte des captures de l'interface. `tools/make_knob.py` génère les calques du potard.
