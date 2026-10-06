"""Plays the phone for MobiMicHeadlessTest: streams a tone over wss with clock drift and Wi-Fi stalls.

    python fake_phone.py <port> <seconds> <drift> <stall_probability>

Like a real phone, audio produced during a stall is queued and sent afterwards (TCP loses nothing).
Writes what it sent to sent.raw so the captured take can be compared bit-for-bit.
"""
import asyncio, json, random, ssl, sys, time, urllib.request
import numpy as np, websockets

port, seconds, drift, stall = int(sys.argv[1]), float(sys.argv[2]), float(sys.argv[3]), float(sys.argv[4])
ctx = ssl.create_default_context(); ctx.check_hostname = False; ctx.verify_mode = ssl.CERT_NONE

async def main():
    html = urllib.request.urlopen(f"https://127.0.0.1:{port}/", context=ctx).read()
    assert b"MobiMic" in html, "phone page not served"
    try:
        urllib.request.urlopen(f"https://127.0.0.1:{port}/nope", context=ctx); raise SystemExit("expected 404")
    except urllib.error.HTTPError as e:
        assert e.code == 404
    sent = bytearray()
    async with websockets.connect(f"wss://127.0.0.1:{port}/ws", ssl=ctx, ping_interval=None) as ws:
        ph = 0; t0 = time.perf_counter(); period = 0.01 / drift; stats = {}
        for i in range(int(seconds * 100)):
            n = np.arange(ph, ph + 480) / 48000; ph += 480
            pcm = (np.sin(2 * np.pi * 440 * n) * 8000).astype("<i2").tobytes()
            await ws.send(pcm); sent += pcm
            if i % 100 == 99:
                await ws.send("ping"); stats = json.loads(await ws.recv())
            if random.random() < stall: await asyncio.sleep(0.08)
            await asyncio.sleep(max(0, t0 + (i + 1) * period - time.perf_counter()))
    open("sent.raw", "wb").write(sent)
    print(json.dumps({"sent_samples": len(sent) // 2, "last_stats": stats}))

asyncio.run(main())
