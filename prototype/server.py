"""audiomic - use your phone as a wireless microphone for this PC.

Run this, open the printed URL on the phone, tap Start. Audio is played into a
virtual cable (VB-CABLE), whose other end shows up in Windows as a microphone.
"""

import argparse
import asyncio
import datetime
import ipaddress
import json
import socket
import ssl
import subprocess
import sys
import threading
from pathlib import Path

import numpy as np
import sounddevice as sd
from aiohttp import WSMsgType, web

ROOT = Path(__file__).parent
CERT_DIR = ROOT / ".cert"
SAMPLE_RATE = 48000
CABLE_HINTS = ("cable input", "vb-audio", "voicemeeter input")


class JitterBuffer:
    """Thread-safe mono float32 FIFO between the network and the audio callback."""

    def __init__(self, target_ms, max_ms):
        self.target = SAMPLE_RATE * target_ms // 1000
        self.max = SAMPLE_RATE * max_ms // 1000
        self.buf = np.zeros(self.max * 2, dtype=np.float32)
        self.size = 0
        self.frac = 0.0
        self.avg_size = float(self.target)
        self.priming = True
        self.lock = threading.Lock()
        self.underruns = 0
        self.drops = 0

    def push(self, samples):
        with self.lock:
            n = len(samples)
            if self.size + n > self.max:
                # Fell behind (clock drift / network burst): skip ahead to target.
                keep = max(self.target - n, 0)
                self.buf[:keep] = self.buf[self.size - keep:self.size]
                self.size = keep
                self.drops += 1
            self.buf[self.size:self.size + n] = samples
            self.size += n
            if self.size >= self.target:
                self.priming = False

    def pull(self, n):
        with self.lock:
            if self.priming:
                return None
            # The phone's and the PC's audio clocks never match exactly. Instead of
            # skipping or stalling when the buffer drifts, play very slightly faster
            # or slower (at most 0.5%, inaudible) to steer it back to the target.
            self.avg_size += 0.01 * (self.size - self.avg_size)
            error = (self.avg_size - self.target) / self.target
            ratio = 1.0 + float(np.clip(error * 0.01, -0.005, 0.005))
            pos = self.frac + np.arange(n) * ratio
            last = int(pos[-1]) + 1
            if last >= self.size:
                self.underruns += 1
                self.priming = True
                return None
            i = pos.astype(np.int64)
            t = (pos - i).astype(np.float32)
            out = self.buf[i] * (1 - t) + self.buf[i + 1] * t
            end = self.frac + n * ratio
            used = int(end)
            self.frac = end - used
            self.buf[:self.size - used] = self.buf[used:self.size]
            self.size -= used
            return out

    def reset(self):
        with self.lock:
            self.size = 0
            self.frac = 0.0
            self.avg_size = float(self.target)
            self.priming = True


def find_output_device(name):
    """Pick the WASAPI output to play into. Returns (index, is_virtual_cable)."""
    wasapi = next(i for i, h in enumerate(sd.query_hostapis()) if "WASAPI" in h["name"])
    outputs = [(i, d) for i, d in enumerate(sd.query_devices())
               if d["hostapi"] == wasapi and d["max_output_channels"] > 0]
    if name:
        if name.isdigit():
            return int(name), True
        for i, d in outputs:
            if name.lower() in d["name"].lower():
                return i, True
        sys.exit(f"No output device matching '{name}'. Run with --list to see devices.")
    for i, d in outputs:
        if any(h in d["name"].lower() for h in CABLE_HINTS):
            return i, True
    return sd.query_hostapis(wasapi)["default_output_device"], False


def local_ips():
    ips = set()
    try:
        for info in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
            ips.add(info[4][0])
    except socket.gaierror:
        pass
    # The address the default route uses comes first; it's the one the phone can reach.
    primary = None
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
            s.connect(("10.255.255.255", 1))
            primary = s.getsockname()[0]
    except OSError:
        pass
    ips.discard("127.0.0.1")
    ordered = sorted(ips)
    if primary in ips:
        ordered.remove(primary)
        ordered.insert(0, primary)
    return ordered


def ensure_cert(ips):
    """Self-signed cert covering the current IPs (browsers need HTTPS for mic access)."""
    from cryptography import x509
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import ec
    from cryptography.x509.oid import NameOID

    CERT_DIR.mkdir(exist_ok=True)
    cert_file, key_file, ips_file = CERT_DIR / "cert.pem", CERT_DIR / "key.pem", CERT_DIR / "ips.json"
    if cert_file.exists() and key_file.exists() and ips_file.exists():
        if set(ips) <= set(json.loads(ips_file.read_text())):
            return cert_file, key_file

    key = ec.generate_private_key(ec.SECP256R1())
    name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "audiomic")])
    sans = [x509.DNSName("localhost"), x509.IPAddress(ipaddress.ip_address("127.0.0.1"))]
    sans += [x509.IPAddress(ipaddress.ip_address(ip)) for ip in ips]
    now = datetime.datetime.now(datetime.timezone.utc)
    cert = (x509.CertificateBuilder()
            .subject_name(name).issuer_name(name)
            .public_key(key.public_key())
            .serial_number(x509.random_serial_number())
            .not_valid_before(now - datetime.timedelta(days=1))
            .not_valid_after(now + datetime.timedelta(days=365))
            .add_extension(x509.SubjectAlternativeName(sans), critical=False)
            .sign(key, hashes.SHA256()))
    cert_file.write_bytes(cert.public_bytes(serialization.Encoding.PEM))
    key_file.write_bytes(key.private_bytes(serialization.Encoding.PEM,
                                           serialization.PrivateFormat.PKCS8,
                                           serialization.NoEncryption()))
    ips_file.write_text(json.dumps(ips))
    return cert_file, key_file


