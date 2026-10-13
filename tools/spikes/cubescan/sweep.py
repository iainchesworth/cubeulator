"""Sensitivity of the spike on the retry session: is the success fragile?"""
import sys, os, cv2, numpy as np
import cubescan as fs
src = sys.argv[1]
names = sorted(n for n in os.listdir(src) if n.startswith('rec_') and n.endswith('.jpg'))

def collect(step, scale):
    reads, prev = [], None
    for n in names[::step]:
        idx = int(n[4:10])
        bgr = cv2.imread(os.path.join(src, n))
        if scale != 1.0:
            bgr = cv2.resize(bgr, None, fx=scale, fy=scale, interpolation=cv2.INTER_AREA)
        lab = cv2.cvtColor(bgr.astype(np.float32) / 255.0, cv2.COLOR_BGR2Lab)
        rd = fs.read_face(bgr, lab)
        if rd is not None:
            rd['frame'] = idx
            if prev is not None and prev['frame'] == idx - step and np.linalg.norm(rd['xy'][4] - prev['xy'][4]) < 0.12 * rd['pitch'] * step:
                reads.append(rd)
            prev = rd
    return reads

def outcome(reads, tau, min_reads):
    groups = fs.group_reads(reads, tau)
    faces = [g for g in groups if len(g['items']) >= min_reads]
    if len(faces) != 6:
        return f'{len(faces)} faces'
    fused = [np.median(np.array(g['items']), axis=0) for g in faces]
    labels, _, _ = fs.name_colours(fused)
    surv = fs.assemble([labels[i*9:(i+1)*9] for i in range(6)])
    return surv[0] if len(surv) == 1 else f'{len(surv)} valid'

base = collect(1, 1.0)
ref = outcome(base, 9.0, 5)
print('reference state:', ref)
print('\ngrouping threshold tau (full rate, full res):')
for tau in (4, 5, 6, 7.5, 9, 12, 15, 18, 22, 26):
    o = outcome(base, tau, 5)
    print(f'  tau {tau:5.1f}: {"SAME CUBE" if o == ref else o}')
print('\nframe rate / resolution (tau 9):')
for step, scale in ((2, 1.0), (3, 1.0), (5, 1.0), (1, 0.75), (1, 0.5), (3, 0.5)):
    r = collect(step, scale)
    o = outcome(r, 9.0, max(2, 5 // step))
    print(f'  every {step} frame(s), {int(640*scale)}x{int(360*scale)}: reads {len(r):3d} -> {"SAME CUBE" if o == ref else o}')
