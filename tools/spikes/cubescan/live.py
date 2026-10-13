"""Live direct-scan spike: hold the cube up and watch each side get read.

Every frame of a session is saved in the corpus recording format (analysis
frames, fullres/, index.csv, manifest.json) so sessions replay offline.
Recordings go to --out, which must stay outside the repository.

keys   SPACE  start / stop a session        Q or ESC  quit
       M      mirror the preview (chips always show the side as it really is)

  python live.py                         # camera 0, live window
  python live.py --replay <session-dir> --headless
"""
import argparse
import json
import os
import queue
import sys
import threading
import time

import cv2
import numpy as np

import cubescan as cs

TAU = 12.0            # whole-side signature distance that still counts as the same side
MIN_READS = 8         # steady reads before a side counts as captured
LOCK_AFTER = 8        # consecutive lattice frames before photometry is frozen
ANALYSIS_W = 640
WINDOW = 'Cubeulator - direct scan'


class Camera:
    def __init__(self, index):
        self.cap = cv2.VideoCapture(index, cv2.CAP_DSHOW)
        if not self.cap.isOpened():
            sys.exit(f'camera {index} did not open')
        self.info = {}
        self.auto()

    def read(self):
        ok, frame = self.cap.read()
        return frame if ok else None

    def _pump(self, n):
        frame = None
        for _ in range(n):
            f = self.read()
            if f is not None:
                frame = f
        return frame

    @staticmethod
    def _blue_green(frame):
        b, g, _ = [float(c.mean()) for c in cv2.split(frame)]
        return b / max(g, 1e-6)

    def auto(self):
        self.cap.set(cv2.CAP_PROP_AUTO_WB, 1)
        self.cap.set(cv2.CAP_PROP_AUTO_EXPOSURE, 1)
        self.info = {}

    def lock(self):
        """Freeze white balance at what auto had chosen, then exposure and gain.

        Switching auto white balance off on its own drops this camera onto a
        stored manual temperature, so the temperature is searched for the one
        that reproduces the auto image's blue/green ratio.
        """
        target = self._blue_green(self._pump(4))
        self.cap.set(cv2.CAP_PROP_AUTO_WB, 0)
        lo, hi = 2800.0, 6500.0
        for _ in range(5):
            mid = (lo + hi) / 2
            self.cap.set(cv2.CAP_PROP_WB_TEMPERATURE, mid)
            if self._blue_green(self._pump(6)) > target:
                lo = mid                      # still too blue: warmer scene than this setting
            else:
                hi = mid
        temp = round((lo + hi) / 2)
        self.cap.set(cv2.CAP_PROP_WB_TEMPERATURE, temp)
        exposure = self.cap.get(cv2.CAP_PROP_EXPOSURE)
        self.cap.set(cv2.CAP_PROP_EXPOSURE, exposure)    # a write is what leaves auto
        got = self._blue_green(self._pump(4))
        self.info = dict(wb_temp=temp, exposure=exposure, gain=self.cap.get(cv2.CAP_PROP_GAIN),
                         blue_green_auto=round(target, 3), blue_green_locked=round(got, 3))

    def close(self):
        self.auto()
        self.cap.release()


class Replay:
    info = {}

    def __init__(self, src):
        self.src = src
        self.names = sorted(n for n in os.listdir(src) if n.startswith('rec_') and n.endswith('.jpg'))
        self.i = 0

    def read(self):
        if self.i >= len(self.names):
            return None
        frame = cv2.imread(os.path.join(self.src, self.names[self.i]))
        self.i += 1
        return frame

    def auto(self): pass
    def lock(self): pass
    def close(self): pass


class Writer:
    def __init__(self, threads=3):
        self.q = queue.Queue()
        self.dropped = 0
        for _ in range(threads):
            threading.Thread(target=self._run, daemon=True).start()

    def put(self, path, image, quality, essential):
        # Analysis frames are what a replay runs on and are never dropped; the
        # full-resolution copy gives way first if the disk falls behind.
        if not essential and self.q.qsize() > 150:
            self.dropped += 1
            return
        self.q.put((path, image, quality))

    def _run(self):
        while True:
            path, image, quality = self.q.get()
            cv2.imwrite(path, image, [cv2.IMWRITE_JPEG_QUALITY, quality])
            self.q.task_done()

    def drain(self):
        self.q.join()


