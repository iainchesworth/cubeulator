# Installing

Cubeulator comes as an installer for each desktop system. There is nothing to
register for and no account.

Download the one for your system from the
[Releases page](https://github.com/iainchesworthlabs/cubeulator/releases) (while
Cubeulator is in beta every release is marked *pre-release*, and the newest is
the first one listed). `<version>` below is the release's number, for example
`0.1.0-beta.1`.

| System | File | Needs |
| --- | --- | --- |
| Windows | `Cubeulator-<version>-windows-x64.exe` | Windows 10 or later, 64-bit |
| macOS | `Cubeulator-<version>-macos-arm64.dmg` | A Mac with Apple silicon (M1 or later) |
| Debian, Ubuntu | `Cubeulator-<version>-linux-x64.deb` | 64-bit x86, X11 or XWayland |
| Fedora | `Cubeulator-<version>-linux-x64.rpm` | 64-bit x86, X11 or XWayland |

Every release also carries a `SHA512SUMS` file and GPG signatures. You do not
need them to use the app; [Verifying build authenticity](../SECURITY.md#verifying-build-authenticity)
shows how to check that a download is the one this project built.

## First run

The installers are not signed with a certificate from Microsoft or Apple yet,
so each system asks before it runs the app the first time. This is expected.

=== "Windows"

    1. Run the installer. It asks for administrator permission, because it
       installs for every user, in `C:\Program Files\Cubeulator`, with one
       entry in the Start menu.
    2. If Windows says **Windows protected your PC**, choose *More info*, then
       *Run anyway*.
    3. Start **Cubeulator** from the Start menu.

    The first time it opens the camera, Windows may ask whether the app can use
    it. If scanning says *Camera unavailable*, check *Settings > Privacy &
    security > Camera*: **Camera access** and **Let desktop apps access your
    camera** must both be on.

    To remove it, use *Settings > Apps > Installed apps*.

=== "macOS"

    1. Open the disk image and drag **Cubeulator** onto **Applications**.
    2. The app is not notarised by Apple. The first time, right-click it in
       Applications and choose *Open*, then *Open* again. (Or try to open it,
       then choose *Open Anyway* in *System Settings > Privacy & Security*.)
    3. When it first opens the camera, macOS asks. Choose *OK*. If you chose
       *Don't Allow*, turn Cubeulator on under *System Settings > Privacy &
       Security > Camera* and start it again.

    To remove it, drag it from Applications to the Bin.

=== "Debian, Ubuntu"

    ```sh
    sudo apt install ./Cubeulator-<version>-linux-x64.deb
    ```

    Start **Cubeulator** from the application menu, or run `cubeulator`. The
    camera is read through Video4Linux, so a webcam that appears as
    `/dev/video0` works; your user needs to be allowed to open it (on most
    desktops that is automatic).

    To remove it: `sudo apt remove cubeulator`.

=== "Fedora"

    ```sh
    sudo dnf install ./Cubeulator-<version>-linux-x64.rpm
    ```

    Start **Cubeulator** from the application menu, or run `cubeulator`. The
    camera is read through Video4Linux, so a webcam that appears as
    `/dev/video0` works.

    To remove it: `sudo dnf remove $(rpm -qa | grep -i '^cubeulator')`.

The Linux packages draw with X11; on a Wayland desktop that means XWayland.
Japanese and Chinese need a CJK font on the system, and the packages recommend
Noto Sans CJK (`fonts-noto-cjk` on Debian and Ubuntu, `google-noto-sans-cjk-fonts`
on Fedora).

## In a browser

No install: [open Cubeulator in your browser](../web.md). It is the same app,
running on your device, and needs a current browser and a secure page for the
camera. Each release also carries the same site as `Cubeulator-<version>-web.zip`,
if you would rather host it yourself ([how](../web.md#hosting-it-yourself)).

## Updating

Cubeulator never checks for updates or contacts a server. To update, download
the newer package and install it over the old one; your settings are kept.

## Next

[Scan a cube](scan.md).
