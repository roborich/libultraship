#pragma once
#include "AudioPlayer.h"

namespace Ship {
// SOH [WASM] Plays through a Web Audio AudioWorklet, so the callback that feeds the speaker
// runs on the browser's audio rendering thread rather than the page's main thread.
//
// SDL's Emscripten backend uses a ScriptProcessorNode, whose callback is a main-thread event:
// any frame longer than a callback interval or two (about 20-40 ms at 1024 frames) starves it
// and the output glitches, however much audio SDL has queued, because the code that moves
// samples from that queue to the device is the thing being starved. With the worklet the
// queue depth is real headroom: a long frame is covered by whatever is already queued.
//
// Single-threaded build, no SharedArrayBuffer: each update is copied out of the heap and
// posted to the worklet as a transferred Int16Array; the worklet posts back how many frames
// it has consumed, which is what Buffered() reports.
class WebAudioAudioPlayer final : public AudioPlayer {
  public:
    WebAudioAudioPlayer(AudioSettings settings) : AudioPlayer(settings) {
    }
    ~WebAudioAudioPlayer();

    int32_t Buffered() override;

  protected:
    bool DoInit() override;
    void DoClose() override;
    void DoPlay(const uint8_t* buf, size_t len) override;

  private:
    int32_t mNumChannels = 2;
};
} // namespace Ship