def print_qr(url):
    try:
        import qrcode
    except ImportError:
        return
    qr = qrcode.QRCode(border=1)
    qr.add_data(url)
    try:
        qr.print_ascii(invert=True)
    except UnicodeEncodeError:
        pass  # output redirected to something that can't show block characters


def adb_reverse(port):
    try:
        subprocess.run(["adb", "reverse", f"tcp:{port}", f"tcp:{port}"],
                       check=True, capture_output=True, timeout=10)
        return True
    except (OSError, subprocess.SubprocessError):
        return False


async def index(request):
    return web.FileResponse(ROOT / "static" / "index.html",
                            headers={"Cache-Control": "no-store"})


async def ws_handler(request):
    app = request.app
    ws = web.WebSocketResponse(heartbeat=5, max_msg_size=1 << 20)
    await ws.prepare(request)

    # One phone at a time: a new connection replaces the old one.
    old = app["state"]["client"]
    if old is not None and not old.closed:
        await old.close()
    app["state"]["client"] = ws
    jitter = app["jitter"]
    jitter.reset()
    print(f"[+] phone connected: {request.remote}")

    try:
        async for msg in ws:
            if msg.type == WSMsgType.BINARY:
                pcm = np.frombuffer(msg.data, dtype="<i2").astype(np.float32) / 32768.0
                jitter.push(pcm)
            elif msg.type == WSMsgType.TEXT:
                if msg.data == "ping":
                    await ws.send_str(json.dumps({
                        "buffer_ms": round(jitter.size * 1000 / SAMPLE_RATE),
                        "underruns": jitter.underruns,
                        "drops": jitter.drops,
                    }))
    finally:
        if app["state"]["client"] is ws:
            app["state"]["client"] = None
            jitter.reset()
        print(f"[-] phone disconnected: {request.remote}")
    return ws


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--list", action="store_true", help="list output devices and exit")
    p.add_argument("--device", help="output device name (substring) or index; default: auto-detect VB-CABLE")
    p.add_argument("--port", type=int, default=8443)
    p.add_argument("--buffer-ms", type=int, default=120, help="jitter buffer target (lower = less delay, more glitches)")
    p.add_argument("--gain", type=float, default=1.0, help="volume multiplier")
    args = p.parse_args()

    if args.list:
        print(sd.query_devices())
        return

    device, is_cable = find_output_device(args.device)
    dev_info = sd.query_devices(device)
    channels = min(2, dev_info["max_output_channels"])
    jitter = JitterBuffer(args.buffer_ms, max(args.buffer_ms * 4, 250))

    def audio_callback(outdata, frames, time, status):
        data = jitter.pull(frames)
        if data is None:
            outdata.fill(0)
        else:
            np.clip(data * args.gain, -1.0, 1.0, out=data)
            outdata[:] = data[:, None]

    stream = sd.OutputStream(device=device, samplerate=SAMPLE_RATE, channels=channels,
                             dtype="float32", blocksize=0, latency="low",
                             callback=audio_callback,
                             extra_settings=sd.WasapiSettings(auto_convert=True))

    ips = local_ips()
    cert_file, key_file = ensure_cert(ips)
    ssl_ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ssl_ctx.load_cert_chain(cert_file, key_file)

    app = web.Application()
    app["jitter"] = jitter
    app["state"] = {"client": None}
    app.router.add_get("/", index)
    app.router.add_get("/ws", ws_handler)

    print(f"\naudiomic -> playing into: {dev_info['name']}")
    if is_cable:
        print("In your apps, pick the matching 'CABLE Output' device as the microphone.")
    else:
        print("!! No virtual cable found - phone audio will just play through your speakers.")
        print("!! Install VB-CABLE (https://vb-audio.com/Cable/), reboot, and run this again.")
    if ips:
        url = f"https://{ips[0]}:{args.port}"
        print(f"\nOn the phone (same Wi-Fi / hotspot / USB tethering) open:\n\n    {url}\n")
        print_qr(url)
        for ip in ips[1:]:
            print(f"    other address: https://{ip}:{args.port}")
        print("\nFirst time: the browser warns about the certificate -> Advanced -> Proceed.")
    if adb_reverse(args.port):
        print(f"USB cable + adb detected: https://localhost:{args.port} on the phone also works.")
    print("Ctrl+C to quit.\n")

    with stream:
        try:
            web.run_app(app, host="0.0.0.0", port=args.port, ssl_context=ssl_ctx,
                        print=None, access_log=None)
        except KeyboardInterrupt:
            pass


if __name__ == "__main__":
    if sys.platform == "win32":
        asyncio.set_event_loop_policy(asyncio.WindowsSelectorEventLoopPolicy())
    main()
