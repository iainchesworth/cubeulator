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
// advance. Nor does anything else below: the exposure and white balance lock
// only calls a track the person has already let the page have.
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

    // What became of one control of a lock, shared with camera_web.cpp
    // (ControlOutcome in interfaces/camera.hpp).
    NOT_REQUESTED: 0,
    UNSUPPORTED: 1,
    REJECTED: 2,
    APPLIED: 3,

    // The two controls a lock holds: the setting that says whether the camera is
    // choosing it ('mode': 'continuous' or 'manual') and the one that is its
    // value. Both are named as MediaTrackConstraints and getSettings() name them.
    CONTROLS: [
      { key: 'exposure', mode: 'exposureMode', value: 'exposureTime' },
      { key: 'whiteBalance', mode: 'whiteBalanceMode', value: 'colorTemperature' },
    ],

    // How many frames the camera is let deliver before a lock reads what its
    // automatics settled on (the desktop loop's kPhotometrySettleFrames), and
    // the most it is waited for: a camera that delivers slowly, or a page that
    // is not being painted, must not hold the scan in "setting up" for ever.
    SETTLE_FRAMES: 20,
    SETTLE_LIMIT_MS: 3000,

    // The most a call into the camera's own controls is waited for. They are a
    // round trip to a driver, and the scan is waiting for the answer.
    CONTROL_LIMIT_MS: 2000,

    // { request, track, stream, video, canvas, context, active, lastTime,
    //   frames, held, locking }
    //   frames   delivered so far, which is how a lock counts "settled"
    //   held     what a lock changed, as the modes it found them in, to put back
    //   locking  a promise that settles when the lock in flight is done with
    //            the camera (never rejects), or null
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

    // Lets go of a camera, having put back anything a lock changed in it: a UVC
    // camera keeps what it was told, and the next program to open it (this
    // page's next scan, or a video call) would find it in manual. A lock that is
    // still asking the camera is waited for first, or it would finish after the
    // track was stopped and leave the camera as it found it nowhere.
    release: function (session) {
      if (!session) {
        return;
      }
      session.active = false;
      var stop = function () {
        if (session.stream) {
          session.stream.getTracks().forEach(function (track) { track.stop(); });
        }
        if (session.video) {
          session.video.pause();
          session.video.srcObject = null;
          session.video.remove();
        }
      };
      var finish = function () {
        var restoring = CubeCamera.unlock(session);
        if (restoring) {
          restoring.then(stop, stop);
        } else {
          stop();
        }
      };
      if (session.locking) {
        session.locking.then(finish);
      } else {
        finish();
      }
    },

    // ---- Exposure and white balance ---------------------------------------
    //
    // A scan wants one side to read the same colours while the cube is turned,
    // and a camera's own exposure and white balance re-meter for whatever fills
    // the frame. A browser can be asked to hold them -
    // MediaStreamTrack.applyConstraints() with exposureMode and
    // whiteBalanceMode 'manual' - where it has the control (Chromium; Safari
    // and Firefox do not) and the camera offers it (UVC webcams and many
    // Android cameras do, an iPhone's does not). So everything here is a
    // question before it is an action, and the answer is only what was read
    // back afterwards: applyConstraints() resolves for an advanced set it could
    // not satisfy as readily as for one it applied (it skips the set), so its
    // promise is no evidence at all. interfaces/camera.hpp says why an unproven
    // control reported as locked is worse than a missing one.

    // Resolves with `promise`'s value, or with `fallback` if it rejects or has
    // not settled in time. Never rejects.
    within: function (promise, fallback) {
      return new Promise(function (resolve) {
        var timer = setTimeout(function () { resolve(fallback); }, CubeCamera.CONTROL_LIMIT_MS);
        promise.then(
          function (value) { clearTimeout(timer); resolve(value); },
          function () { clearTimeout(timer); resolve(fallback); });
      });
    },

    // The track's capabilities and settings for the four names above, as
    // { capabilities, settings } (each possibly empty). Chromium has them on
    // the track; an older one only on ImageCapture; a browser with neither has
    // no control to offer, which is an answer.
    controls: function (session) {
      var track = session.track;
      var names = ['exposureMode', 'exposureTime', 'whiteBalanceMode', 'colorTemperature'];
      var take = function (from) {
        var out = null;
        names.forEach(function (name) {
          if (from && from[name] !== undefined && from[name] !== null) {
            out = out || {};
            out[name] = from[name];
          }
        });
        return out;
      };
      var ask = function (object, method) {
        try {
          return object && typeof object[method] === 'function' ? object[method]() : undefined;
        } catch (error) {
          return undefined;
        }
      };
      var image = null;
      var viaImageCapture = function (method) {
        if (typeof ImageCapture !== 'function') {
          return Promise.resolve(null);
        }
        try {
          image = image || new ImageCapture(track);
          return image[method]().then(take, function () { return null; });
        } catch (error) {
          return Promise.resolve(null);
        }
      };
      var capabilities = take(ask(track, 'getCapabilities'));
      var settings = take(ask(track, 'getSettings'));
      return CubeCamera.within(Promise.all([
        capabilities ? Promise.resolve(capabilities) : viaImageCapture('getPhotoCapabilities'),
        settings ? Promise.resolve(settings) : viaImageCapture('getPhotoSettings'),
      ]).then(function (both) {
        return { capabilities: both[0] || {}, settings: both[1] || {} };
      }), { capabilities: {}, settings: {} });
    },

    // Whether this browser could ever have a control to offer: it has one of
    // the two places they live. Safari and Firefox have neither, and say so at
    // once; a Chromium whose camera lists none is only known to after a wait.
    hasControlApi: function (session) {
      var track = session.track;
      var probe = null;
      try {
        probe = typeof track.getCapabilities === 'function' ? track.getCapabilities() : null;
      } catch (error) {
        probe = null;
      }
      return typeof ImageCapture === 'function' ||
        !!(probe && (probe.exposureMode !== undefined || probe.whiteBalanceMode !== undefined));
    },

    // What a lock can do about one control, from what the camera says it has
    // and is doing: { control, wanted, offered, readable, requested, tolerance,
    // asFound, revertTo, held, manual, readback }.
    //   offered   a manual mode, and a range of values to hold it at
    //   readable  and it says what the value is now, which is what is held
    //             (without that, 'manual' falls back to whatever the camera has
    //             stored, which the picture shows as a jump)
    plan: function (control, wanted, before) {
      var modes = before.capabilities[control.mode];
      var range = before.capabilities[control.value];
      var value = before.settings[control.value];
      var found = before.settings[control.mode];
      var plan = {
        control: control,
        wanted: wanted,
        offered: Array.isArray(modes) && modes.indexOf('manual') >= 0 &&
                 range !== undefined && range !== null && typeof range === 'object',
        readable: typeof value === 'number' && isFinite(value),
        requested: value,
        tolerance: 0,
        asFound: typeof found === 'string' ? found : null,
        revertTo: null,
        held: false,
        manual: false,
        readback: 0,
      };
      if (plan.offered && plan.readable) {
        // A camera quantises to a step, and one step is the same setting.
        plan.tolerance = Math.max(Number(range.step) || 0, Math.abs(value) * 0.02);
      }
      if (Array.isArray(modes)) {
        plan.revertTo = plan.asFound !== null && modes.indexOf(plan.asFound) >= 0 ? plan.asFound :
          (modes.indexOf('continuous') >= 0 ? 'continuous' : null);
      }
      return plan;
    },

    // Resolves true if the camera took the constraint set, false if it refused
    // it, and null if it did not answer in time. Whether it was taken is not
    // the question (see above), but a camera that does not answer is not asked
    // again.
    apply: function (session, set) {
      var track = session.track;
      if (!track || typeof track.applyConstraints !== 'function') {
        return Promise.resolve(false);
      }
      try {
        var asked = track.applyConstraints({ advanced: [set] }).then(
          function () { return true; }, function () { return false; });
        return CubeCamera.within(asked, null);
      } catch (error) {
        return Promise.resolve(false);
      }
    },

    // Asks for `plans` to be held in manual at their values and then reads the
    // camera back, setting `held` on each plan that now is: in manual, at the
    // value. Resolves false if the camera did not answer.
    attempt: function (session, plans) {
      var set = {};
      var answered = true;
      plans.forEach(function (plan) {
        set[plan.control.mode] = 'manual';
        set[plan.control.value] = plan.requested;
      });
      return CubeCamera.apply(session, set).then(function (taken) {
        answered = taken !== null;
        return CubeCamera.controls(session);
      }).then(function (after) {
        plans.forEach(function (plan) {
          var value = after.settings[plan.control.value];
          var finite = typeof value === 'number' && isFinite(value);
          plan.manual = after.settings[plan.control.mode] === 'manual';
          plan.readback = finite ? value : 0;
          plan.held = plan.manual && finite && Math.abs(value - plan.requested) <= plan.tolerance;
        });
        return answered;
      });
    },

    // Resolves once the camera has delivered SETTLE_FRAMES more frames, or the
    // time limit has passed, or the camera has gone.
    settle: function (session) {
      var target = session.frames + CubeCamera.SETTLE_FRAMES;
      var began = Date.now();
      return new Promise(function (resolve) {
        var check = function () {
          if (!session.active || session.frames >= target ||
              Date.now() - began >= CubeCamera.SETTLE_LIMIT_MS) {
            resolve();
            return;
          }
          setTimeout(check, 40);
        };
        check();
      });
    },

    // Holds the controls `want` names ({ exposure, whiteBalance }) at the
    // values the automatics settled on, and calls `done` once with the answer:
    // { offered, exposure, whiteBalance }, each control { outcome, requested,
    // readback }. Whatever happens, and never with an exception: a camera that
    // will not be locked is a scan that goes on.
    lock: function (session, want, done) {
      var nothing = { outcome: CubeCamera.NOT_REQUESTED, requested: 0, readback: 0 };
      var answer = function (offered, results) {
        return { offered: offered, exposure: results[0] || nothing, whiteBalance: results[1] || nothing };
      };
      var unsupported = function () {
        return CubeCamera.CONTROLS.map(function (control) {
          return want[control.key] ?
            { outcome: CubeCamera.UNSUPPORTED, requested: 0, readback: 0 } : nothing;
        });
      };

      if (!want.exposure && !want.whiteBalance) {
        done(answer(false, []));
        return;
      }
      if (!CubeCamera.hasControlApi(session)) {
        done(answer(false, unsupported()));
        return;
      }

      var running = CubeCamera.settle(session).then(function () {
        if (CubeCamera.current !== session || !session.active) {
          return null;
        }
        return CubeCamera.controls(session).then(function (before) {
          var plans = CubeCamera.CONTROLS.map(function (control) {
            return CubeCamera.plan(control, !!want[control.key], before);
          });
          var offered = plans.some(function (plan) { return plan.offered; });
          var pending = plans.filter(function (plan) {
            return plan.wanted && plan.offered && plan.readable;
          });

          var chain = Promise.resolve();
          if (pending.length > 0) {
            chain = CubeCamera.attempt(session, pending).then(function (answered) {
              // A control the camera will not hold vetoes the set it is in; the
              // other may still be holdable on its own. (Not a camera that did
              // not answer: it would not answer these either, and the scan is
              // waiting. Nor one that has been let go of since.)
              var missed = pending.filter(function (plan) { return !plan.held; });
              if (!answered || !session.active || pending.length < 2 || missed.length === 0) {
                return undefined;
              }
              return missed.reduce(function (previous, plan) {
                return previous.then(function () { return CubeCamera.attempt(session, [plan]); });
              }, Promise.resolve());
            }).then(function () {
              // A control that went to manual and would not hold is put back:
              // a camera frozen at a value nobody can vouch for is worse than
              // its own automatics.
              var revert = {};
              var any = false;
              pending.forEach(function (plan) {
                if (!plan.held && plan.manual && plan.asFound !== 'manual' && plan.revertTo) {
                  revert[plan.control.mode] = plan.revertTo;
                  any = true;
                }
              });
              return any ? CubeCamera.apply(session, revert) : undefined;
            });
          }
          return chain.then(function () {
            session.held = session.held || {};
            plans.forEach(function (plan) {
              // The first mode found is the one to give back, however many
              // times a lock is asked for.
              if (plan.held && plan.asFound !== 'manual' && plan.revertTo &&
                  session.held[plan.control.mode] === undefined) {
                session.held[plan.control.mode] = plan.revertTo;
              }
            });
            return answer(offered, plans.map(function (plan) {
              if (!plan.wanted) {
                return nothing;
              }
              if (!plan.offered || !plan.readable) {
                return { outcome: CubeCamera.UNSUPPORTED, requested: 0, readback: 0 };
              }
              return {
                outcome: plan.held ? CubeCamera.APPLIED : CubeCamera.REJECTED,
                requested: plan.requested,
                readback: plan.readback,
              };
            }));
          });
        });
      });
      session.locking = running.then(function () {}, function () {}).then(function () {
        session.locking = null;
      });
      running.then(function (result) {
        // Nothing to say for a camera that has gone: the answer would be for a
        // session that is over.
        if (result) {
          done(result);
        }
      }, function () {
        // Not a thing that should happen (every step above is guarded), but an
        // answer the scan is waiting for is not one to lose to a surprise.
        done(answer(false, unsupported()));
      });
    },

    // Puts back the modes a lock changed. Returns a promise that resolves when
    // the camera has taken them (or not), or null at once if there is nothing
    // to put back.
    unlock: function (session) {
      if (!session || !session.track || !session.held) {
        return null;
      }
      var set = session.held;
      session.held = null;
      if (Object.keys(set).length === 0) {
        return null;
      }
      return CubeCamera.apply(session, set);
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
        session.frames++;
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

    var session = {
      request: request, track: null, stream: null, video: null, canvas: null, context: null,
      active: false, lastTime: -1, frames: 0, held: null, locking: null,
    };
    CubeCamera.current = session;

    navigator.mediaDevices.getUserMedia(CubeCamera.constraintsFor(CubeCamera.text(idPointer, idLength)))
      .then(function (stream) {
        if (CubeCamera.current !== session) {
          // Superseded (a newer open(), or close()) while the prompt was up.
          stream.getTracks().forEach(function (track) { track.stop(); });
          return undefined;
        }
        session.stream = stream;
        session.track = stream.getVideoTracks()[0] || null;

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

  // Holds the open camera's exposure and white balance where its automatics
  // have put them, once it has delivered a few frames. Answered, always, with
  // cube_web_camera_lock_reported(); `request` is the camera's, so that an
  // answer for one that has since been closed or replaced is dropped there.
  cube_web_camera_lock__deps: ['$CubeCamera'],
  cube_web_camera_lock__proxy: 'async',
  cube_web_camera_lock: function (request, exposure, whiteBalance) {
    var session = CubeCamera.current;
    if (!session || session.request !== request || !session.active || !session.track) {
      return;
    }
    var number = function (value) {
      return typeof value === 'number' && isFinite(value) ? value : 0;
    };
    CubeCamera.lock(session, { exposure: exposure !== 0, whiteBalance: whiteBalance !== 0 },
      function (answer) {
        _cube_web_camera_lock_reported(request, answer.offered ? 1 : 0,
          answer.exposure.outcome, number(answer.exposure.requested),
          number(answer.exposure.readback),
          answer.whiteBalance.outcome, number(answer.whiteBalance.requested),
          number(answer.whiteBalance.readback));
      });
  },

  // Puts back what a lock changed, and leaves the camera running.
  cube_web_camera_unlock__deps: ['$CubeCamera'],
  cube_web_camera_unlock__proxy: 'async',
  cube_web_camera_unlock: function () {
    CubeCamera.unlock(CubeCamera.current);
  },
});
