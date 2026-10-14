// A camera for the page to find in place of a real one.
//
// Injected before the app's own scripts (Playwright's addInitScript), it
// replaces navigator.mediaDevices.getUserMedia() with a MediaStream cut from a
// canvas the test draws on, and enumerateDevices() with one camera. Everything
// downstream is the app's own path: the <video> element, the draw to a canvas,
// getImageData(), the pixels into the reader.
//
// What is drawn is what tests/unit/drawn_cube.hpp draws for the native tests:
// a cluttered grey room, and a cube side as dark plastic with nine stickers.
// `window.__fakeCamera` is how a test changes it.
(() => {
  'use strict';

  // An init script runs in every document of the context, the images and
  // scripts the harness opens to clear storage included; only the app's page
  // has a camera.
  if (document.contentType !== 'text/html') {
    return;
  }

  const WIDTH = 640;
  const HEIGHT = 360;

  // drawn_cube.hpp's paint() is BGR; these are the same colours as RGB.
  const PAINT = {
    white: [235, 235, 235],
    yellow: [235, 210, 40],
    red: [200, 40, 40],
    orange: [240, 120, 30],
    green: [40, 160, 70],
    blue: [20, 80, 170],
  };

  // A deterministic generator, so the room is the same in every frame and
  // every run.
  const prng = (seed) => () => {
    seed = (seed + 0x6d2b79f5) | 0;
    let t = Math.imul(seed ^ (seed >>> 15), 1 | seed);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };

  const room = (() => {
    const random = prng(1234);
    const boxes = [];
    for (let i = 0; i < 40; i++) {
      boxes.push({
        x: Math.floor(random() * WIDTH),
        y: Math.floor(random() * HEIGHT),
        w: 8 + Math.floor(random() * (WIDTH / 6 - 8)),
        h: 8 + Math.floor(random() * (HEIGHT / 5 - 8)),
        colour: `rgb(${30 + Math.floor(random() * 190)},${30 + Math.floor(random() * 190)},${30 + Math.floor(random() * 190)})`,
      });
    }
    return boxes;
  })();

  const canvas = document.createElement('canvas');
  canvas.width = WIDTH;
  canvas.height = HEIGHT;
  const context = canvas.getContext('2d');

  let view = { kind: 'room' };
  let denied = null;
  let devices = [{ deviceId: 'fake-camera-0', kind: 'videoinput', label: 'Fake camera', groupId: 'fake' }];
  let opened = 0;
  // Every getUserMedia() call, refused or not: what the page asked for.
  let attempts = 0;
  let stream = null;

  function drawRoom() {
    context.fillStyle = 'rgb(110,104,96)';
    context.fillRect(0, 0, WIDTH, HEIGHT);
    for (const box of room) {
      context.fillStyle = box.colour;
      context.fillRect(box.x, box.y, box.w, box.h);
    }
  }

  // A square `half` sticker-spacings either side of the point (column, row),
  // under the pose (centre, pitch in pixels, in-plane turn in degrees).
  function square(pose, column, row, half, colour) {
    const radians = (pose.degrees * Math.PI) / 180;
    const at = (c, r) => {
      const x = c * pose.pitch;
      const y = r * pose.pitch;
      return [
        pose.x + x * Math.cos(radians) - y * Math.sin(radians),
        pose.y + x * Math.sin(radians) + y * Math.cos(radians),
      ];
    };
    const corners = [
      at(column - half, row - half),
      at(column + half, row - half),
      at(column + half, row + half),
      at(column - half, row + half),
    ];
    context.fillStyle = colour;
    context.beginPath();
    context.moveTo(corners[0][0], corners[0][1]);
    for (const corner of corners.slice(1)) {
      context.lineTo(corner[0], corner[1]);
    }
    context.closePath();
    context.fill();
  }

  function drawSide(stickers, pose) {
    drawRoom();
    square(pose, 0, 0, 1.62, 'rgb(16,16,16)');
    for (let row = 0; row < 3; row++) {
      for (let column = 0; column < 3; column++) {
        const [r, g, b] = PAINT[stickers[row * 3 + column]];
        square(pose, column - 1, row - 1, 0.4, `rgb(${r},${g},${b})`);
      }
    }
  }

  function draw() {
    if (view.kind === 'image') {
      // A recorded frame, 640 wide: the middle rows of it, which is where a
      // cube drawn by cube_tumble_sim is.
      const rows = Math.min(HEIGHT, view.image.naturalHeight);
      context.drawImage(view.image, 0, (view.image.naturalHeight - rows) / 2, WIDTH, rows, 0, 0, WIDTH, rows);
    } else if (view.kind === 'side') {
      drawSide(view.stickers, { x: WIDTH / 2, y: HEIGHT / 2, pitch: 40, degrees: 0, ...view.pose });
    } else {
      drawRoom();
    }
  }

  // The stream only produces a frame for a canvas that has been drawn on, so
  // the canvas is redrawn at the camera's rate whether or not it changed.
  setInterval(draw, 33);

  function error(name, message) {
    const e = new Error(message);
    e.name = name;
    return e;
  }

  const mediaDevices = navigator.mediaDevices || {};
  const fake = {
    enumerateDevices: async () => devices,
    getUserMedia: async () => {
      attempts++;
      if (denied) {
        throw error(denied.name, denied.message);
      }
      if (devices.length === 0) {
        throw error('NotFoundError', 'Requested device not found');
      }
      opened++;
      draw();
      stream = canvas.captureStream(30);
      return stream;
    },
  };
  try {
    Object.defineProperty(navigator, 'mediaDevices', {
      configurable: true,
      value: Object.assign(Object.create(mediaDevices), fake, {
        addEventListener() {},
        removeEventListener() {},
      }),
    });
  } catch (e) {
    console.error('fake camera could not replace navigator.mediaDevices: ' + e);
  }

  window.__fakeCamera = {
    // What the lens sees from now on.
    room() { view = { kind: 'room' }; },
    side(stickers, pose) { view = { kind: 'side', stickers, pose }; },
    // A picture (a data: URL) for the lens to see, from the moment it has
    // loaded; until then the lens keeps what it had, and is never a room for a
    // frame in between.
    image(url) {
      const image = new Image();
      image.onload = () => {
        view = { kind: 'image', image };
      };
      image.src = url;
    },
    // The next getUserMedia() fails the way a browser's does: 'NotAllowedError'
    // (denied), 'NotFoundError', 'NotReadableError' (busy).
    deny(name, message) { denied = name ? { name, message: message || name } : null; },
    // The cameras enumerateDevices() lists: [{deviceId, label}].
    setDevices(list) {
      devices = list.map((d) => ({ deviceId: d.deviceId, kind: 'videoinput', label: d.label, groupId: 'fake' }));
    },
    opened: () => opened,
    attempts: () => attempts,
    live: () => !!stream && stream.getTracks().some((t) => t.readyState === 'live'),
  };
})();
