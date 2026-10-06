"""Record and stop are pressed on the real plugin while a fake phone streams.

Checks the "press record" chain up to the Ableton helper: the take is cut to the
recording and a note with the start position is left for the helper to place.
Two ways of learning that recording started are covered:

  host    the host reports it through the plugin interface (other DAWs)
  helper  the Ableton helper sends start/stop messages (this script plays the helper)

The helper script itself is tested in test_ableton_helper.py.
"""
import json, os, socket, subprocess, sys, tempfile, time, wave
from pathlib import Path

here = Path(__file__).parent
exe = here.parent / "build" / "MobiMicHostRecordTest_artefacts" / "Release" / "MobiMicHostRecordTest.exe"
failures = 0


def check(name, ok, detail=""):
    global failures
    failures += not ok
    print(("PASS  " if ok else "FAIL  ") + name + (": " + str(detail) if detail != "" else ""))


def run(mode):
    print(f"--- recording reported by the {mode}")
    # A private handoff folder, so an Ableton helper running on this machine never places the test take.
    handoff = Path(tempfile.mkdtemp(prefix="mobimic-host-test-"))
    env = dict(os.environ, MOBIMIC_HANDOFF=str(handoff))

    def helper_alive():
        (handoff / "helper.json").write_text(json.dumps({"time": int(time.time()), "result": ""}))

    if mode == "helper":
        helper_alive()

    host = subprocess.Popen([str(exe)] + (["helper"] if mode == "helper" else []), stdout=subprocess.PIPE, text=True, env=env)
    port = json.loads(host.stdout.readline())["port"]
    phone = subprocess.Popen([sys.executable, str(here / "fake_phone.py"), str(port), "12", "1.0", "0"],
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, cwd=handoff)
    expected_beats, expected_seconds = None, None

    if mode == "helper":
        # Play the helper: keep the heartbeat fresh, then send start and stop four seconds apart.
        udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        for _ in range(7):
            time.sleep(0.5); helper_alive()
        control = ("127.0.0.1", json.loads((handoff / "plugin.json").read_text())["control_port"])
        udp.sendto(json.dumps({"cmd": "start", "beats": 16.0, "bpm": 120.0}).encode(), control)
        started = time.perf_counter()
        for _ in range(8):
            time.sleep(0.5); helper_alive()
        udp.sendto(json.dumps({"cmd": "stop"}).encode(), control)
        expected_beats, expected_seconds = 16.0, time.perf_counter() - started

    did = json.loads(host.communicate(timeout=60)[0].strip().splitlines()[-1])
    phone.wait(timeout=30)

    if mode == "host":
        expected_beats, expected_seconds = did["start_beats"], did["recorded_samples"] / 48000

    notes = list((handoff / "pending").glob("*.json")) if (handoff / "pending").exists() else []
    log = (handoff / "log.txt").read_text() if (handoff / "log.txt").exists() else ""

    check("phone connected to the plugin", did["connected"])
    check("one note left for the helper", len(notes) == 1, len(notes))

    if len(notes) == 1:
        note = json.loads(notes[0].read_text())
        with wave.open(note["file"]) as w:
            frames, fmt = w.getnframes(), (w.getnchannels(), w.getsampwidth(), w.getframerate())
            audio = w.readframes(frames)
        Path(note["file"]).unlink()

        check("note has the position where recording started", abs(note["start_beats"] - expected_beats) < 1e-6,
              f"{note['start_beats']} vs {expected_beats}")
        # The clocks are paced separately here, so allow 100 ms either way.
        check("take is as long as the recording", abs(frames / 48000 - expected_seconds) <= 0.1,
              f"{frames / 48000:.3f} s vs {expected_seconds:.3f} s recorded")
        check("take file matches the note", fmt == (1, 2, 48000) and frames == note["samples"])
        check("take contains audio", max(audio) > 0)

    source = "Ableton helper" if mode == "helper" else "Host"
    check("log explains what happened", f"{source} started recording" in log and "handed to the Ableton helper" in log)
    print(log.strip())


run("host")
run("helper")
sys.exit(1 if failures else 0)
