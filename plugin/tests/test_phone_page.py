"""Opens the real phone page in headless Chrome (with a fake microphone) against the real server.

This is the one test that exercises the page itself: Start, streaming, the stats line,
mute (which must keep sending silence so takes stay continuous) and Stop.
Nothing appears on screen and no sound plays.
"""
import asyncio, base64, json, shutil, subprocess, sys, tempfile, time, urllib.request, wave
from pathlib import Path

import numpy as np
import websockets

here = Path(__file__).parent
exe = here.parent / "build" / "MobiMicHeadlessTest_artefacts" / "Release" / "MobiMicHeadlessTest.exe"
BROWSERS = (Path(r"C:\Program Files\Google\Chrome\Application\chrome.exe"),
            Path(r"C:\Program Files (x86)\Google\Chrome\Application\chrome.exe"),
            Path(r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe"),
            Path(r"C:\Program Files\Microsoft\Edge\Application\msedge.exe"))
DEBUG_PORT = 9334
failures = 0


def check(name, ok, detail=""):
    global failures
    failures += not ok
    print(("PASS  " if ok else "FAIL  ") + name + (": " + str(detail) if detail != "" else ""))


async def drive(port):
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

        async def js(expression):
            result = await call("Runtime.evaluate", expression=expression, userGesture=True)
            return result.get("result", {}).get("value")

        async def text(element_id):
            return (await js(f"document.getElementById('{element_id}').textContent")) or ""

        await call("Emulation.setDeviceMetricsOverride", width=390, height=780, deviceScaleFactor=2, mobile=True)
        await call("Page.navigate", url=f"https://127.0.0.1:{port}/")
        await asyncio.sleep(1.5)
        check("page loads", "MobiMic" in (await js("document.title") or ""))

        await js("document.getElementById('mic').click()")
        await asyncio.sleep(4)
        status, stats = await text("status"), await text("stats")
        check("Start connects and streams", "Streaming" in status, status)
        check("stats line shows the plugin's buffer and glitch count", "buffer" in stats and "glitches" in stats,
              stats.encode("ascii", "replace").decode())

        await js("document.getElementById('mic').click()")   # mute
        await asyncio.sleep(2)
        check("tapping the button mutes", "Muted" in await text("status"))

        await js("document.getElementById('stop').click()")
        await asyncio.sleep(0.5)
        check("Stop ends the session", "Stopped" in await text("status"))


def main():
    browser = next((p for p in BROWSERS if p.exists()), None)
    if browser is None:
        print("SKIP  no Chrome or Edge found")
        return

    host = subprocess.Popen([str(exe), "13", "48000", "120"], stdout=subprocess.PIPE, text=True)
    port = json.loads(host.stdout.readline())["port"]
    profile = tempfile.mkdtemp(prefix="mobimic-chrome-")
    chrome = subprocess.Popen([str(browser), "--headless=new", f"--remote-debugging-port={DEBUG_PORT}",
                               f"--user-data-dir={profile}", "--ignore-certificate-errors",
                               "--use-fake-ui-for-media-stream", "--use-fake-device-for-media-stream",
                               "--mute-audio", "about:blank"],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        asyncio.run(drive(port))
    finally:
        chrome.terminate()
        time.sleep(1)
        shutil.rmtree(profile, ignore_errors=True)

    result = json.loads(host.communicate(timeout=60)[0].strip().splitlines()[-1])
    with wave.open(result["take"]) as w:
        audio = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2")
    Path(result["take"]).unlink()

    seconds = len(audio) / 48000
    check("the plugin received the stream", 5.0 < seconds < 8.0, f"{seconds:.2f} s")
    live, muted = audio[:int(3.0 * 48000)], audio[-int(1.5 * 48000):]
    check("live audio arrived", np.abs(live).max() > 500, int(np.abs(live).max()))
    check("mute sends silence instead of stopping the stream", np.abs(muted).max() == 0, int(np.abs(muted).max()))


main()
sys.exit(1 if failures else 0)
