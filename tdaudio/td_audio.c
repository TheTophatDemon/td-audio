#include "./td_audio.h"
#include <stdio.h>
#include <assert.h>

#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_MP3
#define MA_NO_FLAC
#define MA_NO_ENCODING
#include "miniaudio.h"

#define MAX($a, $b) (($a > $b) ? $a : $b)

#define ARRAY_PUSH($array, $type, $item) do {                                                        \
    ++$array.length;                                                                                 \
    if ($array.length >= $array.capacity) {                                                          \
        $array.capacity = MAX($array.capacity, 1) * 2;                                               \
        $array.items = ($type *)realloc($array.items, $array.capacity * sizeof($type));              \
    }                                                                                                \
    $array.items[$array.length-1] = ($item);                                                         \
} while(0)

#define LOG_ERR($message, ...) fprintf(stderr, "error occurred in " __FILE__ ":%s:%d " $message "\n", __func__, __LINE__, __VA_ARGS__)

typedef struct td_player {
    uint8_t num_voices;
    ma_sound *voices;
} td_player;

typedef struct td_player_list {
    uint32_t length;
    uint32_t capacity;
    td_player *items;
} td_player_list;

static ma_device g_device;
static ma_resource_manager g_resource_manager;
static ma_engine g_engine;
//TODO: Add sound group for sfx and music
static td_player_list g_players;
//TODO: Add music player(s)

static void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount)
{
    (void) pInput;

    ma_engine_read_pcm_frames(&g_engine, pOutput, frameCount, NULL);
}

static void teardown_sounds() {
    for (int s = 0; s < g_players.length; ++s) {
        td_player *player = &g_players.items[s];
        for (int v = 0; v < player->num_voices; ++v) {
            ma_sound_uninit(&player->voices[v]);
        }
        free(player->voices);
    }
    g_players.length = g_players.capacity = 0;
    free(g_players.items);
}

static bool check_voice_id(td_voice_id voice) {
    if (voice.sound_id.id == 0 || 
        voice.sound_id.id >= g_players.length ||
        voice.voice_id >= g_players.items[voice.sound_id.id].num_voices) {
        return false;
    }
    return true;
}

td_sound_id td_audio_load_sound(const char *path, uint8_t polyphony, bool looping, float rolloff) {
    assert(path != NULL);
    assert(polyphony > 0);

    td_player player = (td_player){
        .num_voices = polyphony,
        .voices = (ma_sound *) malloc(sizeof(ma_sound) * polyphony),
    };
    td_sound_id new_id = (td_sound_id){.id = g_players.length};
    ARRAY_PUSH(g_players, td_player, player);

    // Set up voices
    int v = 0;
    for (v = 0; v < polyphony; ++v) {
        ma_sound *voice = &player.voices[v];
        ma_uint32 flags = MA_SOUND_FLAG_DECODE;
        ma_result result = ma_sound_init_from_file(&g_engine, path, flags, NULL, NULL, voice);
        if (result != MA_SUCCESS) {
            LOG_ERR("failed to load sound at %s, code %d", path, result);
            goto fail;
        }
        ma_sound_set_looping(voice, (ma_bool32) looping);
        ma_sound_set_rolloff(voice, rolloff);
    }
    return new_id;
fail:
    for (v -= 1; v > 0; --v) {
        ma_sound_uninit(&player.voices[v]);
    }
    --g_players.length;
    free(player.voices);
    return (td_sound_id){0};
}

bool td_audio_sound_is_looped(td_sound_id sound) {
    if (sound.id >= g_players.length) return false;
    td_player *player = &g_players.items[sound.id];
    if (player->num_voices == 0) return false;
    return ma_sound_is_looping(&player->voices[0]);
}

bool td_audio_init() {
    ma_result result = MA_SUCCESS;

    // Resource manager init
    {
        ma_resource_manager_config config = ma_resource_manager_config_init();
        config.decodedFormat = ma_format_f32;
        config.decodedChannels = N_CHANNELS;
        config.decodedSampleRate = SAMPLE_RATE;
        if ((result = ma_resource_manager_init(&config, &g_resource_manager)) != MA_SUCCESS) {
            LOG_ERR("failed to initialize mini audio resource manager, code %d", result);
            return false;
        }
    }

    // Device init
    {
        ma_device_config config = ma_device_config_init(ma_device_type_playback);
        config.playback.format = ma_format_f32;
        config.playback.channels = N_CHANNELS;
        config.sampleRate = SAMPLE_RATE;
        config.dataCallback = data_callback;
        if ((result = ma_device_init(NULL, &config, &g_device)) != MA_SUCCESS) {
            LOG_ERR("failed to initialize audio system, code %d", result);
            return false;
        }  
    }

    // Engine init
    {
        ma_engine_config config = ma_engine_config_init();
        config.pDevice = &g_device;
        config.pResourceManager = &g_resource_manager;

        if ((result = ma_engine_init(&config, &g_engine)) != MA_SUCCESS) {
            LOG_ERR("failed to initialize miniaudio engine, code %d", result);
            return false;
        }
    }

    // Start the device
    if ((result = ma_device_start(&g_device)) != MA_SUCCESS) {
        LOG_ERR("failed to start playback device, code %d", result);
        return false;
    }

    g_players = (td_player_list) {0};

    return true;
}

