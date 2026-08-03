# cube_corner_pose.onnx — provenance

Phase 1 learned corner detector (docs/pipeline-plan.md): finds the cube
body content-agnostically and localises its 8 outer corners.

- Architecture: YOLO11n-pose, 1 class ("cube"), 8 keypoints, 2.70M params,
  6.8 GFLOPs. Exported opset 19, verified against the PyTorch weights to
  <0.5 px (box and keypoints).
- Training data: `tools/synthdata` renders (commit "Close the detector's
  sim-to-real gap"), ~6000 train / 600 val at 640 px: random states and
  SO(3) poses, apparent size ~12–70% of frame height incl. face-on,
  finger-like occluders, motion blur/JPEG/WB jitter, cubes composited
  over real background frames (absent-01 corpus clip, validation split
  held out) plus ~15% pure-background hard negatives.
- Metrics: synthetic val pose mAP50 0.944 / mAP50-95 0.927. Real-footage
  protocol (40 hand-boxed cardboard frames, IoU>0.3): steady 14/14,
  freerot 13/13, occlusion 11/12 TP; 0 off-cube boxes; 0/383 false
  positives on the cube-absent clip.
- Interface: input `images` [1,3,640,640] float32 (letterbox 640, pad
  114, BGR→RGB, /255, CHW); output `output0` [1,29,8400] — rows 0–3 box
  cx,cy,w,h (letterbox px), row 4 cube score (sigmoid), rows 5–28 = 8 ×
  (x px, y px, conf sigmoid). Un-letterbox: (v − pad)/r.
- Keypoint slot order is synthdata's binary corner order (bit0/1/2 =
  x/y/z sign, x fastest); `corner_detector.cpp` maps slots to Singmaster
  ids (0 URF … 7 DRB) via {6,7,2,3,5,4,1,0} — the mapping verified
  algebraically and on renders in tools/synthdata.
- License: model weights derived from Ultralytics YOLO11n-pose pretrained
  weights (AGPL-3.0) fine-tuned on project-generated data. NOTE: AGPL
  licensing of Ultralytics-derived weights must be resolved before any
  binary distribution of this model (fine for local development and CI);
  revisit at Phase 7 productisation — options: Ultralytics license,
  train-from-scratch, or an Apache-licensed pose architecture.
