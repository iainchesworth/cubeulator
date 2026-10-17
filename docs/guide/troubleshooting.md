# Troubleshooting

Find what the screen is saying, or what is going wrong, below. Most of it comes
down to light and to holding the cube square to the camera.

## The camera does not open

The scan screen says **Camera unavailable**, with a reason such as *no camera
device was found* or *the camera could not be started*.

- **Is another program using it?** A video call, another app or a browser tab
  can hold the camera exclusively. Close it and press **Try again**.
- **Is the system letting Cubeulator use it?**
    - *Windows:* **Settings > Privacy & security > Camera**. Turn on **Camera
      access** and **Let desktop apps access your camera**.
    - *macOS:* **System Settings > Privacy & Security > Camera**, and turn
      Cubeulator on. Then start it again.
- **Did you plug it in after starting?** Open **Settings**, and under **Camera**
  press **Look again**, then choose the camera.
- **Several cameras?** The list in Settings shows all of them, including
  virtual ones that other software has installed. Pick the one that is the
  webcam.
- **Linux:** the camera is read through Video4Linux. Check that
  `ls -l /dev/video*` shows a device you can open. If only the `video` group
  can, add yourself (`sudo usermod -aG video $USER`) and log in again.
- **In a browser:** allow the camera when the browser asks, and check the
  address bar's site settings if you said no. The page has to be served over
  `https://`; see [Try it in your browser](../web.md).

Once the cause is sorted, **Try again** on the scan screen asks for the camera
again; you do not have to go back to the start screen.

## It will not read a side

If no dots appear on the cube's stickers, the camera cannot find nine stickers
on a side. Check, in this order:

1. **Square on.** Hold the side flat towards the lens, not tilted. A side seen
   at an angle is not read, by design. (A cube held on a corner, so that two or
   three sides show at once, is read differently: see
   [Scanning a cube](scan.md#holding-a-corner-to-the-camera).)
2. **Light.** Even light on the side, with no bright window or lamp behind the
   cube, and no hard reflection on the stickers. Move the cube a little.
3. **The whole side in view**, close enough that the stickers are clear, and
   nothing across them.
4. **The cube itself.** It reads a cube with dark plastic between the
   stickers. A cube with white or light plastic, or with flush tiles and no
   visible gap, is not read yet.

If dots appear but the chip does not fill, it is moving: hold the cube still
for a moment.

## "…do not make a valid cube yet"

*These six sides do not make a valid cube yet. Show each side again, square to
the camera.*

Cubeulator checks that the six sides it read make a cube that can exist, and
that is also how it tells red from orange. This message means they do not, and
it is almost always one sticker read as the wrong colour:

- **Show each side again**, square to the camera, and let each chip refill.
- **Improve the light**, and keep it steady: a lamp that flickers or daylight
  that changes between sides is enough to confuse red and orange.
- **Start over** if it keeps saying so.
- **A cube that is not a real position cannot be solved.** If a cube has been
  taken apart and put back together wrongly, or a sticker has been moved or
  swapped, there is no solution, and the scan will never settle however well
  it reads.

## When the camera misreads a colour

Press **Check the colours** on the scan screen once six sides are in. It shows
what the camera read, one side at a time, as you would see it on your cube, with
the sides named by their middle colour.

- **A "?" on a sticker** is one the camera was not sure of. Look at that sticker
  on your cube first. Painting it, with the colour you can see, clears the mark.
- **Pick a colour, then tap or drag over the stickers** that have it. Under the
  side: **Undo** and **Redo**, and **Back to the scan**, which puts everything
  back as the camera read it.
- **If a whole side is wrong,** press **Describe this side** and type its colours
  as letters, left to right and a row at a time: W white, Y yellow, G green, B
  blue, O orange, R red, and ? for a sticker you cannot tell. `WWW/WRW/WWW` and
  `WWWWRWWWW` are the same side.
- **If the camera could not tell which way up the sides were**, the screen says
  so and shows each side as the camera saw it. Compare each with your cube and use
  **Turn this side** on any that is the wrong way round.
- **Cubeulator tells you when what is on the screen cannot be a real cube** (a
  colour used too many or too few times, or pieces that cannot go together) and
  which piece looks wrong. **Solve** is only available for a cube that can exist,
  so a mistake here is found before the solve rather than after it.

## "…fit together in more than one way"

*Turn the cube slowly to show the corner where Yellow, Green and Red meet.*
or *These six sides fit together in more than one way. Turn the cube slowly so
that two sides show at once.*

About one scrambled cube in eighty looks the same from six flat sides in two
different ways, and a perfect read cannot choose between them. Turn the cube
slowly, with a corner towards the camera, so that two sides are in view at the
same moment; that is what settles it.

## The screen's solution does not match my cube

- **Are you holding it as the screen says?** The line at the top, *Hold yellow
  on top, green facing you*, names the centres. Every move is relative to that
  hold.
- **Did a move go the wrong way?** *Previous* takes the last move back on the
  screen; undo it on your cube too. If the camera noticed, the screen says
  *That was not the move*, and names the side.
- **Did you turn the cube over, or make a few turns at once?** Press **Out of
  step? Rescan**. It reads the cube as it now is and solves it from there.
  Anything already done stays done.

## The camera keeps saying "Not the side that was expected"

The side facing the camera is not what the solution has there. Show another
side, or correct the last move. If the last move went wrong and cannot be put
right, **Rescan the cube and carry on from there**.

## The camera on the solve screen does not step on by itself

It can only see a move that turns a side it can see. A move on the side
*facing you* turns out of its view, so for those it says *press Next after this
move*. The buttons and the keys always step.

## Other things

**The words are in English.** Not every language is translated yet, and the
scan screen's guidance and most of the words round the solution are English in
every language. See [Settings and shortcuts](settings.md#language).

**Japanese or Chinese shows boxes.** The system has no font for those
characters. On Linux install Noto Sans CJK (`fonts-noto-cjk`); the browser
version carries its own.

**The cube is not drawn, or the window is slow.** In **Settings > Rendering**
choose another graphics system and restart Cubeulator.

**Some buttons are cut off.** The window can be as small as 360 by 560;
below 720 pixels wide it switches to the narrow layout, which scrolls. Make
the window larger, or press <kbd>F11</kbd> for full screen.

**Windows says "Windows protected your PC", or macOS will not open it.** See
[Installing](install.md#first-run).

## Still stuck

[Open an issue](https://github.com/iainchesworthlabs/cubeulator/issues) with:

- the version (**Settings > About Cubeulator**, or the application menu on a
  Mac) and your system;
- the camera, and the kind of light the cube was in;
- what the screen said, word for word;
- what the cube looks like (its body colour and sticker style).

Please do not attach a picture that shows a person or the room behind them.
