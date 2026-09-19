#pragma once

#include <string>
#include <memory>
#include <vector>
#include "ship/audio/AudioPlayer.h"

namespace Ship {
// SOH [WASM] WEBAUDIO is the browser build's AudioWorklet player.
enum class AudioBackend { WASAPI, SDL, COREAUDIO, WEBAUDIO, NUL };

class Audio {
  public:
    Audio(AudioSettings settings) : mAudioSettings(settings) {
    }
    ~Audio();

    void Init();
    std::shared_ptr<AudioPlayer> GetAudioPlayer();
    AudioBackend GetCurrentAudioBackend();
    std::shared_ptr<std::vector<AudioBackend>> GetAvailableAudioBackends();
    void SetCurrentAudioBackend(AudioBackend backend);

    // Set audio channels configuration and reinitialize audio player
    // This can be called at runtime without restarting the game
    void SetAudioChannels(AudioChannelsSetting channels);
    AudioChannelsSetting GetAudioChannels() const;

  protected:
    void InitAudioPlayer();
    // SOH [WASM] Switches to SDL without writing the choice to the config.
    void FallBackToSdl(const char* why);

  private:
    std::shared_ptr<AudioPlayer> mAudioPlayer;
    AudioBackend mAudioBackend;
    AudioSettings mAudioSettings;
    std::shared_ptr<std::vector<AudioBackend>> mAvailableAudioBackends;
};
} // namespace Ship
