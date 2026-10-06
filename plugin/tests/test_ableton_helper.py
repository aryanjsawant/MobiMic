"""Runs the Ableton helper script against a fake Live session (no Ableton needed).

Checks which track it picks and that it places the take at the right position.
"""
import importlib.util, json, os, socket, sys, tempfile, time, types
from pathlib import Path

appdata = tempfile.mkdtemp(prefix="mobimic-helper-test-")
os.environ["APPDATA"] = appdata

framework = types.ModuleType("_Framework")
control_surface = types.ModuleType("_Framework.ControlSurface")


class ControlSurface:
    def __init__(self, c_instance): self._song = c_instance
    def song(self): return self._song
    def log_message(self, *a): pass
    def show_message(self, *a): pass
    def update_display(self): pass
    def disconnect(self): pass


control_surface.ControlSurface = ControlSurface
sys.modules["_Framework"] = framework
sys.modules["_Framework.ControlSurface"] = control_surface

script = Path(__file__).resolve().parents[2] / "ableton" / "MobiMic" / "__init__.py"
spec = importlib.util.spec_from_file_location("MobiMicHelper", script)
helper = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helper)


class Clip:
    warping = True


class Device:
    def __init__(self, name): self.name = name


class Track:
    def __init__(self, name, audio=True, devices=(), arm=False):
        self.name, self.devices, self.arm, self.can_be_armed = name, [Device(d) for d in devices], arm, True
        self.has_audio_input, self.has_midi_input = audio, not audio
        self.placed = []

    def create_audio_clip(self, path, position):
        if not self.has_audio_input:
            raise RuntimeError("Audio clips can only be created on audio tracks")
        clip = Clip(); self.placed.append((path, position, clip)); return clip


class Song:
    def __init__(self, tracks, selected=None):
        self.tracks = tracks
        self.view = types.SimpleNamespace(selected_track=selected or tracks[0])
        self.record_mode = self.is_playing = self.is_counting_in = False
        self.current_song_time, self.tempo = 0.0, 120.0

    def __getattr__(self, name):  # add_xxx_listener / remove_xxx_listener
        if name.endswith("_listener"):
            return lambda callback: None
        raise AttributeError(name)


def run(song, created=None, beats=8.0):
    surface = helper.create_instance(song)
    os.makedirs(helper.PENDING, exist_ok=True)
    note = {"file": r"C:\takes\a.wav", "start_beats": beats, "samples": 48000, "created": created or int(time.time())}
    Path(helper.PENDING, "take.json").write_text(json.dumps(note))
    for _ in range(10):
        surface.update_display()
    status = json.loads(Path(helper.STATUS).read_text())
    assert not os.listdir(helper.PENDING), "note was not consumed"
    assert time.time() - status["time"] < 5
    return status["result"]


failures = 0

def check(name, condition, detail=""):
    global failures
    failures += not condition
    print(("PASS  " if condition else "FAIL  ") + name + (": " + str(detail) if detail else ""))


# 1. Plugin on an audio track: the take goes there, at the recorded position, unwarped.
t = [Track("Drums"), Track("Vocals", devices=["EQ Eight", "MobiMic"]), Track("Keys", audio=False)]
result = run(Song(t))
check("places on the audio track holding MobiMic", len(t[1].placed) == 1 and t[1].placed[0][:2] == (r"C:\takes\a.wav", 8.0), result)
check("turns warping off", t[1].placed and t[1].placed[0][2].warping is False)

# 2. Plugin on a MIDI track: falls back to the armed audio track.
t = [Track("Synth", audio=False, devices=["MobiMic"]), Track("Gtr"), Track("Vox", arm=True)]
result = run(Song(t))
check("plugin on a MIDI track -> armed audio track", len(t[2].placed) == 1 and not t[1].placed, result)

# 3. Plugin on a MIDI track, nothing armed: the selected audio track.
t = [Track("Synth", audio=False, devices=["MobiMic"]), Track("Gtr"), Track("Vox")]
result = run(Song(t, selected=t[1]))
check("nothing armed -> selected audio track", len(t[1].placed) == 1, result)

# 4. No audio track available: says so instead of failing.
t = [Track("Synth", audio=False, devices=["MobiMic"])]
result = run(Song(t))
check("no audio track -> clear message", "No audio track" in result, result)

# 5. A stale note from an earlier session is discarded, not placed.
t = [Track("Vocals", devices=["MobiMic"])]
run(Song(t), created=int(time.time()) - 3600)
check("stale take is ignored", not t[0].placed)

# 6. Pressing record and stop in Live is passed on to the plugin, with the song position.
plugin = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
plugin.bind(("127.0.0.1", 0))
plugin.settimeout(1)
Path(helper.PLUGIN_STATUS).write_text(json.dumps({"control_port": plugin.getsockname()[1], "time": int(time.time())}))

song = Song([Track("Vocals", devices=["MobiMic"])])
surface = helper.create_instance(song)
surface.update_display()
song.record_mode, song.is_playing, song.is_counting_in, song.current_song_time = True, True, True, 15.0
surface.update_display()                      # still counting in: nothing yet
plugin.settimeout(0.2)
try:
    plugin.recvfrom(512); early = True
except socket.timeout:
    early = False
check("waits for the count-in to finish", not early)

plugin.settimeout(1)
song.is_counting_in, song.current_song_time = False, 16.0
surface.update_display()
message = json.loads(plugin.recvfrom(512)[0])
check("record pressed -> tells the plugin to start, with the position", message["cmd"] == "start" and message["beats"] == 16.0, message)

song.is_playing, song.current_song_time = False, 24.0
surface.update_display()
message = json.loads(plugin.recvfrom(512)[0])
check("stopped -> tells the plugin to stop", message["cmd"] == "stop", message)

sys.exit(1 if failures else 0)
