#ifndef SHINOBU_AUDIO_H
#define SHINOBU_AUDIO_H

#include "miniaudio/miniaudio.h"
#include "shinobu_sound_data.h"
#include <cstring>
#include <string>
#include <map>
#include <memory>

#define SH_RESULT ma_result
#define SH_SUCCESS MA_SUCCESS

class ShinobuSoundGroup {
    ma_sound_group *sound_group;
    SH_RESULT result;
    std::string name;
public:
    ShinobuSoundGroup(ma_engine *engine, std::string name)
    : name(name) {
        sound_group = new ma_sound_group;
        result = ma_sound_group_init(engine, NULL, NULL, sound_group);
    }

    SH_RESULT get_result() {
        return result;
    }
    ma_sound_group* get_sound_group() {
        return sound_group;
    }

    void set_volume(float linear_volume) {
        ma_sound_group_set_volume(sound_group, linear_volume);
    }

    const float get_volume() {
        return ma_sound_group_get_volume(sound_group);
    }

    ~ShinobuSoundGroup() {
        ma_sound_group_uninit(sound_group);
        delete sound_group;
    }
};

class ShinobuSoundPlayback {
    ma_sound *sound;
    ma_engine *engine;
    SH_RESULT result;
    uint64_t start_time_msec = 0;
    uint64_t cached_length = -1;
    bool looping = false;
public:
    ShinobuSoundPlayback(ma_engine *engine, std::string name, ma_sound_group *sound_group)
    : engine(engine) {
        sound = new ma_sound;
        result = ma_sound_init_from_file(engine, name.c_str(), 0, sound_group, NULL, sound);
    }
    
    ~ShinobuSoundPlayback() {
        ma_sound_uninit(sound);
        delete sound;
    }
    
    SH_RESULT get_result() {
        return result;
    }

    SH_RESULT seek(uint64_t to_time_msec) {
        // Sound MUST be stopped before seeking or we crash
        if (ma_sound_is_playing(sound) == MA_TRUE) {
            ma_sound_stop(sound);
        }
        uint32_t sample_rate;
        ma_sound_get_data_format(sound, NULL, NULL, &sample_rate, NULL, 0);
        return ma_sound_seek_to_pcm_frame(sound, to_time_msec * (sample_rate / 1000));
    }

    bool is_playing() {
        return (bool)ma_sound_is_playing(sound);
    }
    
    SH_RESULT start() {
        return ma_sound_start(sound);
    }
    
    SH_RESULT stop() {
        return ma_sound_stop(sound);
    }

    void set_pitch_scale(float pitch_scale) {
        ma_sound_set_pitch(sound, pitch_scale);
    }

    void schedule_start_time(uint64_t global_time_msec) {
        start_time_msec = global_time_msec;
        ma_sound_set_start_time_in_milliseconds(sound, global_time_msec);
    }

    void schedule_stop_time(uint64_t global_time_msec) {
        ma_sound_set_stop_time_in_milliseconds(sound, global_time_msec);
    }

    uint64_t get_playback_position_msec() {

        
        ma_uint64 pos_frames;
        result = ma_sound_get_cursor_in_pcm_frames(sound, &pos_frames);
        uint64_t out_pos = 0;
        uint32_t sample_rate;
        ma_sound_get_data_format(sound, NULL, NULL, &sample_rate, NULL, 0);
        if (result == SH_SUCCESS) {
           out_pos = pos_frames / (sample_rate / 1000);
        }
        
        // This allows the return of negative playback time
        uint64_t dsp_time = ma_engine_get_time(engine) / (ma_engine_get_sample_rate(engine) / 1000);

        if (!is_playing() && start_time_msec > dsp_time) {
            return dsp_time - start_time_msec + out_pos;
        }

        return out_pos;
    }

    uint64_t get_length_msec() {
        if (cached_length != -1) {
            return cached_length;
        }

        ma_uint64 p_length = 0;
        ma_sound_get_length_in_pcm_frames(sound, &p_length);
        uint32_t sample_rate;
        ma_sound_get_data_format(sound, NULL, NULL, &sample_rate, NULL, 0);
        p_length /= sample_rate / 1000;
        cached_length = p_length;

        return p_length;
    }

    void set_volume(float linear_volume) {
        ma_sound_set_volume(sound, linear_volume);
    }

    float get_volume() const {
        return ma_sound_get_volume(sound);
    }

    void set_looping_enabled(bool m_looping) {
        looping = m_looping;
        ma_sound_set_looping(sound, (ma_bool32)m_looping);
    }

    bool get_looping_enabled() const {
        return looping;
    }

};
class ShinobuAudio {
private:
    std::map<std::string, std::unique_ptr<ShinobuSoundSource>> sound_datas;
    std::map<std::string, std::unique_ptr<ShinobuSoundGroup>> sound_groups;
    ma_engine *engine;
    ma_device *device;
    ma_resource_manager *resource_manager;
    uint64_t buffer_size = 10;
    bool is_initialized = false;

    float master_volume = 1.0f;
    static void ma_data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount);
    ShinobuSoundGroup* get_group(std::string name);
    ma_sound_group* get_ma_group(std::string name);
    ShinobuSoundSource* get_sound_source(std::string name);
public:
    uint64_t get_dsp_time() const;

    SH_RESULT register_sound_from_memory(std::string name, const void* data, size_t size);
    SH_RESULT register_group(std::string name);
    void unregister_sound(std::string name);

    SH_RESULT fire_and_forget_sound(std::string sound_name, std::string group_name);
    std::unique_ptr<ShinobuSoundPlayback> instantiate_sound(std::string name, std::string group_name);
    SH_RESULT initialize();

    void set_group_volume(std::string name, float linear_volume);
    float get_group_volume(std::string name);

    SH_RESULT set_master_volume(float linear_volume);
    float get_master_volume();

    uint64_t get_buffer_size() const;
    void set_buffer_size(uint64_t new_buffer_size);

    ShinobuAudio();
    ~ShinobuAudio();
};

#endif