"""MobiMic helper for Ableton Live.

A plugin can't see Live's record button reliably or put clips on the timeline,
so this script does both for the MobiMic plugin:

  1. When recording starts or stops, it tells the plugin (a tiny local network
     message), so the plugin can cut the phone's audio to exactly that stretch.
  2. The plugin saves the take and leaves a note in %APPDATA%\\MobiMic\\pending;
     this script reads it and places the take on the track where recording began.

Install: copy this folder to  Documents\\Ableton\\User Library\\Remote Scripts\\MobiMic
then pick "MobiMic" as a Control Surface in Live's Preferences > Link, Tempo & MIDI.
"""

import json
import os
import socket
import time

from _Framework.ControlSurface import ControlSurface

HANDOFF = os.path.join(os.environ.get("APPDATA", os.path.expanduser("~")), "MobiMic")
PENDING = os.path.join(HANDOFF, "pending")
STATUS = os.path.join(HANDOFF, "helper.json")
PLUGIN_STATUS = os.path.join(HANDOFF, "plugin.json")
PLUGIN_NAME = "MobiMic"
MAX_AGE_SECONDS = 60  # never place a stale take left over from an earlier session


def create_instance(c_instance):
    return MobiMic(c_instance)


class MobiMic(ControlSurface):
    def __init__(self, c_instance):
        super().__init__(c_instance)
        self._ticks = 0
        self._result = ""
        self._recording = False
        self._last_time = 0.0
        self._socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

        # Listeners fire the moment the transport changes; update_display also checks as a fallback.
        song = self.song()
        self._listeners = []
        for name in ("record_mode", "is_playing", "is_counting_in"):
            add = getattr(song, "add_%s_listener" % name, None)
            if add is not None:
                add(self._check_recording)
                self._listeners.append(name)

        self.log_message("MobiMic helper loaded")
        self._write_status()

    def disconnect(self):
        song = self.song()
        for name in self._listeners:
            try:
                getattr(song, "remove_%s_listener" % name)(self._check_recording)
            except Exception:
                pass
        try:
            self._socket.close()
            os.remove(STATUS)
        except OSError:
            pass
        super().disconnect()

    def update_display(self):
        # Called by Live about ten times a second on its main thread.
        super().update_display()
        self._ticks += 1
        self._check_recording()
        if self._ticks % 2 == 0:
            self._place_pending_takes()
        if self._ticks % 10 == 0:
            self._write_status()

    # ------------------------------------------------------------------
    # Telling the plugin when recording starts and stops

    def _check_recording(self):
        song = self.song()
        recording = bool(song.record_mode and song.is_playing and not getattr(song, "is_counting_in", False))
        now = song.current_song_time

        if recording and self._recording and now < self._last_time - 0.01:
            # Loop recording jumped back to the loop start: finish this pass, begin the next.
            self._tell_plugin("stop")
            self._tell_plugin("start", now, song.tempo)
        elif recording and not self._recording:
            self._tell_plugin("start", now, song.tempo)
        elif self._recording and not recording:
            self._tell_plugin("stop")

        self._recording = recording
        self._last_time = now

    def _tell_plugin(self, command, beats=0.0, bpm=120.0):
        # "beats" is where the song is right now, which is where the plugin's take will begin.
        try:
            with open(PLUGIN_STATUS, encoding="utf-8") as f:
                plugin = json.load(f)
            if time.time() - plugin.get("time", 0) > 10:
                raise ValueError("plugin not running")
            message = json.dumps({"cmd": command, "beats": beats, "bpm": bpm})
            self._socket.sendto(message.encode("utf-8"), ("127.0.0.1", int(plugin["control_port"])))
            self.log_message("MobiMic: told plugin to %s at beat %.3f" % (command, beats))
        except (OSError, ValueError, KeyError):
            if command == "start":
                self._result = "Recording started, but the MobiMic plugin isn't running on a track"
                self.log_message("MobiMic: " + self._result)

    # ------------------------------------------------------------------
    # Placing finished takes

    def _write_status(self):
        """Lets the plugin show whether the helper is running and what it last did."""
        try:
            os.makedirs(HANDOFF, exist_ok=True)
            temp = STATUS + ".tmp"
            with open(temp, "w", encoding="utf-8") as f:
                json.dump({"time": int(time.time()), "result": self._result}, f)
            os.replace(temp, STATUS)
        except OSError:
            pass

    def _place_pending_takes(self):
        try:
            names = sorted(n for n in os.listdir(PENDING) if n.endswith(".json"))
        except OSError:
            return

        for name in names:
            path = os.path.join(PENDING, name)
            try:
                with open(path, encoding="utf-8") as f:
                    take = json.load(f)
                os.remove(path)
            except (OSError, ValueError):
                continue  # still being written, or unreadable; try again next time

            if time.time() - take.get("created", 0) > MAX_AGE_SECONDS:
                continue

            try:
                self._result = self._place(take)
            except Exception as error:  # Live raises plain RuntimeErrors with a readable message
                self._result = "Could not place the take: %s" % error

            self.log_message("MobiMic: " + self._result)
            self.show_message("MobiMic: " + self._result)
            self._write_status()

    def _place(self, take):
        track = self._target_track()
        if track is None:
            return "No audio track to put the take on. Put MobiMic on an audio track, or arm one."

        clip = track.create_audio_clip(take["file"], float(take["start_beats"]))

        # Otherwise Live may auto-warp a long take and shift its timing.
        try:
            clip.warping = False
        except Exception:
            pass

        return "Take placed on '%s'" % track.name

    def _target_track(self):
        """The audio track holding the plugin; failing that an armed audio track, then the selected one."""
        song = self.song()
        audio_tracks = [t for t in song.tracks if t.has_audio_input and not t.has_midi_input]

        with_plugin = [t for t in audio_tracks if any(d.name == PLUGIN_NAME for d in t.devices)]
        armed = [t for t in audio_tracks if t.can_be_armed and t.arm]

        for candidates in ([t for t in with_plugin if t in armed], with_plugin, armed):
            if candidates:
                return candidates[0]

        selected = song.view.selected_track
        return selected if selected in audio_tracks else None
