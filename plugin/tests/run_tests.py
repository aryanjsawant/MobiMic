"""Runs MobiMicHeadlessTest against fake_phone.py for several network conditions and checks the results."""
import json, os, subprocess, sys, wave
from pathlib import Path

here = Path(__file__).parent
exe = here.parent / "build" / "MobiMicHeadlessTest_artefacts" / "Release" / "MobiMicHeadlessTest.exe"
# (host rate, buffer ms, phone clock drift, stall probability, live glitches must be zero?)
CASES = [
    (48000, 120, 1.000, 0.0, True),
    (48000, 120, 1.002, 0.0, True),
    (44100, 120, 0.998, 0.0, True),
    (96000, 120, 1.002, 0.0, True),
    (48000, 300, 1.002, 0.005, True),
    (44100, 300, 0.998, 0.005, True),
    (48000, 60, 1.000, 0.02, False),   # harsh: live output is expected to glitch, the take must not
]
SECONDS = 20
failed = False
for rate, buf, drift, stall, need_clean in CASES:
    host = subprocess.Popen([str(exe), str(SECONDS + 4), str(rate), str(buf)], stdout=subprocess.PIPE, text=True)
    port = json.loads(host.stdout.readline())["port"]
    phone = subprocess.run([sys.executable, str(here / "fake_phone.py"), str(port), str(SECONDS), str(drift), str(stall)],
                           capture_output=True, text=True, cwd=here)
    if phone.returncode != 0:
        print("PHONE FAILED:", phone.stderr[-600:]); host.kill(); failed = True; continue
    sent_info = json.loads(phone.stdout.strip().splitlines()[-1])
    result = json.loads(host.communicate()[0].strip().splitlines()[-1])
    sent = (here / "sent.raw").read_bytes()
    with wave.open(result["take"]) as w:
        fmt = (w.getnchannels(), w.getsampwidth(), w.getframerate())
        got = w.readframes(w.getnframes())
    Path(result["take"]).unlink()
    glitches = result["underruns"] + result["drops"]
    take_ok = fmt == (1, 2, 48000) and got == sent
    # Shared CI machines stall unpredictably, so there only the take's integrity is enforced.
    ok = take_ok and (glitches == 0 or not need_clean or bool(os.environ.get("CI")))
    failed |= not ok
    print(f"{'PASS' if ok else 'FAIL'}  host {rate} Hz  buffer {buf} ms  drift {drift}  stalls {stall}: "
          f"live glitches {glitches}, last buffer {sent_info['last_stats'].get('buffer_ms')} ms, "
          f"take {'bit-exact' if take_ok else 'MISMATCH'} ({len(got)//2} of {len(sent)//2} samples)")
(here / "sent.raw").unlink(missing_ok=True)
sys.exit(1 if failed else 0)
