"""Measures what listening costs the process in CPU time. Run inside the editor by Measure-ListeningCost.ps1.

It only uses the plugin's Blueprint API (Init, Enable Keyword Mode, Shutdown), which has
not changed since the Unreal Engine 4 plugin, so the same script measures any version.

Phases, each timed with the process's own CPU clock:
  1. idle       the editor doing nothing in particular
  2. listening  the microphone open, the decoder waiting for speech
The difference is what the plugin adds. Nothing is recorded or kept.
"""
import json
import os
import time

import unreal

IDLE_SECONDS = float(os.environ.get("SPHINX_MEASURE_IDLE", "15"))
LISTEN_SECONDS = float(os.environ.get("SPHINX_MEASURE_LISTEN", "30"))
SETTLE_SECONDS = 6.0  # model load and microphone start are not what is being measured
OUTPUT = os.environ.get("SPHINX_MEASURE_OUTPUT", os.path.join(unreal.Paths.project_saved_dir(), "SpeechRecognition", "listening-cost.json"))

state = {"phase": "wait", "since": time.time(), "cpu": time.process_time(), "result": {}, "handle": None}


def find_subsystem():
    # Python has no accessor for world subsystems: look the object up under the editor world by name
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    for index in range(16):
        found = unreal.find_object(world, "SpeechRecognitionSubsystem_%d" % index)
        if found is not None:
            return found
    raise RuntimeError("The editor world has no Speech Recognition Subsystem.")


def begin(phase):
    state["phase"] = phase
    state["since"] = time.time()
    state["cpu"] = time.process_time()


def measured():
    wall = time.time() - state["since"]
    cpu = time.process_time() - state["cpu"]
    return {"seconds": round(wall, 2), "cpu_seconds": round(cpu, 3), "cores": round(cpu / wall, 4)}


def tick(delta):
    try:
        step(delta)
    except Exception as error:  # never leave a headless editor running on a broken script
        unreal.log_error("measure_listening_cost: %s" % error)
        unreal.unregister_slate_post_tick_callback(state["handle"])
        unreal.SystemLibrary.quit_editor()


def step(_delta):
    elapsed = time.time() - state["since"]
    phase = state["phase"]

    if phase == "wait" and elapsed >= 5.0:  # let the editor finish starting
        begin("idle")
    elif phase == "idle" and elapsed >= IDLE_SECONDS:
        state["result"]["idle"] = measured()
        speech = find_subsystem()
        phrase = unreal.RecognitionPhrase()
        phrase.set_editor_property("phrase", "go forward")
        phrase.set_editor_property("tolerance", unreal.PhraseRecognitionTolerance.VE_5)
        started = speech.init(unreal.SpeechRecognitionLanguage.VE_ENGLISH, True) and speech.enable_keyword_mode([phrase])
        state["result"]["started"] = bool(started)
        begin("settle")
    elif phase == "settle" and elapsed >= SETTLE_SECONDS:
        begin("listening")
    elif phase == "listening" and elapsed >= LISTEN_SECONDS:
        state["result"]["listening"] = measured()
        speech = find_subsystem()
        if hasattr(speech, "get_state"):
            state["result"]["state_at_end"] = str(speech.get_state())
        speech.shutdown()
        idle = state["result"]["idle"]["cores"]
        listening = state["result"]["listening"]["cores"]
        state["result"]["added_by_listening_cores"] = round(listening - idle, 4)
        os.makedirs(os.path.dirname(OUTPUT), exist_ok=True)
        with open(OUTPUT, "w", encoding="utf-8") as handle:
            json.dump(state["result"], handle, indent=2)
        unreal.log("LISTENING COST " + json.dumps(state["result"]))
        unreal.unregister_slate_post_tick_callback(state["handle"])
        begin("done")
        unreal.SystemLibrary.quit_editor()


state["handle"] = unreal.register_slate_post_tick_callback(tick)
