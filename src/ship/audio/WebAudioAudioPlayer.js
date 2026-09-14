// SOH [WASM] JS side of WebAudioAudioPlayer (see WebAudioAudioPlayer.cpp / .h). Linked into
// the Emscripten build as a JS library; the C++ side calls the lus_webaudio_* functions.
//
// State lives on Module.LUSWebAudio so the embedding page and the tests can read it:
//   { ctx, node, url, pending, queued, consumed, dropped, underruns, channels, closed,
//     failed, listeners, poll }
// `queued`, `consumed` and `dropped` are cumulative frame counts; queued - consumed - dropped
// is what is buffered ahead of the speaker. `consumed` lags: the worklet reports every four
// render quanta, and during a long main-thread frame those reports pile up and drain
// together afterwards, so right after such a frame Buffered() reads high for a tick or two.
// The game then synthesises its smaller update size for those ticks and catches up; it never
// discards audio on the strength of the stale number, because the drop check in
// lus_webaudio_play reads the same value.

mergeInto(LibraryManager.library, {
  // The processor that runs on the audio rendering thread. Its source is handed to the
  // worklet through a Blob URL built from toString(), so no extra file has to be served next
  // to soh.js. Two consequences: this function must not reference anything from library
  // scope (it is evaluated in the worklet's global scope, where only the Web Audio globals
  // exist), and it is never called on the main thread, where AudioWorkletProcessor does not
  // exist. Under --closure 1 (not used today) AudioWorkletProcessor, registerProcessor and
  // sampleRate would need externs.
  $lus_webaudio_processor: function () {
    class LusProcessor extends AudioWorkletProcessor {
      constructor(options) {
        super();
        const o = options.processorOptions;
        this.channels = o.channels;
        this.capacity = o.capacity;
        // Source frames per output frame; 1 when the context honoured the requested rate.
        this.step = o.sourceRate / sampleRate;
        this.ring = [];
        for (let c = 0; c < this.channels; c++) {
          this.ring.push(new Float32Array(this.capacity));
        }
        this.written = 0;   // source frames ever written (ring index = frame % capacity)
        this.readPos = 0;   // source frame position of the next output sample, fractional
        this.dropped = 0;   // source frames refused because the ring was full
        this.underruns = 0; // render quanta filled with silence while the tab was visible
        this.hidden = false;
        this.blocks = 0;
        this.port.onmessage = (e) => {
          if (ArrayBuffer.isView(e.data)) {
            this.enqueue(e.data);
          } else if (typeof e.data.hidden === 'boolean') {
            this.hidden = e.data.hidden;
          }
        };
      }

      enqueue(samples) {
        const channels = this.channels;
        const total = Math.floor(samples.length / channels);
        const room = this.capacity - (this.written - Math.floor(this.readPos));
        const frames = Math.max(0, Math.min(total, room));
        for (let i = 0; i < frames; i++) {
          const w = (this.written + i) % this.capacity;
          for (let c = 0; c < channels; c++) {
            this.ring[c][w] = samples[i * channels + c] / 32768;
          }
        }
        this.written += frames;
        // The main thread caps well below capacity, so this should not happen; it is counted
        // and reported so Buffered() stays honest if it ever does.
        this.dropped += total - frames;
      }

      report() {
        this.port.postMessage({
          consumed: Math.floor(this.readPos),
          dropped: this.dropped,
          underruns: this.underruns,
        });
      }

      process(inputs, outputs) {
        const out = outputs[0];
        const n = out[0].length;
        const step = this.step;
        const need = Math.ceil(n * step) + 2;
        if (this.written - Math.floor(this.readPos) < need) {
          // Starved: a whole quantum of silence rather than a partial one, and no advance,
          // so the queued audio plays intact once the main thread catches up. A hidden tab
          // starves by design (the game's frame loop is throttled), so that is not counted.
          for (let c = 0; c < out.length; c++) {
            out[c].fill(0);
          }
          if (!this.hidden) {
            this.underruns++;
          }
          this.report();
          return true;
        }
        const capacity = this.capacity;
        for (let c = 0; c < out.length; c++) {
          const ring = this.ring[Math.min(c, this.channels - 1)];
          const dst = out[c];
          let pos = this.readPos;
          for (let j = 0; j < n; j++) {
            const i0 = Math.floor(pos);
            const frac = pos - i0;
            const a = ring[i0 % capacity];
            const b = ring[(i0 + 1) % capacity];
            dst[j] = a + (b - a) * frac;
            pos += step;
          }
        }
        this.readPos += n * step;
        if ((++this.blocks & 3) === 0) {
          this.report();
        }
        return true;
      }
    }
    registerProcessor('lus-audio', LusProcessor);
  },

  // An AudioContext at the game's rate if the browser allows it (it then resamples), at the
  // device rate otherwise (the worklet then resamples). null when there is no Web Audio or
  // no AudioWorklet, which is the signal to fall back to SDL.
  $lus_webaudio_createContext: function (rate) {
    const Ctx = typeof AudioContext !== 'undefined' ? AudioContext
              : (typeof webkitAudioContext !== 'undefined' ? webkitAudioContext : null);
    if (!Ctx) {
      return null;
    }
    let ctx = null;
    try {
      ctx = new Ctx({ sampleRate: rate });
    } catch (e) {
      try {
        ctx = new Ctx();
      } catch (e2) {
        return null;
      }
    }
    if (!ctx.audioWorklet || typeof AudioWorkletNode === 'undefined') {
      ctx.close().catch(() => {});
      return null;
    }
    return ctx;
  },

  // Loads the processor and connects the node. Asynchronous: updates played before it
  // resolves wait in state.pending, in order. A rejection (a page whose CSP does not allow
  // blob: scripts, say) sets state.failed, which Audio::GetAudioPlayer reads to fall back.
  $lus_webaudio_loadWorklet__deps: ['$lus_webaudio_processor'],
  $lus_webaudio_loadWorklet: function (state, rate, channels, capacity) {
    const source = '(' + lus_webaudio_processor.toString() + ')();';
    state.url = URL.createObjectURL(new Blob([source], { type: 'application/javascript' }));
    state.ctx.audioWorklet.addModule(state.url).then(() => {
      if (state.closed) {
        return;
      }
      const node = new AudioWorkletNode(state.ctx, 'lus-audio', {
        numberOfInputs: 0,
        numberOfOutputs: 1,
        outputChannelCount: [channels],
        processorOptions: { channels: channels, capacity: capacity, sourceRate: rate },
      });
      node.port.onmessage = (e) => {
        state.consumed = e.data.consumed;
        state.dropped = e.data.dropped;
        state.underruns = e.data.underruns;
      };
      node.connect(state.ctx.destination);
      state.node = node;
      node.port.postMessage({ hidden: document.hidden });
      const pending = state.pending;
      state.pending = [];
      for (const samples of pending) {
        node.port.postMessage(samples, [samples.buffer]);
      }
    }).catch((e) => {
      console.error('Web Audio: the worklet could not be loaded: ' + e);
      state.failed = true;
    });
  },

  // Keeps the context running. Browsers create it suspended until the page has had a user
  // gesture, and a gamepad button is not one, so the gesture listeners are backed by a poll
  // of userActivation, which any earlier gesture on the page satisfies. The context can also
  // leave 'running' later with no visibility change -- on iOS a phone call, Siri, or a
  // headphone or Bluetooth route change -- so everything stays armed until close and every
  // state change away from 'running' retries. resume() is a no-op while running.
  $lus_webaudio_armResume: function (state) {
    const ctx = state.ctx;
    const resume = () => {
      if (!state.closed && ctx.state !== 'running') {
        ctx.resume().catch(() => {});
      }
    };
    const onVisibility = () => {
      if (state.node) {
        state.node.port.postMessage({ hidden: document.hidden });
      }
      if (!document.hidden) {
        resume();
      }
    };
    state.listeners = [
      [window, 'keydown', resume],
      [window, 'pointerdown', resume],
      [window, 'mousedown', resume],
      [window, 'touchend', resume],
      [document, 'visibilitychange', onVisibility],
    ];
    for (const [target, name, handler] of state.listeners) {
      target.addEventListener(name, handler, true);
    }
    ctx.onstatechange = () => {
      if (ctx.state !== 'running' && ctx.state !== 'closed') {
        resume();
      }
    };
    state.poll = setInterval(() => {
      if (ctx.state !== 'running' && navigator.userActivation && navigator.userActivation.hasBeenActive) {
        resume();
      }
    }, 250);
  },

  lus_webaudio_init__deps: ['$lus_webaudio_createContext', '$lus_webaudio_loadWorklet', '$lus_webaudio_armResume'],
  lus_webaudio_init: function (rate, channels, capacity) {
    const ctx = lus_webaudio_createContext(rate);
    if (!ctx) {
      return 0;
    }
    const state = {
      ctx: ctx, node: null, url: null, pending: [],
      queued: 0, consumed: 0, dropped: 0, underruns: 0,
      channels: channels, closed: false, failed: false, listeners: [], poll: null,
    };
    Module.LUSWebAudio = state;
    lus_webaudio_loadWorklet(state, rate, channels, capacity);
    lus_webaudio_armResume(state);
    return 1;
  },

  lus_webaudio_close: function () {
    const state = Module.LUSWebAudio;
    if (!state) {
      return;
    }
    state.closed = true;
    clearInterval(state.poll);
    for (const [target, name, handler] of state.listeners) {
      target.removeEventListener(name, handler, true);
    }
    state.ctx.onstatechange = null;
    if (state.node) {
      state.node.port.onmessage = null;
      state.node.disconnect();
    }
    if (state.url) {
      URL.revokeObjectURL(state.url);
    }
    state.ctx.close().catch(() => {});
    Module.LUSWebAudio = null;
  },

  lus_webaudio_buffered: function () {
    const state = Module.LUSWebAudio;
    return state ? Math.max(0, state.queued - state.consumed - state.dropped) : 0;
  },

  lus_webaudio_failed: function () {
    const state = Module.LUSWebAudio;
    return state && state.failed ? 1 : 0;
  },

  // ptr/len: interleaved int16 frames in the heap. Copied out (the heap cannot be
  // transferred) and posted with the copy transferred. maxQueued: frames beyond which the
  // update is discarded instead -- see the C++ side.
  lus_webaudio_play: function (ptr, len, maxQueued) {
    const state = Module.LUSWebAudio;
    if (!state || state.closed || state.failed) {
      return;
    }
    if (state.queued - state.consumed - state.dropped >= maxQueued) {
      return;
    }
    const samples = HEAP16.slice(ptr >> 1, (ptr + len) >> 1);
    state.queued += Math.floor(samples.length / state.channels);
    if (state.node) {
      state.node.port.postMessage(samples, [samples.buffer]);
    } else {
      state.pending.push(samples);
    }
  },
});
