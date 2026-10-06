# Contributing to MobiMic

Thanks for wanting to help. Bug reports, phone and DAW compatibility notes, and pull requests are all welcome.

## Reporting a problem

Open an [issue](../../issues/new/choose) and include:

- Your Windows version, DAW and its version, and your phone and browser.
- How the phone is connected (Wi-Fi, hotspot, USB tethering).
- What you did, what you expected, and what happened.
- The file `%APPDATA%\MobiMic\log.txt` if the problem is about recording.

Compatibility reports are especially useful: MobiMic is tested with Chrome on Android and Ableton Live 12. If it works (or doesn't) with another phone, browser or DAW, please say so.

## Building

See [Build from source](README.md#build-from-source). `build.bat` builds everything and runs the tests.

## Making a change

1. Fork the repository and create a branch.
2. Make the change. Match the style of the code around it.
3. Run the tests:
   ```bat
   python plugin\tests\run_tests.py
   python plugin\tests\test_ableton_helper.py
   python plugin\tests\test_host_record.py
   python plugin\tests\test_phone_page.py
   ```
4. If you changed how anything looks, regenerate the images:
   ```bat
   python plugin\tools\make_screenshots.py
   python plugin\tools\make_hero.py
   ```
5. Open a pull request describing what changed and how you tested it.

## Where things are

The table in the [README](README.md#under-the-hood) maps the folders. The look (colours, fonts, shapes) is defined in three places that should stay in step: `plugin/src/Theme.h`, `plugin/web/index.html` and `docs/index.html`.

## Licence

By contributing you agree that your contribution is licensed under the [AGPL-3.0](LICENSE), the same licence as the project.
