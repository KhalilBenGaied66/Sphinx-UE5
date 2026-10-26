# Third-party notices

The plugin's own source is in the public domain (see [License.md](License.md)).
What it builds on is not, and keeps its own terms.

## PocketSphinx and SphinxBase

`Plugins/SpeechRecognition/ThirdParty` holds the headers, import libraries and
64-bit Windows DLLs of PocketSphinx and SphinxBase, exactly as the original
plugin shipped them in 2016: a build of PocketSphinx "5prealpha" that Shane
Colbert modified to accept a list of key phrases at run time
(`ps_set_keyphrase`). The DLLs are dated 22 May 2016. They have not been
rebuilt for this repository.

Both libraries are distributed under this licence:

```
Copyright (c) 1999-2016 Carnegie Mellon University.  All rights
reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in
   the documentation and/or other materials provided with the
   distribution.

This work was supported in part by funding from the Defense Advanced
Research Projects Agency and the National Science Foundation of the
United States of America, and the CMU Sphinx Speech Consortium.

THIS SOFTWARE IS PROVIDED BY CARNEGIE MELLON UNIVERSITY ``AS IS'' AND
ANY EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL CARNEGIE MELLON UNIVERSITY
NOR ITS EMPLOYEES BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

A game that ships the DLLs must reproduce that notice in its documentation or
credits.

## Test recordings

`Plugins/SpeechRecognition/Resources/TestAudio/pocketsphinx/goforward.raw` is a
recording taken from the test data of the PocketSphinx source distribution,
under the licence above.

The rest of the test audio is not in the repository:

- English and French utterances are synthesised on your machine by
  `Tools/New-TestAudio.ps1` with the text-to-speech voices installed in Windows.
- German recordings are downloaded by `Tools/Get-TestSpeech.ps1` from VoxForge
  (`ralfherzog-20080214-de115`, (C) 2008 Ralf Herzog, GNU GPL 3). They are used
  for testing only and are not redistributed here.
- Tunisian Darija clips are recorded by `Tools/DarijaEval/record.py` or
  synthesised by `Tools/DarijaEval/placeholder.py` on your machine, and stay in
  `Tools/DarijaEval/recordings/`, which is not in the repository. Recordings of
  people are personal data: keep them with each speaker's consent.
- `Plugins/SpeechRecognition/Resources/TestAudio/darija/` holds text only: the
  command spellings and transcripts that the matcher's parity test uses.

## Speech models

No model is in the repository. `Tools/Get-Models.ps1` downloads them from their
publishers, and each keeps its publisher's licence:

| Language | What | Publisher | Licence |
|---|---|---|---|
| US English | acoustic model `cmusphinx-en-us-ptm-5.2` | CMUSphinx | BSD-style (see its README) |
| US English | dictionary `cmudict-en-us.dict`, language model `en-us.lm.bin` | CMUSphinx / CMUdict | BSD-style |
| French | acoustic model `cmusphinx-fr-ptm-5.2`, `fr.dict`, `fr-small.lm.bin` | CMUSphinx / Université du Maine | BSD-style (the script saves its `LICENSE` next to the model) |
| German | acoustic model, dictionary and language model `cmusphinx-de-voxforge-5.2` | VoxForge / Zamia Speech | GNU LGPL 3 |
| The second engine: `any` and every Arabic code (`Content/model/omnilingual`) | Omnilingual ASR CTC 300M, int8 ONNX export `OpenVoiceOS/omnilingual-asr-ctc-300m-onnx` (revision `bc55e1d`) | Meta (model), OpenVoiceOS (export) | Apache-2.0 (the script saves the model card as `MODEL_CARD.md`) |

The Darija engine runs the model with Unreal Engine's NNERuntimeORT plugin
(ONNX Runtime), which ships with the engine under Epic's licence.

Read the licence of a model before you ship a game with it.

## Lineage of the plugin

- Original plugin for Unreal Engine 4: Shane Colbert,
  <https://github.com/shanecolb/sphinx-ue4> (The Unlicense).
- Unreal Engine 5 port, kept current through UE 5.8: Urszula Kustra,
  <https://github.com/ukustra/sphinx-ue4> (The Unlicense).
- This repository starts from that port at commit `08e3957` and adds the changes
  listed in [CHANGELOG.md](CHANGELOG.md).