class Recording:
    def __init__(self, root, writer, fullres):
        self.dir, self.writer, self.fullres = root, writer, fullres
        os.makedirs(os.path.join(root, 'fullres') if fullres else root, exist_ok=True)
        self.rows = ['pass,t_ms,w,h,full_w,full_h']

    def add(self, idx, small, full):
        name = f'rec_{idx:06d}.jpg'
        self.writer.put(os.path.join(self.dir, name), small, 92, True)
        if self.fullres and full is not small:
            self.writer.put(os.path.join(self.dir, 'fullres', name), full, 88, False)
        self.rows.append(f'{idx},{int(time.monotonic() * 1000)},{small.shape[1]},{small.shape[0]},'
                         f'{full.shape[1]},{full.shape[0]}')

    def close(self, lighting, extra):
        self.writer.drain()
        with open(os.path.join(self.dir, 'index.csv'), 'w') as f:
            f.write('\n'.join(self.rows) + '\n')
        manifest = dict(state=None, lighting=lighting, motion_script='side-by-side with a brief pause',
                        camera='workstation webcam', intrinsics=None, cube='')
        with open(os.path.join(self.dir, 'manifest.json'), 'w') as f:
            json.dump(manifest, f, indent=2)
        with open(os.path.join(self.dir, 'spike_result.json'), 'w') as f:
            json.dump(extra, f, indent=2)


class Ui:
    """A Tk window: the installed OpenCV is the headless build and cannot open one."""

    def __init__(self):
        import tkinter as tk
        from PIL import Image, ImageTk
        self.Image, self.ImageTk = Image, ImageTk
        self.root = tk.Tk()
        self.root.title(WINDOW)
        self.root.attributes('-topmost', True)
        self.width = min(1280, self.root.winfo_screenwidth() - 60)
        self.label = tk.Label(self.root, bd=0)
        self.label.pack()
        self.keys, self.open, self.photo = [], True, None
        self.root.bind('<Key>', lambda e: self.keys.append(e.keysym.lower()))
        self.root.protocol('WM_DELETE_WINDOW', self._closed)
        self.root.focus_force()

    def _closed(self):
        self.open = False

    def show(self, bgr):
        height = round(self.width * bgr.shape[0] / bgr.shape[1])
        rgb = cv2.cvtColor(cv2.resize(bgr, (self.width, height), interpolation=cv2.INTER_AREA),
                           cv2.COLOR_BGR2RGB)
        self.photo = self.ImageTk.PhotoImage(self.Image.fromarray(rgb))
        self.label.configure(image=self.photo)
        self.root.update()
        keys, self.keys = self.keys, []
        return keys

    def destroy(self):
        self.root.destroy()


class Scan:
    def __init__(self):
        self.groups, self.prev, self.current, self.result = [], None, None, None
        self.lattice_frames = self.steady_reads = 0
        self.read_at = None

    def feed(self, idx, rd):
        self.current = None
        if rd is None:
            self.prev = None
            return
        self.lattice_frames += 1
        rd['frame'] = idx
        steady = (self.prev is not None and self.prev['frame'] == idx - 1
                  and np.linalg.norm(rd['xy'][4] - self.prev['xy'][4]) < 0.12 * rd['pitch'])
        self.prev = rd
        if not steady:
            return
        self.steady_reads += 1
        best = None
        for g in self.groups:
            d, k = cs.face_dist(rd['lab'], g['ref'])
            if best is None or d < best[0]:
                best = (d, k, g)
        if best and best[0] < TAU:
            _, k, g = best
            g['items'].append(rd['lab'][cs.ROLL[k]])
            g['frames'].append(idx)
            g['ref'] = np.median(np.array(g['items']), axis=0)
            g['roll'] = k
        else:
            g = dict(ref=rd['lab'].copy(), items=[rd['lab']], frames=[idx], roll=0)
            self.groups.append(g)
        self.current = g

    def sides(self):
        return sorted((g for g in self.groups if len(g['items']) >= MIN_READS),
                      key=lambda g: -len(g['items']))

    def shown(self):
        """Captured sides in the order they were first seen, so chips do not jump about."""
        return [g for g in self.groups if len(g['items']) >= MIN_READS][:6]

    def evaluate(self, idx=None):
        sides = self.sides()
        if len(sides) < 6:
            return None
        sides = sides[:6]
        fused = [g['ref'] for g in sides]
        labels, _, margin = cs.name_colours(fused)
        grid = [labels[i * 9:(i + 1) * 9] for i in range(6)]
        if self.result and self.result['grid'] == grid:
            return self.result                    # same naming: the roll search cannot differ
        survivors = cs.assemble(grid)
        status = 'read' if len(survivors) == 1 else 'ambiguous' if survivors else 'conflict'
        if status == 'read':
            for g, row in zip(sides, grid):
                g['names'] = row
            if self.read_at is None:
                self.read_at = idx
        self.result = dict(status=status, grid=grid, margin=round(margin, 1),
                           state=survivors[0] if len(survivors) == 1 else None,
                           survivors=len(survivors), read_at_frame=self.read_at)
        return self.result

    def summary(self):
        return dict(lattice_frames=self.lattice_frames, steady_reads=self.steady_reads,
                    group_sizes=sorted((len(g['items']) for g in self.groups), reverse=True),
                    groups=[dict(reads=len(g['items']), first=g['frames'][0], last=g['frames'][-1],
                                 centre_lab=[round(float(v), 1) for v in g['ref'][4]])
                            for g in self.groups if len(g['items']) >= 3],
                    result=self.result)


