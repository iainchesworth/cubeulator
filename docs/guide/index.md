# User guide

Cubeulator solves a Rubik's Cube from a picture of it. Hold each side of a
scrambled cube up to the camera, and it reads the whole cube, works out a
solution, and shows you the turns one at a time on a 3D cube.

<figure markdown="span">
  ![The start screen: a headline, a Scan a cube button, and a 3D cube.](img/launch-wide-light.webp#only-light){ loading=lazy }
  ![The start screen: a headline, a Scan a cube button, and a 3D cube.](img/launch-wide-dark.webp#only-dark){ loading=lazy }
  <figcaption>The start screen, on a desktop.</figcaption>
</figure>

## What you need

- **A cube.** The camera reads a 3x3, and it reads best on a cube with dark (black) plastic between the
  stickers, which is most of them. A cube with white or light plastic, or no
  stickers at all, is not read yet.
- **A camera.** A laptop's webcam or a USB one is enough; in a browser on a
  phone, the rear camera.
- **Steady, even light.** A lamp or daylight on the side you are holding, not
  a window behind it. Where the camera allows it, Cubeulator fixes the camera's
  exposure and colour balance for the scan; light that changes while you scan
  still makes red and orange hard to tell apart.
- **A computer to run it on:** Windows 10 or later (64-bit), macOS on a Mac
  with Apple silicon, or Linux (Debian, Ubuntu, Fedora) - or
  [a browser](../web.md), with nothing to install.

## The short version

1. **[Install it](install.md)**, or [open it in your browser](../web.md).
2. **Choose Scan a cube** and hold each of the cube's six sides square to the
   camera, one after another, in any order and either way up. Each side fills a
   chip along the bottom of the screen. See [Scanning a cube](scan.md).
3. **Follow the 3D cube.** The layer to turn nudges the way it has to go; make
   that turn on your cube, and press *Next move* (or let the camera see it and
   it steps on by itself). See [Following the solution](solve.md).
4. **Finish.** The screen says *Solved* with the number of turns and the time,
   and offers another scan, or a review of the whole solution. The solve is
   kept in [your history](history.md).

## In this guide

| | |
| --- | --- |
| [Installing](install.md) | The package for each system, the first-run prompts, and uninstalling |
| [Scanning a cube](scan.md) | How to hold the cube, what the screen is telling you, and what to do when it asks again |
| [Following the solution](solve.md) | The solve screen, the three methods, and how the camera follows your turns |
| [Another size of cube](other-sizes.md) | A 2x2, 4x4 or 5x5: type its scramble or its colours, and follow it |
| [Past solves](history.md) | The list of cubes you have solved, and carrying on one you left |
| [Settings and shortcuts](settings.md) | Camera, method, appearance, language, and the keyboard |
| [Privacy](privacy.md) | What the camera picture is used for, and what is kept |
| [Troubleshooting](troubleshooting.md) | When it will not start, will not read a side, or will not agree with your cube |

## What this release is

!!! warning "A beta"
    This is a pre-release, and the guide says plainly what is not finished.

    - **The scan is proven on a few cubes and rooms, not many.** It has been
      checked on recorded sessions of one cube in tungsten light and in backlit
      daylight, and on a webcam by hand. Other cameras, rooms and cubes are what
      the beta is for.
    - **A sticker cannot be corrected by hand yet.** If one is read wrongly, the
      scan will say the six sides do not make a cube, and you start the scan
      again.
    - **Some of the wording is English whatever language you choose:** the scan
      screen's guidance and most of the words around the solution. See
      [Settings and shortcuts](settings.md#language).
    - **The installers are not code-signed**, so Windows and macOS ask before
      they run it. [Installing](install.md) says what to expect.

Something wrong, or a cube it will not read? Please
[open an issue](https://github.com/iainchesworthlabs/cubeulator/issues): what
the cube looks like, what light it was in, and what the screen said.
