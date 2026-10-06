"""Regenerates the README images in docs/images.

Runs MobiMicScreenshots (the plugin window, rendered off-screen) and opens the real
phone page in headless Chrome with a fake microphone, so the two talk to each other
exactly as a phone and the plugin would. Nothing appears on screen and no sound plays.

    python plugin/tools/make_screenshots.py
"""
import asyncio, base64, json, shutil, subprocess, tempfile, time, urllib.request
from pathlib import Path

import websockets

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "docs" / "images"
TOOL = ROOT / "plugin" / "build" / "MobiMicScreenshots_artefacts" / "Release" / "MobiMicScreenshots.exe"
CHROME = next(p for p in (Path(r"C:\Program Files\Google\Chrome\Application\chrome.exe"),
                          Path(r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe")) if p.exists())
DEBUG_PORT = 9333


async def drive_phone(port):
    for _ in range(50):
        try:
            pages = json.load(urllib.request.urlopen(f"http://127.0.0.1:{DEBUG_PORT}/json"))
            target = next(p for p in pages if p["type"] == "page")
            break
        except Exception:
            time.sleep(0.2)
    async with websockets.connect(target["webSocketDebuggerUrl"], max_size=None) as cdp:
        ids = iter(range(1, 1000))

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
        await shot("phone-start.png")
        await call("Runtime.evaluate", expression="document.getElementById('mic').click()", userGesture=True)
        await asyncio.sleep(4.5)   # by now the plugin tool is capturing a take
        await shot("phone-live.png")
        status = await call("Runtime.evaluate", expression="document.getElementById('status').textContent + ' | ' + document.getElementById('stats').textContent")
        print("phone page says:", status["result"]["value"].encode("ascii", "replace").decode())
        await asyncio.sleep(3)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    tool = subprocess.Popen([str(TOOL), str(OUT)], stdout=subprocess.PIPE, text=True)
    port = json.loads(tool.stdout.readline())["port"]
    profile = tempfile.mkdtemp(prefix="mobimic-chrome-")
    chrome = subprocess.Popen([str(CHROME), "--headless=new", f"--remote-debugging-port={DEBUG_PORT}",
                               f"--user-data-dir={profile}", "--ignore-certificate-errors",
                               "--use-fake-ui-for-media-stream", "--use-fake-device-for-media-stream",
                               "--hide-scrollbars", "--mute-audio", "about:blank"],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        asyncio.run(drive_phone(port))
        tool.wait(timeout=60)
    finally:
        chrome.terminate()
        tool.poll() is None and tool.kill()
        time.sleep(1)
        shutil.rmtree(profile, ignore_errors=True)
    print("wrote:", ", ".join(sorted(p.name for p in OUT.glob("*.png"))))


if __name__ == "__main__":
    main()
