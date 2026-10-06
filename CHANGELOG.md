# Changelog

## 1.0.0

A new look, and the standalone app becomes part of the product.

- **New design** across the plugin, the app, the phone page and the website: silver surfaces, white cards, one rose-pink accent.
- **Live waveform** of the phone's signal in the plugin, the app and on the phone. Bars turn red when the signal clips.
- **Settings panel.** Gain, Buffer, Offset, Hear the phone live and Capture while the DAW records moved out of the main window into settings.
- **Standalone app** is installed with a Start menu shortcut. It has its own wording (Record, not Capture), nothing about DAWs, and no longer opens or warns about the computer's own microphone.
- **Icon** for the app, the installer and the website.
- **Website** with download, guide, settings reference and FAQ.
- The QR code tucks away once the phone is connected; "Show QR code" brings it back.
- New test that drives the real phone page in headless Chrome.

## 0.3.0

- **Ableton Live helper**: press record and the take lands on the track where recording started.
- Takes are cut exactly to the recording and shifted by a new **Offset** setting to cancel the phone's delay.
- Clips already on the track now pass through the plugin.
- **Gain** is applied on the phone before the audio is sent, and reaches +36 dB.
- New **Hear the phone live** switch.
- A USB-tethered phone's network is preferred when the computer is on several.
- `log.txt` in `%APPDATA%\MobiMic` records what happened to each recording.

## 0.1.0

- First version: VST3 plugin with an embedded HTTPS/WebSocket server, drift-correcting jitter buffer, gap-free take capture, installer.
