// The page's half of the web camera (camera_web.cpp is the other half).
//
// An Emscripten JS library (--js-library): the three functions at the bottom
// are the ones camera_web.cpp declares extern "C" and calls. Every one of them
// touches the DOM or MediaDevices, which a pthread (a Web Worker) cannot, so
// each carries a __proxy: Emscripten forwards a call made on any other thread
// to the browser's main thread and runs it there. The answers come back
// through the EMSCRIPTEN_KEEPALIVE functions in camera_web.cpp, which this
// code calls on the main thread.
//
// getUserMedia() only exists in a secure context (https, or localhost) and
// prompts the person itself the first time it is called; nothing here asks in
// advance.
//
// A library is compiled by Emscripten, not run as written: the object below
// is serialised member by member, so nothing in it may close over a variable
// outside it. State lives on `$CubeCamera` itself.
mergeInto(LibraryManager.library, {
  $CubeCamera: {
    // The vision reader works at 640 px wide (kWorkingWidth in
    // vision/direct_scan.cpp) and scales anything larger down itself. Scaling
    // here, where the browser has already decoded the frame to a canvas, means
    // far fewer pixels cross into the heap and through getImageData().
    MAX_FRAME_WIDTH: 640,

    // What the sensor is asked for. Larger than the reader needs on purpose:
    // the browser's downscale then averages real detail into each sticker
    // sample, where a 640x480 mode would hand over its own (often already
    // compressed) pixels.
    IDEAL_WIDTH: 1280,
    IDEAL_HEIGHT: 720,

    // Answers to a start, shared with camera_web.cpp (enum class Answer).
    OK: 0,
    DENIED: 1,
    NOT_FOUND: 2,
    UNAVAILABLE: 3,
    FAILED: 4,

    // { request, stream, video, canvas, context, active, lastTime }
    current: null,

    text: function (pointer, length) {
      // slice(): TextDecoder refuses a view onto shared memory.
      return new TextDecoder().decode(HEAPU8.slice(pointer, pointer + length));
    },

    // Hands a string to C++ through the buffer it owns, then reports its length
    // through `report` (an exported C function taking (request, length)).
    publish: function (request, string, report) {
      var bytes = new TextEncoder().encode(string);
      var pointer = _cube_web_camera_string_buffer(bytes.length + 1);
      HEAPU8.set(bytes, pointer);
      HEAPU8[pointer + bytes.length] = 0;
      report(request, bytes.length);
    },

    classify: function (error) {
      switch (error && error.name) {
        case 'NotAllowedError':
        case 'SecurityError':
          return CubeCamera.DENIED;
        case 'NotFoundError':
        case 'OverconstrainedError':
          return CubeCamera.NOT_FOUND;
        case 'NotReadableError':
        case 'AbortError':
          return CubeCamera.UNAVAILABLE;
        default:
          return CubeCamera.FAILED;
      }
    },

    constraintsFor: function (id) {
      var video = {
        width: { ideal: CubeCamera.IDEAL_WIDTH },
        height: { ideal: CubeCamera.IDEAL_HEIGHT },
      };
      if (id === 'facing:environment') {
        video.facingMode = { ideal: 'environment' };
      } else if (id === 'facing:user') {
        video.facingMode = { ideal: 'user' };
      } else if (id !== '' && id !== 'default') {
        video.deviceId = { exact: id };
      }
      return { audio: false, video: video };
    },

    release: function (session) {
      if (!session) {
        return;
      }
      session.active = false;
      if (session.stream) {
        session.stream.getTracks().forEach(function (track) { track.stop(); });
      }
      if (session.video) {
        session.video.pause();
        session.video.srcObject = null;
        session.video.remove();
      }
    },

    pump: function (session) {
      if (!session.active) {
        return;
      }
      var video = session.video;
      // HAVE_CURRENT_DATA: a frame has been decoded and can be drawn. And a
      // frame not seen before: the timer below can fire between two frames of
      // the camera, and a repeat of the last one would count, to the reader,
      // as the cube having been held still for another instant.
      if (video.readyState >= 2 && video.videoWidth > 0 && video.videoHeight > 0 &&
          video.currentTime !== session.lastTime) {
        session.lastTime = video.currentTime;
        var scale = Math.min(1, CubeCamera.MAX_FRAME_WIDTH / video.videoWidth);
        var width = Math.max(1, Math.round(video.videoWidth * scale));
        var height = Math.max(1, Math.round(video.videoHeight * scale));
        // The heap buffer is fixed (camera_web.cpp); an unusually tall frame is
        // scaled down to fit it rather than overrunning it.
        var capacity = _cube_web_camera_frame_capacity();
        if (width * height * 4 > capacity) {
          var fit = Math.sqrt(capacity / (width * height * 4));
          width = Math.max(1, Math.floor(width * fit));
          height = Math.max(1, Math.floor(height * fit));
        }
        if (session.canvas.width !== width || session.canvas.height !== height) {
          session.canvas.width = width;
          session.canvas.height = height;
        }
        session.context.drawImage(video, 0, 0, width, height);
        var pixels = session.context.getImageData(0, 0, width, height);
        HEAPU8.set(pixels.data, _cube_web_camera_frame_buffer());
        _cube_web_camera_frame(session.request, width, height);
      }
      CubeCamera.schedule(session);
    },

    schedule: function (session) {
      if (!session.active) {
        return;
      }
      // Whichever comes first. requestVideoFrameCallback is the right clock -
      // one call per decoded frame, in step with the camera - but a browser
      // only runs it for a frame it composites, and an element kept off the
      // page (below) or a page the browser is not painting never gets one. The
      // timer is the floor under it: ten reads a second, which is what the
      // reader needs, whatever the compositor is doing.
      var due = false;
      var go = function () {
        if (due) {
          return;
        }
        due = true;
        CubeCamera.pump(session);
      };
      var hasCallback = typeof session.video.requestVideoFrameCallback === 'function';
      if (hasCallback) {
        session.video.requestVideoFrameCallback(go);
      }
      setTimeout(go, hasCallback ? 100 : 33);
    },
  },

  // Lists the video inputs without prompting: `deviceId<TAB>label` per line.
  // Both fields are empty until the person has granted camera access at least
  // once for this site; camera_web.cpp decides what to show then.
  cube_web_camera_enumerate__deps: ['$CubeCamera'],
  cube_web_camera_enumerate__proxy: 'async',
  cube_web_camera_enumerate: function (request) {
    var done = function (lines) {
      CubeCamera.publish(request, lines.join('\n'), _cube_web_camera_devices);
    };
    if (!navigator.mediaDevices || typeof navigator.mediaDevices.enumerateDevices !== 'function') {
      done([]);
      return;
    }
    navigator.mediaDevices.enumerateDevices().then(function (devices) {
      var clean = function (value) { return String(value || '').replace(/[\t\r\n]+/g, ' '); };
      done(devices
        .filter(function (device) { return device.kind === 'videoinput'; })
        .map(function (device) { return clean(device.deviceId) + '\t' + clean(device.label); }));
    }, function () { done([]); });
  },

  // Opens a camera and, once it is delivering frames, answers with
  // cube_web_camera_started(request, OK) - or with the error class. Sync: the
  // id's storage belongs to the caller only until this returns.
  cube_web_camera_open__deps: ['$CubeCamera'],
  cube_web_camera_open__proxy: 'sync',
  cube_web_camera_open: function (request, idPointer, idLength) {
    CubeCamera.release(CubeCamera.current);
    CubeCamera.current = null;

    var fail = function (code) { _cube_web_camera_started(request, code); };
    if (!navigator.mediaDevices || typeof navigator.mediaDevices.getUserMedia !== 'function') {
      // Not a secure context: the browser withholds the whole API.
      fail(CubeCamera.UNAVAILABLE);
      return;
    }

    var session = { request: request, stream: null, video: null, canvas: null, context: null, active: false, lastTime: -1 };
    CubeCamera.current = session;

    navigator.mediaDevices.getUserMedia(CubeCamera.constraintsFor(CubeCamera.text(idPointer, idLength)))
      .then(function (stream) {
        if (CubeCamera.current !== session) {
          // Superseded (a newer open(), or close()) while the prompt was up.
          stream.getTracks().forEach(function (track) { track.stop(); });
          return undefined;
        }
        session.stream = stream;

        var video = document.createElement('video');
        video.muted = true;
        video.setAttribute('playsinline', '');
        video.setAttribute('aria-hidden', 'true');
        // Off-screen rather than display:none - iOS Safari does not decode
        // frames for a video that is not rendered.
        video.style.cssText = 'position:fixed;left:-9999px;top:0;width:2px;height:2px;opacity:0;pointer-events:none';
        video.srcObject = stream;
        document.body.appendChild(video);
        session.video = video;
        session.canvas = document.createElement('canvas');
        session.context = session.canvas.getContext('2d', { willReadFrequently: true });

        // The camera going away under us (unplugged, permission revoked from
        // the browser's own UI) ends the tracks; stop pumping.
        stream.getVideoTracks().forEach(function (track) {
          track.addEventListener('ended', function () { session.active = false; });
        });

        return video.play().then(function () {
          if (CubeCamera.current !== session) {
            CubeCamera.release(session);
            return;
          }
          session.active = true;
          _cube_web_camera_started(request, CubeCamera.OK);
          CubeCamera.schedule(session);
        });
      })
      .catch(function (error) {
        if (CubeCamera.current === session) {
          CubeCamera.release(session);
          CubeCamera.current = null;
          fail(CubeCamera.classify(error));
        }
      });
  },

  cube_web_camera_close__deps: ['$CubeCamera'],
  cube_web_camera_close__proxy: 'async',
  cube_web_camera_close: function () {
    CubeCamera.release(CubeCamera.current);
    CubeCamera.current = null;
  },
});
