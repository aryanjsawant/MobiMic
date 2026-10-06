# Python prototype

The original proof of concept that MobiMic grew out of. It is not part of the plugin and is kept for reference.

It does the same job a different way: a Python server receives the phone's audio and plays it into a virtual audio cable ([VB-CABLE](https://vb-audio.com/Cable/)), which then shows up in Windows as a microphone. That makes the phone usable as a mic in any app (calls, streaming), at the cost of installing a driver.

```
pip install -r requirements.txt
start.bat        rem low delay, for calls
record.bat       rem bigger buffer, for recording
```

`allow-phone.ps1` marks the current network as Private and opens the server's port, for machines where Windows blocks all incoming connections on public networks.
