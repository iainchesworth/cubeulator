#!/usr/bin/env python3
"""Runs the prototype on a phone (or emulator) over adb, size by size and renderer by
renderer, and prints what each frame cost.

    android_matrix.py --apk cube3d_prototype-signed.apk [--sizes 5 7 9] [--moves 40]

Needs adb (on PATH, or under ANDROID_SDK_ROOT/platform-tools) and one device with USB
debugging on. It installs the APK, starts the app once per configuration with the
options as an intent extra, waits for the line of JSON the app writes to the log, and
prints a table. The phone's screen must stay on and unlocked for the run: a screen
that is off does not draw frames.
"""
import argparse
import json
import os
import pathlib
import shutil
import subprocess
import sys
import time

PACKAGE = "org.qtproject.example.cube3d_prototype"
ACTIVITY = f"{PACKAGE}/org.qtproject.qt.android.bindings.QtActivity"


def find_adb():
    found = shutil.which("adb")
    if found:
        return found
    root = os.environ.get("ANDROID_SDK_ROOT") or os.environ.get("ANDROID_HOME")
    if root:
        for name in ("adb.exe", "adb"):
            candidate = pathlib.Path(root) / "platform-tools" / name
            if candidate.exists():
                return str(candidate)
    sys.exit("cannot find adb")


def run(adb, *args, check=True):
    return subprocess.run([adb, *args], capture_output=True, text=True, check=check).stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--apk", required=True, type=pathlib.Path)
    parser.add_argument("--sizes", nargs="+", type=int, default=[3, 5, 7, 9])
    parser.add_argument("--modes", nargs="+", default=["lift", "naive"])
    parser.add_argument("--moves", type=int, default=40)
    parser.add_argument("--timeout", type=int, default=120)
    parser.add_argument("--report", type=pathlib.Path, help="append each result as a line of JSON")
    args = parser.parse_args()

    adb = find_adb()
    print(run(adb, "devices").strip())
    model = run(adb, "shell", "getprop", "ro.product.model").strip()
    soc = run(adb, "shell", "getprop", "ro.soc.model").strip() or run(adb, "shell", "getprop", "ro.board.platform").strip()
    print(f"device: {model} ({soc})")
    run(adb, "install", "-r", str(args.apk))

    print(f"{'mode':<6} {'size':>4} {'ready ms':>9} {'frames':>7} {'p50':>6} {'p95':>6} {'sync mean':>10} "
          f"{'sync p95':>9} {'move ms':>8}")
    for size in args.sizes:
        for mode in args.modes:
            run(adb, "shell", "am", "force-stop", PACKAGE)
            run(adb, "logcat", "-c")
            options = f"--mode {mode} --size {size} --moves {args.moves} --solve --turn-ms 160 --idle-ms 800"
            run(adb, "shell", "am", "start", "-n", ACTIVITY, "--es", "applicationArguments", f"'{options}'")
            result = None
            deadline = time.time() + args.timeout
            while time.time() < deadline and result is None:
                time.sleep(2)
                for line in run(adb, "logcat", "-d", check=False).splitlines():
                    if "CUBE3D-RESULT" in line:
                        result = json.loads(line.split("CUBE3D-RESULT", 1)[1].strip())
                        break
            run(adb, "shell", "am", "force-stop", PACKAGE)
            if result is None:
                print(f"{mode:<6} {size:>4}  no result within {args.timeout} s")
                continue
            if args.report:
                with args.report.open("a") as out:
                    out.write(json.dumps({**result, "device": model}) + "\n")
            m = result.get("moving", {})
            print(f"{mode:<6} {size:>4} {result['sceneReadyMs']:>9.0f} {m.get('frames', 0):>7} "
                  f"{m.get('p50Ms', 0):>6.1f} {m.get('p95Ms', 0):>6.1f} {m.get('syncRenderMeanMs', 0):>10.2f} "
                  f"{m.get('syncRenderP95Ms', 0):>9.2f} {result['moveWall']['meanMicros'] / 1000:>8.0f}")


if __name__ == "__main__":
    main()
