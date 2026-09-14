#ifdef __EMSCRIPTEN__
#include "ship/audio/WebAudioAudioPlayer.h"
#include <spdlog/spdlog.h>

// Implemented in WebAudioAudioPlayer.js, linked as an Emscripten JS library.
extern "C" {
int lus_webaudio_init(int sampleRate, int channels, int ringCapacityFrames);
void lus_webaudio_close(void);
int lus_webaudio_buffered(void);
void lus_webaudio_play(const uint8_t* buf, size_t len, int maxQueuedFrames);
}

namespace Ship {

// Frames the worklet's ring buffer can hold: half a second at 32 kHz, well above the point at
// which DoPlay stops accepting updates.
static const int32_t kRingCapacityFrames = 16384;

// Frames queued but not yet consumed beyond which an update is discarded rather than queued.
// The game only tops the queue up while it is below its target, so this is only reached when
// nothing is consuming: an AudioContext still suspended for want of a user gesture, or a
// hidden tab. Matches SDLAudioPlayer::DoPlay, so the game's tuning applies to both players.
static const int32_t kMaxQueuedFrames = 6000;

WebAudioAudioPlayer::~WebAudioAudioPlayer() {
    SPDLOG_TRACE("destruct Web Audio player");
    DoClose();
}

bool WebAudioAudioPlayer::DoInit() {
    mNumChannels = this->GetNumOutputChannels();
    if (!lus_webaudio_init(this->GetSampleRate(), mNumChannels, kRingCapacityFrames)) {
        SPDLOG_ERROR("Web Audio: no AudioContext with AudioWorklet support in this browser");
        return false;
    }
    SPDLOG_INFO("Web Audio initialized: {} channels, {} Hz requested", mNumChannels, this->GetSampleRate());
    return true;
}

void WebAudioAudioPlayer::DoClose() {
    lus_webaudio_close();
}

int32_t WebAudioAudioPlayer::Buffered() {
    return lus_webaudio_buffered();
}

void WebAudioAudioPlayer::DoPlay(const uint8_t* buf, size_t len) {
    lus_webaudio_play(buf, len, kMaxQueuedFrames);
}

} // namespace Ship
#endif // __EMSCRIPTEN__
