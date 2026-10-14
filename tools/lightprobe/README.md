# lightprobe

Makes `src/app/qt/lightprobe/sky.ktx`, the cube's light probe.

## What the file is

`Cube3DView.qml` lights the cube from a sky: an image-based light, which Qt
Quick 3D wants as a *prefiltered* cube map, one mip level for each roughness.
Given a plain image it makes that itself, on the GPU, on the first frame of
every `View3D` - 512 x 512 faces, six levels, 1024 samples a texel, whatever
the size of the image (`QSSGBufferManager::createEnvironmentMap`: the face is
`max(512, height / 2)`, so a smaller sky does not make it cheaper). Where
there is a GPU that is not noticed; where there is not (a virtual machine, a
remote desktop, a browser without WebGL acceleration, a weak phone) it is
seconds of CPU every time a screen with the cube comes up - see
[docs/performance.md](../../docs/performance.md#the-light-probe).

Given a KTX file that carries the key `QT_IBL_BAKER_VERSION`, Qt skips all of
that and uses the cube map as it is. `sky.ktx` is that file, made from the sky
the cube used to make at run time: Qt's `ProceduralSkyTextureData` with its
defaults.

## Making it

`bake_probe` needs Qt's *private* Quick 3D modules and the IBL baker, which the
application does not link, so it is a project of its own and not part of the
build:

```sh
cmake -S tools/lightprobe -B build/lightprobe -DCMAKE_PREFIX_PATH=<Qt prefix>
cmake --build build/lightprobe
build/lightprobe/bake_probe src/app/qt/lightprobe/sky.ktx 128
```

It needs a Qt with `Quick3DIblBaker` (the Windows, macOS and Linux desktop
kits have it) and a way to draw: the baker renders with Qt's RHI. On a
machine without a GPU, `QT_QPA_PLATFORM=offscreen` and Mesa work.

The second argument is the number of texels across a face of the first mip
level, 16 to 512, a power of two (the default is 128). What it does is:

1. generates the sky and writes it as an OpenEXR image, rows in reverse order;
2. runs `QSSGIblBaker` over it - the code the run-time path is - which gives
   512 x 512 faces and six levels in RGBA16F;
3. averages every level down to the asked-for size. Each level keeps its
   roughness; only the number of texels across drops.

**Why the rows are reversed.** Qt reads an image file with its first row at
the bottom, and the procedural sky's texture data has it at the top. The first
bake, without the reversal, came out upside down: the underside of the cube lit
by the sky. The unit test `the light probe is a prefiltered cube map, sky above
and ground below` (tests/unit/cube_view_test.cpp) reads the face data and fails
that.

## Why 128

The tour pictures (`system.tour.wide`, three shader styles, light and dark) of
the cube with each size of the probe, against the same pictures with the probe
Qt made at run time:

| Face | File (raw) | Largest difference, 0-255 | Mean difference |
|---|---|---|---|
| 512 | 16.8 MB | 1 | 0.000 |
| 256 | 4.2 MB | 1 | 0.01 |
| **128** | **1.0 MB** | **2** | **0.02 to 0.04** |
| 64 | 262 KB | 7 | 0.03 to 0.09 |
| 32 | 66 KB | 19 | 0.1 to 0.25 |
| 16 | 16 KB | 55 | 0.6 to 1.9 |

The pictures compared are the ones that hold still (the settings preview, the
launch screen in the dark theme, the solution's start; the same picture taken
twice differs by at most 1). 512 and 256 differ from the run-time probe by one
level of one channel at most; 128 by two, in a few pixels, which is below what an
eye sees; from 64 down it begins to.

Re-measure that after changing the sky, the sizes, or Qt: the baker is Qt's, and
what it makes is Qt's to change.