NAMED = {'W': (245, 245, 245), 'Y': (0, 215, 255), 'R': (40, 40, 220), 'O': (0, 130, 255),
         'G': (70, 170, 40), 'B': (200, 90, 20)}


def lab_to_bgr(lab):
    px = cv2.cvtColor(np.float32(lab).reshape(1, -1, 3), cv2.COLOR_Lab2BGR).reshape(-1, 3)
    return np.clip(px * 255, 0, 255).astype(int)


def text(img, s, org, scale=0.7, colour=(255, 255, 255), thick=2):
    cv2.putText(img, s, org, cv2.FONT_HERSHEY_SIMPLEX, scale, (0, 0, 0), thick + 3, cv2.LINE_AA)
    cv2.putText(img, s, org, cv2.FONT_HERSHEY_SIMPLEX, scale, colour, thick, cv2.LINE_AA)


def draw(small, rd, scan, state, mirror, lines):
    view = cv2.resize(small, None, fx=2, fy=2, interpolation=cv2.INTER_LINEAR)
    if mirror:
        view = cv2.flip(view, 1)
    h, w = view.shape[:2]
    if rd is not None:
        pts = rd['xy'] * 2
        if mirror:
            pts = np.column_stack([w - 1 - pts[:, 0], pts[:, 1]])
        counted = scan is not None and scan.current is not None
        colour = (60, 230, 60) if counted else (0, 210, 255)
        centre = pts[4]
        hull = cv2.convexHull(((pts - centre) * 1.42 + centre).astype(np.float32)).astype(int)
        cv2.polylines(view, [hull], True, colour, 3, cv2.LINE_AA)
        for x, y in pts:
            cv2.circle(view, (int(x), int(y)), 5, colour, -1, cv2.LINE_AA)
    cell, pad = 30, 18
    chip = cell * 3
    sides = scan.shown() if scan is not None else []
    named = scan is not None and scan.result is not None and scan.result['status'] == 'read'
    x0 = (w - (6 * chip + 5 * pad)) // 2
    y0 = h - chip - 34
    for i in range(6):
        x = x0 + i * (chip + pad)
        cv2.rectangle(view, (x - 4, y0 - 4), (x + chip + 3, y0 + chip + 3), (25, 25, 25), -1)
        if i < len(sides):
            g = sides[i]
            cols = [NAMED[c] for c in g['names']] if named and 'names' in g else lab_to_bgr(g['ref'])
            roll = cs.ROLL[g['roll']]
            for j in range(9):
                p = int(roll[j])                  # where this cell sat when the side was last shown
                cx, cy = x + (p % 3) * cell, y0 + (p // 3) * cell
                cv2.rectangle(view, (cx + 1, cy + 1), (cx + cell - 2, cy + cell - 2),
                              tuple(int(v) for v in cols[j]), -1)
            if scan.current is g:
                cv2.rectangle(view, (x - 4, y0 - 4), (x + chip + 3, y0 + chip + 3), (255, 255, 255), 2)
        else:
            cv2.rectangle(view, (x + 1, y0 + 1), (x + chip - 2, y0 + chip - 2), (70, 70, 70), 1)
    for i, (s, colour) in enumerate(lines):
        text(view, s, (18, 36 + 34 * i), 0.8 if i == 0 else 0.62, colour)
    if state == 'scanning':
        cv2.circle(view, (w - 30, 30), 10, (40, 40, 230), -1, cv2.LINE_AA)
    return view


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--camera', type=int, default=0)
    ap.add_argument('--replay')
    ap.add_argument('--headless', action='store_true')
    ap.add_argument('--out', default=r'D:\cubeulator-corpus\direct-scan')
    ap.add_argument('--lighting', default='unlabelled')
    ap.add_argument('--no-fullres', action='store_true')
    ap.add_argument('--no-lock', action='store_true')
    ap.add_argument('--max-frames', type=int, default=0)
    ap.add_argument('--snapshot', help='write the last rendered preview here (debug)')
    args = ap.parse_args()

    source = Replay(args.replay) if args.replay else Camera(args.camera)
    writer = None if args.replay else Writer()
    day = time.strftime('%Y%m%d')
    state, scan, rec, mirror = 'idle', None, None, False
    idx = lattice_run = 0
    last_eval = done_at = started = 0.0
    session_no = 0
    banner, banner_secs = None, 0.0

    def start():
        nonlocal state, scan, rec, idx, lattice_run, done_at, started, session_no, banner
        source.auto()
        scan, idx, lattice_run, done_at, banner = Scan(), 0, 0, 0.0, None
        started = time.monotonic()
        if writer:
            root = os.path.join(args.out, day)
            os.makedirs(root, exist_ok=True)
            session_no = 1 + max([int(d[1:3]) for d in os.listdir(root) if d[:1] == 's' and d[1:3].isdigit()] or [0])
            rec = Recording(os.path.join(root, f's{session_no:02d}-{args.lighting}'), writer,
                            not args.no_fullres)
        state = 'scanning' if (args.replay or args.no_lock) else 'metering'

    def stop():
        nonlocal state, rec, banner, banner_secs
        res = scan.result or {}
        took = time.monotonic() - started
        summary = dict(scan.summary(), seconds=round(took, 1), frames=idx, photometry=source.info,
                       lighting=args.lighting)
        if rec:
            summary['dropped_writes'] = writer.dropped
            rec.close(args.lighting, summary)
            print(f'SESSION {os.path.basename(rec.dir)}', end='  ')
            rec = None
        print(f"{res.get('status', 'incomplete')}  frames {idx}  {took:.1f}s  reads {scan.steady_reads}"
              f"  groups {summary['group_sizes']}  state {res.get('state')}  photometry {source.info}",
              flush=True)
        banner, banner_secs = res.get('status', 'incomplete'), took
        source.auto()
        state = 'idle'

    ui = None if args.headless else Ui()
    if args.replay:
        start()
    else:
        print('camera open; window up; SPACE starts a session', flush=True)

    while True:
        frame = source.read()
        if frame is None:
            if args.replay:
                break
            continue
        if frame.shape[1] != ANALYSIS_W:
            small = cv2.resize(frame, (ANALYSIS_W, round(ANALYSIS_W * frame.shape[0] / frame.shape[1])),
                               interpolation=cv2.INTER_AREA)
        else:
            small = frame
        lab = cv2.cvtColor(small.astype(np.float32) / 255.0, cv2.COLOR_BGR2Lab)
        rd = cs.read_face(small, lab)
        now = time.monotonic()

        if state == 'metering':
            lattice_run = lattice_run + 1 if rd is not None else 0
            if lattice_run >= LOCK_AFTER:
                source.lock()
                state, rd = 'scanning', None
        elif state == 'scanning':
            scan.feed(idx, rd)
            if now - last_eval > 0.4 and len(scan.sides()) >= 6:
                last_eval = now
                try:
                    scan.evaluate(idx)
                except Exception as exc:           # a spike must not die mid-session
                    print('evaluate failed:', exc, flush=True)
            if scan.result and scan.result['status'] == 'read':
                done_at = done_at or now
                if now - done_at > 1.0 and not args.replay:
                    stop()
        if rec and state != 'idle':
            rec.add(idx, small, frame)
        if state != 'idle':
            idx += 1

        if ui is None:
            continue
        if state == 'idle':
            if banner == 'read':
                lines = [(f'CUBE READ in {banner_secs:.0f} s - all 54 stickers, one valid cube', (90, 240, 90)),
                         ('SPACE: scan again     Q: quit', (255, 255, 255))]
            elif banner:
                lines = [(f'Stopped: {banner}', (0, 200, 255)), ('SPACE: scan again     Q: quit', (255, 255, 255))]
            else:
                lines = [('Hold the cube up. SPACE starts a scan.', (255, 255, 255)),
                         ('Q quits. Dots on the stickers mean the cube is seen.', (220, 220, 220))]
        elif state == 'metering':
            lines = [('Hold one side of the cube square to the camera...', (0, 210, 255)),
                     ('setting exposure for the cube', (220, 220, 220))]
        else:
            n = len(scan.sides())
            conflict = scan.result and scan.result['status'] != 'read'
            lines = [(f'{min(n, 6)} of 6 sides read', (90, 240, 90) if n >= 6 else (255, 255, 255)),
                     ('show each side square-on for about a second' if not conflict
                      else 'six sides seen but they do not make a valid cube yet - show each side again',
                      (220, 220, 220) if not conflict else (0, 200, 255)),
                     ('SPACE stops', (180, 180, 180))]
        view = draw(small, rd, scan if state != 'metering' else None, state, mirror, lines)
        keys = ui.show(view)
        if args.snapshot:
            cv2.imwrite(args.snapshot, view)
        if args.max_frames and idx >= args.max_frames:
            break
        if not ui.open or 'q' in keys or 'escape' in keys:
            break
        if 'space' in keys:
            stop() if state != 'idle' else start()
        if 'm' in keys:
            mirror = not mirror

    if state != 'idle':
        if args.replay:
            scan.evaluate()
        stop()
    source.close()
    if ui is not None:
        ui.destroy()


if __name__ == '__main__':
    main()
