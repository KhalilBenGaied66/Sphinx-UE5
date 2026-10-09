"""Writes commands.tsv and utterances.tsv, the lists the tests and the benchmark read.

    python Tools/New-TestManifest.py

Only needed when the lists change: the two files are committed. Afterwards run
Tools/New-TestAudio.ps1 (English, French) and Tools/Get-TestSpeech.ps1 (German)
to produce the audio they name.
"""
from pathlib import Path

OUT = Path(__file__).resolve().parent.parent / "Plugins" / "SpeechRecognition" / "Resources" / "TestAudio"

# Synthesised with Windows text-to-speech voices: game-style commands.
COMMANDS = {
    "en": [
        "go forward", "go back", "turn left", "turn right", "jump", "stop",
        "open the door", "close the door", "lights on", "lights off", "follow me", "wait here",
    ],
    "fr": [
        "avance", "recule", "à gauche", "à droite", "saute", "arrête",
        "ouvre la porte", "ferme la porte", "allume la lumière", "coupe la lumière", "suis moi", "attends ici",
    ],
}
CARRIER = {"en": "okay {} now", "fr": "bon {} maintenant"}
NEGATIVES = {
    "en": ["what a lovely morning", "i think it is going to rain", "there is nothing to see", "please be quiet", "it is five to ten", "thank you very much"],
    "fr": ["il fait beau aujourd'hui", "je ne sais pas quoi dire", "bonjour tout le monde", "merci beaucoup", "le chat dort sur le lit", "nous partons demain matin"],
}
GRAMMAR_SENTENCES = [
    "go back five meters", "turn left", "turn right", "open the door", "close the door", "go forward", "go back", "go forward three meters",
]

# Recorded German: sentences from VoxForge (ralfherzog-20080214-de115, GPL 3). No German
# text-to-speech voice is assumed, so the key phrases are taken from what the speaker
# says rather than from a list of game commands. Sentence number -> (text, key phrase).
GERMAN = {
    "01": ("es gibt verschiedene situationen", "verschiedene situationen"),
    "02": ("dieses ziel wurde nur teilweise erreicht", "nur teilweise"),
    "10": ("das wird jederzeit zusammenbrechen", "jederzeit zusammenbrechen"),
    "12": ("es gibt zahlreiche ausnahmen", "zahlreiche ausnahmen"),
    "21": ("es hätte viel geld gewonnen werden können", "viel geld"),
    "30": ("es handelt sich nicht um die aktuelle version", "die aktuelle version"),
    "34": ("mittlerweile ist das doppelt so teuer", "doppelt so teuer"),
    "35": ("die geldmenge ist jährlich um neun prozent gewachsen", "neun prozent"),
    "36": ("es handelt sich um die dritte staffel", "die dritte staffel"),
    "48": ("es handelt sich um ein fass ohne boden", "ein fass ohne boden"),
    "58": ("das hat ganz massive auswirkungen", "ganz massive auswirkungen"),
    "76": ("über das thema wird im radio berichtet", "im radio"),
    "84": ("der geist ist inzwischen aus der flasche entwichen", "aus der flasche"),
    "97": ("das wird uns dann ganz direkt betreffen", "ganz direkt"),
    "99": ("es ist auch unter anderen namen bekannt", "unter anderen namen"),
    # sentences with none of the phrases; 15 shares a word with a phrase without containing it
    "03": ("es gibt viele einsatzmöglichkeiten für holz", ""),
    "06": ("es gibt viele möglichkeiten dazu", ""),
    "07": ("das land soll reformiert werden", ""),
    "08": ("das war eine sehr viel bessere anlage", ""),
    "09": ("es gibt wieder eine erneute welle", ""),
    "11": ("man kann in die aktienmärkte gehen", ""),
    "13": ("die wege zur versorgung sind abgeschnitten", ""),
    "14": ("es gibt keine ausweichmöglichkeiten", ""),
    "15": ("es gibt zahlreiche notizen zu veröffentlichen", ""),
    "16": ("dazu werden die leistungen reduziert", ""),
    "17": ("das ende wird irgendwann eintreten", ""),
    "18": ("es hätte in person geleistet werden müssen", ""),
    "19": ("das hat sich damals deutlich erhöht", ""),
    "20": ("einzelne staaten können ausscheiden", ""),
    "22": ("auf eine weitere anhebung wurde verzichtet", ""),
}


def tolerance(phrase: str) -> int:
    """One word is easy to trigger by accident: strict. Longer phrases: looser."""
    return 8 if len(phrase.split()) == 1 else 5


def slug(text: str) -> str:
    table = str.maketrans("àâçéèêëîïôùûü' ", "aaceeeeiiouuu__")
    return text.translate(table).replace("__", "_")


rows = []
# a person, from the PocketSphinx test suite
rows.append(("en_rec_goforward", "en", "recorded", "0", "pocketsphinx/goforward.raw", "go forward", "1", "go forward ten meters"))

for lang, phrases in COMMANDS.items():
    for phrase in phrases:
        for rate, tag in ((-2, "slow"), (0, "normal"), (2, "fast")):
            rows.append((f"{lang}_{slug(phrase)}_{tag}", lang, "synthetic", str(rate), f"generated/{lang}/{slug(phrase)}_{tag}.wav", phrase, "0", phrase))
        rows.append((f"{lang}_{slug(phrase)}_carrier", lang, "synthetic", "0", f"generated/{lang}/{slug(phrase)}_carrier.wav", phrase, "0", CARRIER[lang].format(phrase)))
    for index, sentence in enumerate(NEGATIVES[lang], start=1):
        rows.append((f"{lang}_negative_{index}", lang, "synthetic", "0", f"generated/{lang}/negative_{index}.wav", "", "0", sentence))

for index, sentence in enumerate(GRAMMAR_SENTENCES, start=1):
    rows.append((f"en_grammar_{index}", "en", "synthetic", "0", f"generated/en/grammar_{index}.wav", "", "1", sentence))

for number, (text, phrase) in sorted(GERMAN.items()):
    rows.append((f"de_voxforge_{number}", "de", "recorded", "0", f"generated/de/de115-{number}.wav", phrase, "0", text))

with (OUT / "commands.tsv").open("w", encoding="utf-8", newline="\n") as f:
    f.write("language\tphrase\ttolerance\n")
    for lang, phrases in COMMANDS.items():
        for phrase in phrases:
            f.write(f"{lang}\t{phrase}\t{tolerance(phrase)}\n")
    for number, (text, phrase) in sorted(GERMAN.items()):
        if phrase:
            f.write(f"de\t{phrase}\t{tolerance(phrase)}\n")

with (OUT / "utterances.tsv").open("w", encoding="utf-8", newline="\n") as f:
    f.write("id\tlanguage\tsource\trate\tfile\texpected\tgrammar\ttext\n")
    for row in rows:
        f.write("\t".join(row) + "\n")

print(len(rows), "utterances")
