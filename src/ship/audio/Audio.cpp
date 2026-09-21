#include "ship/audio/Audio.h"

#include <algorithm>

#ifdef __APPLE__
#include "ship/audio/CoreAudioAudioPlayer.h"
#endif

#include "ship/Context.h"
#include "ship/config/Config.h"
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
// 2S2H [WASM]
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
// 2S2H [WASM] The Web Audio player needs AudioWorklet. A browser without it still has SDL's
// ScriptProcessorNode path, which plays, if not as smoothly.
#ifdef __EMSCRIPTEN__
        if (GetCurrentAudioBackend() == AudioBackend::WEBAUDIO) {
            FallBackTo(AudioBackend::SDL, "no AudioContext with AudioWorklet in this browser");
            return;
        }
#endif
        // Failed to initialize system audio player. Fall back to Null if the native system
        // player does not work.
        FallBackTo(AudioBackend::NUL, "the audio device could not be opened");
    }
}

// Switches backend for this session only. Deliberately not SetCurrentAudioBackend, which saves:
// a device or a browser API that failed once is not a reason to write that choice down and
// silence -- or downgrade -- every later launch. A page whose CSP blocks blob: scripts, a
// browser refusing one more AudioContext, and a device busy at startup are all transient.
//
// Recursion is bounded: the caller is InitAudioPlayer, and the only backend this is ever given
// last is NUL, whose DoInit always succeeds.
void Audio::FallBackTo(AudioBackend backend, const char* why) {
    SPDLOG_WARN("Audio backend unavailable ({}); falling back for this session", why);
    mAudioBackend = backend;
    InitAudioPlayer();
}

#ifdef __EMSCRIPTEN__
// 2S2H [WASM] The Web Audio player's worklet loads after Init returns, so a player that
// reported success can still turn out to be silent for good. Releasing the old one closes its
// context.
void Audio::ReplaceFailedPlayer() {
    if (mAudioPlayer && mAudioPlayer->HasFailed()) {
        FallBackTo(AudioBackend::SDL, "the AudioWorklet could not be loaded");
    }
}
#endif

void Audio::Init() {
    mConfig = Context::GetRawInstance()->GetConfig();

    mAvailableAudioBackends = std::make_shared<std::vector<AudioBackend>>();
#ifdef _WIN32
    mAvailableAudioBackends->push_back(AudioBackend::WASAPI);
#endif
#ifdef __APPLE__
    mAvailableAudioBackends->push_back(AudioBackend::COREAUDIO);
#endif
// 2S2H [WASM] First, so it is both the browser default and the target the availability guard
// below corrects a stale desktop config to: see WebAudioAudioPlayer.h for why SDL's
// main-thread audio glitches on every long frame.
#ifdef __EMSCRIPTEN__
    mAvailableAudioBackends->push_back(AudioBackend::WEBAUDIO);
#endif
    mAvailableAudioBackends->push_back(AudioBackend::SDL);
    mAvailableAudioBackends->push_back(AudioBackend::NUL);

    // A config written on another platform can name a backend this build does not have:
    // "coreaudio" from a Mac install, opened on Windows or in a browser. InitAudioPlayer only
    // compiles its own platform's players, so it would fall through to the null player and the
    // game would run silent without a word. Use this platform's preferred backend instead --
    // mAvailableAudioBackends->front() by construction -- and let SetCurrentAudioBackend write
    // the correction back, so the stale config becomes one this build can play.
    AudioBackend backend = GetSavedAudioBackend();
    if (std::find(mAvailableAudioBackends->begin(), mAvailableAudioBackends->end(), backend) ==
        mAvailableAudioBackends->end()) {
        SPDLOG_WARN("The configured audio backend ({}) is not available in this build; using the default",
                    mConfig->GetString("Window.AudioBackend"));
        backend = mAvailableAudioBackends->front();
    }

    SetCurrentAudioBackend(backend);
    SetAudioChannels(GetSavedAudioChannelsSetting());
}

std::shared_ptr<AudioPlayer> Audio::GetAudioPlayer() {
// 2S2H [WASM] Every audio call comes through here, so this is where a dead player gets
// replaced -- the game's once-per-frame AudioPlayer_Buffered() is what polls it.
#ifdef __EMSCRIPTEN__
    ReplaceFailedPlayer();
#endif
    return mAudioPlayer;
}

AudioBackend Audio::GetCurrentAudioBackend() {
    return mAudioBackend;
}

AudioBackend Audio::GetSavedAudioBackend() {
    std::string backendName = mConfig->GetString("Window.AudioBackend");
    if (backendName == "wasapi") {
        return AudioBackend::WASAPI;
    }

    // Migrate pulse player in config to sdl
    if (backendName == "pulse") {
        mConfig->SetString("Window.AudioBackend", "sdl");
        mConfig->Save();
        return AudioBackend::SDL;
    }

    if (backendName == "coreaudio") {
        return AudioBackend::COREAUDIO;
    }

    if (backendName == "sdl") {
        return AudioBackend::SDL;
    }

    // 2S2H [WASM]
    if (backendName == "webaudio") {
        return AudioBackend::WEBAUDIO;
    }

    if (backendName == "null") {
        return AudioBackend::NUL;
    }

    SPDLOG_TRACE("Could not find AudioBackend matching value from config file ({}). Returning default AudioBackend.",
                 backendName);
#ifdef _WIN32
    return AudioBackend::WASAPI;
#endif

#ifdef __APPLE__
    return AudioBackend::COREAUDIO;
#endif

// 2S2H [WASM] The AudioWorklet player; SDL's Emscripten player runs on the main thread.
#ifdef __EMSCRIPTEN__
    return AudioBackend::WEBAUDIO;
#endif

    return AudioBackend::SDL;
}

void Audio::SetCurrentAudioBackend(AudioBackend backend) {
    mAudioBackend = backend;

    switch (backend) {
        case AudioBackend::WASAPI:
            mConfig->SetString("Window.AudioBackend", "wasapi");
            break;
        case AudioBackend::COREAUDIO:
            mConfig->SetString("Window.AudioBackend", "coreaudio");
            break;
        case AudioBackend::SDL:
            mConfig->SetString("Window.AudioBackend", "sdl");
            break;
        // 2S2H [WASM]
        case AudioBackend::WEBAUDIO:
            mConfig->SetString("Window.AudioBackend", "webaudio");
            break;
        case AudioBackend::NUL:
            mConfig->SetString("Window.AudioBackend", "null");
            break;
        default:
            mConfig->SetString("Window.AudioBackend", "");
    }
    mConfig->Save();

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

AudioChannelsSetting Audio::GetSavedAudioChannelsSetting() {
    int32_t channelsSetting =
        mConfig->GetInt("CVars." CVAR_AUDIO_CHANNELS_SETTING, static_cast<int32_t>(AudioChannelsSetting::audioMax));
    switch (channelsSetting) {
        case AudioChannelsSetting::audioMatrix51:
            return AudioChannelsSetting::audioMatrix51;
        case AudioChannelsSetting::audioRaw51:
            return AudioChannelsSetting::audioRaw51;
        case AudioChannelsSetting::audioStereo:
        case AudioChannelsSetting::audioMax:
        default:
            return AudioChannelsSetting::audioStereo;
    }
}

} // namespace Ship
