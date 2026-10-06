"""Regenerates the product images in docs/images.

Runs MobiMicScreenshots (the plugin window, rendered off-screen) and opens the real
phone page in headless Chrome with a recorded-voice-like signal as its microphone, so
the two talk to each other exactly as a phone and the plugin would. It does this twice:
once for the plugin's layout and once for the standalone app's.

Nothing appears on screen and no sound plays.

    python plugin/tools/make_screenshots.py
"""
import asyncio, base64, json, os, shutil, subprocess, tempfile, threading, time, urllib.request, wave
from pathlib import Path

import numpy as np
import websockets

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "docs" / "images"
TOOL = ROOT / "plugin" / "build" / "MobiMicScreenshots_artefacts" / "Release" / "MobiMicScreenshots.exe"
BROWSER = next(p for p in (Path(r"C:\Program Files\Google\Chrome\Application\chrome.exe"),
                           Path(r"C:\Program Files (x86)\Google\Chrome\Application\chrome.exe"),
                           Path(r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe")) if p.exists())
DEBUG_PORT = 9333


def write_demo_voice(path, seconds=20, rate=48000, seed=7):
    """Something that moves like a voice (phrases, syllables, pauses) so the waveforms look real."""
    rng = np.random.default_rng(seed)
    t = np.arange(seconds * rate) / rate
    envelope = np.zeros_like(t)
    position = 0.2
    while position < seconds:
        phrase_end = position + rng.uniform(1.2, 2.6)
        while position < phrase_end:
            length = rng.uniform(0.09, 0.26)
            strength = rng.uniform(0.35, 1.0)
            inside = (t >= position) & (t < position + length)
            envelope[inside] = strength * np.sin(np.pi * (t[inside] - position) / length) ** 0.7
            position += length + rng.uniform(0.02, 0.08)
        position += rng.uniform(0.25, 0.6)
    pitch = 170 + 25 * np.sin(2 * np.pi * 0.31 * t) + 8 * np.sin(2 * np.pi * 5.1 * t)
    phase = 2 * np.pi * np.cumsum(pitch) / rate
    voice = sum(np.sin(k * phase) / k ** 1.3 for k in range(1, 9)) + 0.15 * rng.standard_normal(len(t))
    signal = 0.62 * envelope * voice / np.abs(voice).max()
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(rate)
        w.writeframes((signal * 32767).astype("<i2").tobytes())


async def drive_phone(port, shots, capturing):
    """Opens the phone page, taps Start, and photographs it. `shots` maps moment -> file name."""
    for _ in range(50):
        try:
            pages = json.load(urllib.request.urlopen(f"http://127.0.0.1:{DEBUG_PORT}/json"))
            target = next(p for p in pages if p["type"] == "page")
            break
        except Exception:
            time.sleep(0.2)

    async with websockets.connect(target["webSocketDebuggerUrl"], max_size=None) as cdp:
        ids = iter(range(1, 10000))

        async def call(method, **params):
            n = next(ids)
            await cdp.send(json.dumps({"id": n, "method": method, "params": params}))
            while True:
                reply = json.loads(await cdp.recv())
                if reply.get("id") == n:
                    return reply.get("result", {})

        async def shot(name):
            data = (await call("Page.captureScreenshot", format="png"))["data"]
            (OUT / name).write_bytes(base64.b64decode(data))

        await call("Emulation.setDeviceMetricsOverride", width=390, height=780, deviceScaleFactor=2, mobile=True)
        await call("Page.navigate", url=f"https://127.0.0.1:{port}/")
        await asyncio.sleep(1.5)
        if "start" in shots:
            await shot(shots["start"])
        await call("Runtime.evaluate", expression="document.getElementById('mic').click()", userGesture=True)

        # Photograph the page while the plugin is capturing a take, then stay connected until it is done.
        for _ in range(200):
            if capturing.is_set():
                break
            await asyncio.sleep(0.1)
        await asyncio.sleep(1.6)
        if "live" in shots:
            await shot(shots["live"])
        await asyncio.sleep(6)


def run(prefix, standalone, voice, phone_shots):
    handoff = Path(tempfile.mkdtemp(prefix="mobimic-shots-"))
    env = dict(os.environ, MOBIMIC_HANDOFF=str(handoff))
    if standalone:
        env["MOBIMIC_STANDALONE"] = "1"

    # Play the Ableton helper for the plugin shots: alive, and reporting where the take went.
    result, stop = {"text": ""}, threading.Event()

    def helper():
        while not stop.is_set():
            (handoff / "helper.json").write_text(json.dumps({"time": int(time.time()), "result": result["text"]}))
            time.sleep(0.4)

    if not standalone:
        threading.Thread(target=helper, daemon=True).start()
        time.sleep(0.6)

    tool = subprocess.Popen([str(TOOL), str(OUT), prefix], stdout=subprocess.PIPE, text=True, env=env)
    port, capturing = None, threading.Event()
    port_ready = threading.Event()

    def follow():
        nonlocal port
        for line in tool.stdout:
            message = json.loads(line)
            if "port" in message:
                port = message["port"]; port_ready.set()
            elif message.get("shot", "").endswith("-connected"):
                capturing.set()
            elif message.get("shot", "").endswith("-capturing"):
                result["text"] = "Take placed on 'Vocals'"

    threading.Thread(target=follow, daemon=True).start()
    port_ready.wait(30)

    profile = tempfile.mkdtemp(prefix="mobimic-chrome-")
    chrome = subprocess.Popen([str(BROWSER), "--headless=new", f"--remote-debugging-port={DEBUG_PORT}",
                               f"--user-data-dir={profile}", "--ignore-certificate-errors",
                               "--use-fake-ui-for-media-stream", "--use-fake-device-for-media-stream",
                               f"--use-file-for-fake-audio-capture={voice}",
                               "--hide-scrollbars", "--mute-audio", "about:blank"],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        asyncio.run(drive_phone(port, phone_shots, capturing))
        tool.wait(timeout=60)
    finally:
        stop.set()
        chrome.terminate()
        if tool.poll() is None:
            tool.kill()
        time.sleep(1)
        shutil.rmtree(profile, ignore_errors=True)
        shutil.rmtree(handoff, ignore_errors=True)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    voice = Path(tempfile.gettempdir()) / "mobimic-demo-voice.wav"
    write_demo_voice(voice)
    run("plugin", False, voice, {"start": "phone-start.png", "live": "phone-live.png"})
    run("app", True, voice, {})
    voice.unlink(missing_ok=True)
    print("wrote:", ", ".join(sorted(p.name for p in OUT.glob("*.png"))))


if __name__ == "__main__":
    main()
