## Installing

**Windows.** Run `@WINDOWS@`. The installer is not code-signed yet, so Windows SmartScreen may say "Windows protected your PC": choose *More info*, then *Run anyway*.

**macOS.** Open `@MACOS@` and drag Cubeulator to Applications. The app carries an ad-hoc signature and is not notarised, so the first time, right-click it and choose *Open*, or use *Open Anyway* in System Settings > Privacy & Security.

**Linux.** `sudo apt install ./@DEB@` (Debian, Ubuntu) or `sudo dnf install ./@RPM@` (Fedora). The Linux packages need X11 or XWayland.

**Web.** Nothing to install: Cubeulator runs in a current Chrome, Edge, Firefox or Safari, at [the project's site](https://iainchesworthlabs.github.io/cubeulator/app/). `@WEB@` is the same app as static files to host yourself: unzip it behind any https server (`HOSTING.txt` inside says what the host has to do), or `python3 -m http.server` in the directory to try it on `localhost`. See [Try it in your browser](docs/web.md).
