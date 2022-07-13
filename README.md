# Tracktion for Sound2Synth

This plug-in is built based on the [tracktion_enngine](https://github.com/Tracktion/tracktion_engine) and is specialized for using the [Sound2Synth Dexed](https://github.com/Sound2Synth/Sound2Synth-Plug-Ins) plug-in. Please note that the included `tracktion_engine` module is modified, which is different from the original github repository.

This plug-in is designed for replacing PyAU, which functionality is to convert or batch convert `.json` formatted Dexed parameters into rendered audio.

A video tutorial is available at [this link](https://www.dropbox.com/sh/fs9fu0y6iw45u90/AACfievRzwBMoOmGU4zLxxDYa?dl=0).

## Setup

An off-the-shelf version of this plug-in is available under the `Builds/` folder. For example, on Mac, use `Builds/MacOSX/build/Release/PluginDemo.app`.

Please make sure the [Sound2Synth Dexed](https://github.com/Sound2Synth/Sound2Synth-Plug-Ins) is correctly installed before using this plug-in. Currently only AU version of [Sound2Synth Dexed](https://github.com/Sound2Synth/Sound2Synth-Plug-Ins) is supported.

1. Plug-in Setup: A scan over plug-ins is required when running the plug-in for the first time. Please select `Plugins` - `Options` - `Scan for new or updated AudioUnit plug-ins` and wait for the process to finish. Please check that `Dexed` in `AudioUnit` format is detected.

2. Dexed Setup: On the right of `Track 1`, select `+` - `Digital Suburban` - `Dexed` to load the Dexed plug-in. By pressing `D` the Dexed interface will pop up.

3. MIDI Setup: When running [Sound2Synth](https://github.com/Sound2Synth/Sound2Synth), default midi files (each contain one and only one note played) are generated under `midi_settings`. For convenience, we provided the default generated midi file here at `MIDI/60.mid`. In the plug-in first select `Track 1` to make sure it is highlighted, then select `Import Midi` and choose the provided midi file (or any other midi file you prefer).

<br/>

## Usage

Use `Render` to directly render using the current configuration of the loaded Dexed plug-in, or select `Batch` and choose the folder of the `.json` formatted parameters to batch render.

<br/>

## Citation

```
@inproceedings{sound2synth,
  title     = {Sound2Synth: Interpreting Sound via FM Synthesizer Parameters Estimation},
  author    = {Chen, Zui and Jing, Yansen and Yuan, Shengcheng and Xu, Yifei and Wu, Jian and Zhao, Hang},
  booktitle = {AI, the Arts and Creativity – Special Track of the 31st International Joint Conference on Artificial Intelligence},
  publisher = {International Joint Conferences on Artificial Intelligence Organization},             
  year      = {2022},
}
```