#include <algorithm>
#include "ship/audio/Audio.h"

#ifdef __APPLE__
#include "ship/audio/CoreAudioAudioPlayer.h"
#endif

#include "ship/Context.h"
#include "ship/controller/controldeck/ControlDeck.h"

namespace Ship {

Audio::~Audio() {
    SPDLOG_TRACE("destruct audio");
}

void Audio::InitAudioPlayer() {
    switch (GetCurrentAudioBackend()) {
#ifdef _WIN32
        case AudioBackend::WASAPI:
            mAudioPlayer = std::make_shared<WasapiAudioPlayer>(this->mAudioSettings);
            break;
#endif
#ifdef __APPLE__
        case AudioBackend::COREAUDIO:
            mAudioPlayer = std::make_shared<CoreAudioAudioPlayer>(this->mAudioSettings);
            break;
#endif
#ifdef __EMSCRIPTEN__
        case AudioBackend::WEBAUDIO:
            mAudioPlayer = std::make_shared<WebAudioAudioPlayer>(this->mAudioSettings);
            break;
#endif
        case AudioBackend::SDL:
            mAudioPlayer = std::make_shared<SDLAudioPlayer>(this->mAudioSettings);
            break;
        default:
            mAudioPlayer = std::make_shared<NullAudioPlayer>(this->mAudioSettings);
            break;
    }

    if (mAudioPlayer && !mAudioPlayer->Init()) {
#ifdef __EMSCRIPTEN__
        // SOH [WASM] The Web Audio player needs AudioWorklet. A browser without it still has
        // SDL's ScriptProcessorNode path, which plays, if not as smoothly.
        if (GetCurrentAudioBackend() == AudioBackend::WEBAUDIO) {
            FallBackToSdl("no AudioContext with AudioWorklet in this browser");
            return;
        }
#endif
        // Failed to initialize system audio player.
        // Fallback to Null if the native system player does not work.
        SetCurrentAudioBackend(AudioBackend::NUL);
    }
}

// SOH [WASM] Unlike SetCurrentAudioBackend this does not save the choice: the failure may be
// one session's (a page whose CSP blocks blob: scripts, a browser refusing one more
// AudioContext), and a saved "sdl" would keep every later session on the main-thread player.
void Audio::FallBackToSdl(const char* why) {
    SPDLOG_WARN("Web Audio player unavailable ({}); falling back to SDL audio for this session", why);
    mAudioBackend = AudioBackend::SDL;
    InitAudioPlayer();
}

void Audio::Init() {
    mAvailableAudioBackends = std::make_shared<std::vector<AudioBackend>>();
#ifdef _WIN32
    mAvailableAudioBackends->push_back(AudioBackend::WASAPI);
#endif
#ifdef __APPLE__
    mAvailableAudioBackends->push_back(AudioBackend::COREAUDIO);
#endif
#ifdef __EMSCRIPTEN__
    // SOH [WASM] First, so it is the default: see WebAudioAudioPlayer.h for why SDL's
    // main-thread audio glitches on every long frame.
    mAvailableAudioBackends->push_back(AudioBackend::WEBAUDIO);
#endif
    mAvailableAudioBackends->push_back(AudioBackend::SDL);
    mAvailableAudioBackends->push_back(AudioBackend::NUL);

    // A config written on another platform can name a backend this build does not have:
    // "coreaudio" from a Mac install, opened on Windows or in a browser. InitAudioPlayer would
    // fall through to the null player and the game would run silent without a word, so use
    // this platform's preferred backend instead.
    AudioBackend backend = Context::GetInstance()->GetConfig()->GetCurrentAudioBackend();
    if (std::find(mAvailableAudioBackends->begin(), mAvailableAudioBackends->end(), backend) ==
        mAvailableAudioBackends->end()) {
        SPDLOG_WARN("The configured audio backend is not available in this build; using the default");
        backend = mAvailableAudioBackends->front();
    }
    SetCurrentAudioBackend(backend);
}

std::shared_ptr<AudioPlayer> Audio::GetAudioPlayer() {
#ifdef __EMSCRIPTEN__
    // SOH [WASM] The Web Audio player's worklet loads after Init returns; if that load failed
    // the player is silent for good. Every audio call comes through here, so this is where a
    // dead player gets replaced. The old one is released, which closes its context.
    if (mAudioPlayer && mAudioPlayer->HasFailed()) {
        FallBackToSdl("the AudioWorklet could not be loaded");
    }
#endif
    return mAudioPlayer;
}

AudioBackend Audio::GetCurrentAudioBackend() {
    return mAudioBackend;
}

void Audio::SetCurrentAudioBackend(AudioBackend backend) {
    mAudioBackend = backend;
    Context::GetInstance()->GetConfig()->SetCurrentAudioBackend(GetCurrentAudioBackend());
    Context::GetInstance()->GetConfig()->Save();

    InitAudioPlayer();
}

std::shared_ptr<std::vector<AudioBackend>> Audio::GetAvailableAudioBackends() {
    return mAvailableAudioBackends;
}

void Audio::SetAudioChannels(AudioChannelsSetting channels) {
    if (mAudioSettings.ChannelSetting != channels) {
        mAudioSettings.ChannelSetting = channels;
        // Reinitialize the existing audio player with the new channel configuration
        if (mAudioPlayer) {
            mAudioPlayer->SetAudioChannels(channels);
        }
    }
}

AudioChannelsSetting Audio::GetAudioChannels() const {
    return mAudioSettings.ChannelSetting;
}

} // namespace Ship
