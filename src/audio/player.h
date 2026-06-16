#ifndef AUDIO_PLAYER_H
#define AUDIO_PLAYER_H

#include <stdint.h>
#include <stdbool.h>

typedef struct audio_player audio_player_t;

audio_player_t *audio_player_create(void);
bool audio_player_init(audio_player_t *player, uint32_t sample_rate,
                        uint32_t channels);
bool audio_player_write(audio_player_t *player, const uint8_t *data,
                         uint32_t size);
void audio_player_destroy(audio_player_t *player);

#endif /* AUDIO_PLAYER_H */
