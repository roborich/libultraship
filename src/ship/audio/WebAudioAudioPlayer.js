// SOH [WASM] JS side of WebAudioAudioPlayer (see WebAudioAudioPlayer.cpp / .h). Linked into
// the Emscripten build as a JS library; the C++ side calls the lus_webaudio_* functions.
//
// State lives on Module.LUSWebAudio so the embedding page and the tests can read it:
//   { ctx, node, url, pending, queued, consumed, underruns, channels, closed, failed }
// `queued` and `consumed` are cumulative frame counts; their difference is what is buffered
// ahead of the speaker. `consumed` lags by up to one report interval (4 blocks, 512 frames),
// which makes Buffered() a slight overestimate -- the safe direction for both the game's
// top-up decision and the drop cap.

mergeInto(LibraryManager.library, {
  // The processor that runs on the audio rendering thread. Its source is handed to the
  // worklet through a Blob URL built from toString(), so no extra file has to be served next
  // to soh.js. This function is never called on the main thread, where AudioWorkletProcessor
  // does not exist.
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
        this.underruns = 0; // blocks filled with silence
        this.blocks = 0;
        this.port.onmessage = (e) => this.enqueue(e.data);
      }

      enqueue(samples) {
        const channels = this.channels;
        let frames = Math.floor(samples.length / channels);
        const room = this.capacity - (this.written - Math.floor(this.readPos));
        if (frames > room) {
          frames = Math.max(0, room); // the main thread caps well below this; drop the tail
        }
        for (let i = 0; i < frames; i++) {
          const w = (this.written + i) % this.capacity;
          for (let c = 0; c < channels; c++) {
            this.ring[c][w] = samples[i * channels + c] / 32768;
          }
        }
        this.written += frames;
      }

      report() {
        this.port.postMessage({ consumed: Math.floor(this.readPos), underruns: this.underruns });
      }

      process(inputs, outputs) {
        const out = outputs[0];
        const n = out[0].length;
        const step = this.step;
        const need = Math.ceil(n * step) + 2;
        if (this.written - Math.floor(this.readPos) < need) {
          // Starved: a whole block of silence rather than a partial one, and no advance, so
          // the queued audio plays intact once the main thread catches up.
          for (let c = 0; c < out.length; c++) {
            out[c].fill(0);
          }
          this.underruns++;
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

  lus_webaudio_init__deps: ['$lus_webaudio_processor'],
  lus_webaudio_init: function (rate, channels, capacity) {
    const Ctx = typeof AudioContext !== 'undefined' ? AudioContext
              : (typeof webkitAudioContext !== 'undefined' ? webkitAudioContext : null);
    if (!Ctx) {
      return 0;
    }
    let ctx = null;
    try {
      // Ask for the game's rate so the browser resamples; the worklet copes if it is ignored.
      ctx = new Ctx({ sampleRate: rate });
    } catch (e) {
      try {
        ctx = new Ctx();
      } catch (e2) {
        return 0;
      }
    }
    if (!ctx.audioWorklet || typeof AudioWorkletNode === 'undefined') {
      ctx.close();
      return 0;
    }
    const state = {
      ctx: ctx, node: null, url: null, pending: [],
      queued: 0, consumed: 0, underruns: 0,
      channels: channels, closed: false, failed: false,
    };
    Module.LUSWebAudio = state;

    const source = '(' + lus_webaudio_processor.toString() + ')();';
    state.url = URL.createObjectURL(new Blob([source], { type: 'application/javascript' }));
    ctx.audioWorklet.addModule(state.url).then(() => {
      if (state.closed) {
        return;
      }
      const node = new AudioWorkletNode(ctx, 'lus-audio', {
        numberOfInputs: 0,
        numberOfOutputs: 1,
        outputChannelCount: [channels],
        processorOptions: { channels: channels, capacity: capacity, sourceRate: rate },
      });
      node.port.onmessage = (e) => {
        state.consumed = e.data.consumed;
        state.underruns = e.data.underruns;
      };
      node.connect(ctx.destination);
      state.node = node;
      // Updates played before the worklet was ready, in order.
      const pending = state.pending;
      state.pending = [];
      for (const samples of pending) {
        node.port.postMessage(samples, [samples.buffer]);
      }
    }).catch((e) => {
      console.error('Web Audio: the worklet could not be loaded: ' + e);
      state.failed = true;
    });

    // Autoplay policy: the context stays suspended until the page has had a user gesture. A
    // gamepad button does not count, so also poll userActivation, which any earlier gesture
    // on the page satisfies. Safari suspends a hidden tab's context too.
    const gestures = ['keydown', 'pointerdown', 'mousedown', 'touchend'];
    const resume = () => {
      if (!state.closed && ctx.state !== 'running') {
        ctx.resume();
      }
    };
    const stopListening = () => {
      for (const name of gestures) {
        window.removeEventListener(name, resume, true);
      }
    };
    for (const name of gestures) {
      window.addEventListener(name, resume, true);
    }
    ctx.onstatechange = () => {
      if (ctx.state === 'running') {
        stopListening();
      }
    };
    const poll = setInterval(() => {
      if (state.closed || ctx.state === 'running') {
        clearInterval(poll);
        return;
      }
      if (navigator.userActivation && navigator.userActivation.hasBeenActive) {
        resume();
      }
    }, 250);
    document.addEventListener('visibilitychange', () => {
      if (!document.hidden) {
        resume();
      }
    });
    return 1;
  },

  lus_webaudio_close: function () {
    const state = Module.LUSWebAudio;
    if (!state) {
      return;
    }
    state.closed = true;
    if (state.node) {
      state.node.port.onmessage = null;
      state.node.disconnect();
    }
    if (state.url) {
      URL.revokeObjectURL(state.url);
    }
    state.ctx.close();
    Module.LUSWebAudio = null;
  },

  lus_webaudio_buffered: function () {
    const state = Module.LUSWebAudio;
    return state ? Math.max(0, state.queued - state.consumed) : 0;
  },

  // ptr/len: interleaved int16 frames in the heap. Copied out (the heap cannot be
  // transferred) and posted with the copy transferred. maxQueued: frames beyond which the
  // update is discarded instead -- see the C++ side.
  lus_webaudio_play: function (ptr, len, maxQueued) {
    const state = Module.LUSWebAudio;
    if (!state || state.closed) {
      return;
    }
    if (state.queued - state.consumed >= maxQueued) {
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
