#include "player.h"
#include "../platform/log.h"
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <mmreg.h>

/* Define GUIDs as static const */
static const CLSID CLSID_MMDeviceEnumerator_local = {0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
static const IID IID_IMMDeviceEnumerator_local = {0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
static const IID IID_IAudioClient_local = {0x1CB9AD4C, 0xDBFA, 0x4c32, {0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2}};
static const IID IID_IAudioRenderClient_local = {0xF294ACFC, 0x3146, 0x4483, {0xA7, 0xBF, 0xAD, 0xDC, 0xA7, 0xC2, 0x60, 0xE2}};
#include <stdlib.h>
#include <string.h>

struct audio_player {
    IAudioClient *client;
    IAudioRenderClient *render_client;
    uint32_t buffer_frames;
    uint32_t sample_rate;
    uint32_t channels;
};

audio_player_t *audio_player_create(void) {
    return calloc(1, sizeof(audio_player_t));
}

bool audio_player_init(audio_player_t *player, uint32_t sample_rate,
                        uint32_t channels) {
    player->sample_rate = sample_rate;
    player->channels = channels;

    HRESULT hr;
    IMMDeviceEnumerator *enumerator = NULL;
    IMMDevice *device = NULL;

    hr = CoCreateInstance(&CLSID_MMDeviceEnumerator_local, NULL, CLSCTX_ALL,
                          &IID_IMMDeviceEnumerator_local, (void **)&enumerator);
    if (FAILED(hr)) {
        log_error("Failed to create device enumerator: 0x%08x", hr);
        return false;
    }

    hr = enumerator->lpVtbl->GetDefaultAudioEndpoint(enumerator, eRender,
                                                      eConsole, &device);
    enumerator->lpVtbl->Release(enumerator);
    if (FAILED(hr)) {
        log_error("Failed to get default audio endpoint: 0x%08x", hr);
        return false;
    }

    hr = device->lpVtbl->Activate(device, &IID_IAudioClient_local, CLSCTX_ALL,
                                   NULL, (void **)&player->client);
    device->lpVtbl->Release(device);
    if (FAILED(hr)) {
        log_error("Failed to activate audio client: 0x%08x", hr);
        return false;
    }

    WAVEFORMATEX wfx = {0};
    wfx.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
    wfx.nChannels = (WORD)channels;
    wfx.nSamplesPerSec = sample_rate;
    wfx.wBitsPerSample = 32;
    wfx.nBlockAlign = (WORD)(channels * 4);
    wfx.nAvgBytesPerSec = sample_rate * wfx.nBlockAlign;

    REFERENCE_TIME duration = 10000000;
    hr = player->client->lpVtbl->Initialize(player->client,
                                             AUDCLNT_SHAREMODE_SHARED,
                                             0, duration, 0, &wfx, NULL);
    if (FAILED(hr)) {
        log_error("Failed to initialize audio client: 0x%08x", hr);
        return false;
    }

    hr = player->client->lpVtbl->GetService(player->client,
                                             &IID_IAudioRenderClient_local,
                                             (void **)&player->render_client);
    if (FAILED(hr)) {
        log_error("Failed to get render client: 0x%08x", hr);
        return false;
    }

    UINT32 buffer_size;
    player->client->lpVtbl->GetBufferSize(player->client, &buffer_size);
    player->buffer_frames = buffer_size;

    player->client->lpVtbl->Start(player->client);

    log_info("Audio player initialized: %u Hz, %u channels", sample_rate, channels);
    return true;
}

bool audio_player_write(audio_player_t *player, const uint8_t *data,
                         uint32_t size) {
    if (!player->render_client) return false;

    UINT32 padding;
    player->client->lpVtbl->GetCurrentPadding(player->client, &padding);

    UINT32 available = player->buffer_frames - padding;
    UINT32 frames = size / (player->channels * 4);
    if (frames > available) frames = available;
    if (frames == 0) return true;

    BYTE *buffer;
    HRESULT hr = player->render_client->lpVtbl->GetBuffer(
        player->render_client, frames, &buffer);
    if (FAILED(hr)) return false;

    memcpy(buffer, data, frames * player->channels * 4);

    player->render_client->lpVtbl->ReleaseBuffer(player->render_client, frames, 0);
    return true;
}

void audio_player_destroy(audio_player_t *player) {
    if (!player) return;

    if (player->client) {
        player->client->lpVtbl->Stop(player->client);
        player->client->lpVtbl->Release(player->client);
    }
    if (player->render_client)
        player->render_client->lpVtbl->Release(player->render_client);

    free(player);
}