/// Attempts to play the sound using one of the available voices. Returns a zeroed voice ID if sound was not played.
/// `x`, `y`, and `z` are the spatial coordinates of the sound in world space. If `attenuated` is false, then those parameters don't do anything.
td_voice_id td_audio_play_sound(td_sound_id sound_id, float x, float y, float z, bool attenuated) {
    assert(sound_id.id < g_players.length);

    td_player *player = &g_players.items[sound_id.id];

    ma_sound *chosen_voice = NULL;
    int chosen_voice_id = -1;
    for (int v = 0; v < player->num_voices; ++v) {
        ma_sound *voice = &player->voices[v];
        if (!ma_sound_is_playing(voice)) {
            chosen_voice = voice;
            chosen_voice_id = v;
            break;
        }
        ma_vec3f listener_pos = ma_engine_listener_get_position(&g_engine, 0);
        float chosen_voice_dist_from_listener = ma_vec3f_len2(ma_vec3f_sub(listener_pos, ma_sound_get_position(chosen_voice)));
        float voice_dist_from_listener = ma_vec3f_len2(ma_vec3f_sub(listener_pos, ma_sound_get_position(voice)));
        if (chosen_voice == NULL || 
            chosen_voice_dist_from_listener < voice_dist_from_listener ||
            (chosen_voice_dist_from_listener == voice_dist_from_listener && ma_sound_get_time_in_milliseconds(chosen_voice) < ma_sound_get_time_in_milliseconds(voice))
        ) {
            // Overwrite the oldest, most distant sound.
            chosen_voice = voice;
            chosen_voice_id = v;
        }
    }

    if (chosen_voice != NULL && chosen_voice_id > -1) {
        ma_result result = ma_sound_seek_to_pcm_frame(chosen_voice, 0);
        if (result != MA_SUCCESS) LOG_ERR("failed to seek sound with id %d, code %d", sound_id.id, result);

        ma_sound_set_spatialization_enabled(chosen_voice, attenuated);
        if (attenuated) {
            ma_sound_set_position(chosen_voice, x, y, z);
        }

        result = ma_sound_start(chosen_voice);
        if (result != MA_SUCCESS) LOG_ERR("failed to start sound with id %d, code %d", sound_id.id, result);
        
        return (td_voice_id) {
            .sound_id = sound_id,
            .voice_id = (uint32_t)chosen_voice_id,
        };
    }
    return (td_voice_id) {0};
}

bool td_audio_sound_is_playing(td_voice_id voice) {
    if (!check_voice_id(voice)) return false;
    td_player *player = &g_players.items[voice.sound_id.id];
    return (bool)ma_sound_is_playing(&player->voices[voice.voice_id]);
}

void td_audio_set_sound_position(td_voice_id voice, float x, float y, float z) {
    if (!check_voice_id(voice)) return;
    td_player *player = &g_players.items[voice.sound_id.id];
    ma_sound *ma_player = &player->voices[voice.voice_id];
    if (!ma_sound_is_spatialization_enabled(ma_player)) return;
    ma_sound_set_position(ma_player, x, y, z);
}

void td_audio_stop_sound(td_voice_id voice) {
    if (!check_voice_id(voice)) return;
    td_player *player = &g_players.items[voice.sound_id.id];
    ma_sound_stop(&player->voices[voice.voice_id]);
}

void td_audio_set_listener_orientation(float pos_x, float pos_y, float pos_z, float dir_x, float dir_y, float dir_z) {
    ma_engine_listener_set_position(&g_engine, 0, pos_x, pos_y, pos_z);
    ma_engine_listener_set_direction(&g_engine, 0, dir_x, dir_y, dir_z);
}

void td_audio_teardown() {
    teardown_sounds();
    ma_engine_uninit(&g_engine);
    ma_device_uninit(&g_device);
    ma_resource_manager_uninit(&g_resource_manager);
}