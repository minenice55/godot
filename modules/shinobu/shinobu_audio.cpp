/*
*/

#include "shinobu_audio.h"

#define MA_NO_VORBIS    /* Disable the built-in Vorbis decoder to ensure the libvorbis decoder is picked. */
#define MA_NO_OPUS      /* Disable the (not yet implemented) built-in Opus decoder to ensure the libopus decoder is picked. */
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio/miniaudio.h"
#include "miniaudio/extras/miniaudio_libvorbis.h"

#include "shinobu_sound_data.h"

static ma_result ma_decoding_backend_init__libvorbis(void* pUserData, ma_read_proc onRead, ma_seek_proc onSeek, ma_tell_proc onTell, void* pReadSeekTellUserData, const ma_decoding_backend_config* pConfig, const ma_allocation_callbacks* pAllocationCallbacks, ma_data_source** ppBackend)
{
    ma_result result;
    ma_libvorbis* pVorbis;

    (void)pUserData;

    pVorbis = (ma_libvorbis*)ma_malloc(sizeof(*pVorbis), pAllocationCallbacks);
    if (pVorbis == NULL) {
        return MA_OUT_OF_MEMORY;
    }

    result = ma_libvorbis_init(onRead, onSeek, onTell, pReadSeekTellUserData, pConfig, pAllocationCallbacks, pVorbis);
    if (result != MA_SUCCESS) {
        ma_free(pVorbis, pAllocationCallbacks);
        return result;
    }

    *ppBackend = pVorbis;

    return MA_SUCCESS;
}

static ma_result ma_decoding_backend_init_file__libvorbis(void* pUserData, const char* pFilePath, const ma_decoding_backend_config* pConfig, const ma_allocation_callbacks* pAllocationCallbacks, ma_data_source** ppBackend)
{
    ma_result result;
    ma_libvorbis* pVorbis;

    (void)pUserData;

    pVorbis = (ma_libvorbis*)ma_malloc(sizeof(*pVorbis), pAllocationCallbacks);
    if (pVorbis == NULL) {
        return MA_OUT_OF_MEMORY;
    }

    result = ma_libvorbis_init_file(pFilePath, pConfig, pAllocationCallbacks, pVorbis);
    if (result != MA_SUCCESS) {
        ma_free(pVorbis, pAllocationCallbacks);
        return result;
    }

    *ppBackend = pVorbis;

    return MA_SUCCESS;
}

static void ma_decoding_backend_uninit__libvorbis(void* pUserData, ma_data_source* pBackend, const ma_allocation_callbacks* pAllocationCallbacks)
{
    ma_libvorbis* pVorbis = (ma_libvorbis*)pBackend;

    (void)pUserData;

    ma_libvorbis_uninit(pVorbis, pAllocationCallbacks);
    ma_free(pVorbis, pAllocationCallbacks);
}

static ma_result ma_decoding_backend_get_channel_map__libvorbis(void* pUserData, ma_data_source* pBackend, ma_channel* pChannelMap, size_t channelMapCap)
{
    ma_libvorbis* pVorbis = (ma_libvorbis*)pBackend;

    (void)pUserData;

    return ma_libvorbis_get_data_format(pVorbis, NULL, NULL, NULL, pChannelMap, channelMapCap);
}

static ma_decoding_backend_vtable g_ma_decoding_backend_vtable_libvorbis =
{
    ma_decoding_backend_init__libvorbis,
    ma_decoding_backend_init_file__libvorbis,
    NULL, /* onInitFileW() */
    NULL, /* onInitMemory() */
    ma_decoding_backend_uninit__libvorbis
};

SH_RESULT ShinobuAudio::initialize() {
    ma_result result;
    is_initialized = true;
    ma_device_config device_config = ma_device_config_init(ma_device_type_playback);
    device_config.pUserData = this;
    device_config.dataCallback = ma_data_callback;
    device_config.playback.format = ma_format_f32;
    device_config.performanceProfile = ma_performance_profile_low_latency;
    device_config.noFixedSizedCallback = MA_TRUE;
    device_config.periodSizeInMilliseconds = buffer_size;
    // This is necessary to enable WASAPI low latency mode
    device_config.wasapi.noAutoConvertSRC = true;

    result = ma_device_init(NULL, &device_config, device);
    if (result != MA_SUCCESS) {
        printf("Device init failed\n");
        return result;
    }

    ma_engine_config engine_config = ma_engine_config_init();
    engine_config.pDevice = device;
    engine_config.periodSizeInMilliseconds = buffer_size;

    // Setup libvorbis

    ma_resource_manager_config resourceManagerConfig;

    /*
    Custom backend vtables
    */
    ma_decoding_backend_vtable* pCustomBackendVTables[] =
    {
        &g_ma_decoding_backend_vtable_libvorbis
    };

    /* Using custom decoding backends requires a resource manager. */
    resourceManagerConfig = ma_resource_manager_config_init();
    resourceManagerConfig.ppCustomDecodingBackendVTables = pCustomBackendVTables;
    resourceManagerConfig.customDecodingBackendCount     = sizeof(pCustomBackendVTables) / sizeof(pCustomBackendVTables[0]);
    resourceManagerConfig.pCustomDecodingBackendUserData = NULL;
    resourceManagerConfig.decodedFormat = ma_format_f32;

    // HACK: Dry run the engine to figure out the correct sample rate for the source manager

    ma_engine_init(&engine_config, engine);
    resourceManagerConfig.decodedSampleRate = ma_engine_get_sample_rate(engine);
    ma_engine_uninit(engine);




    result = ma_resource_manager_init(&resourceManagerConfig, resource_manager);

    if (result != MA_SUCCESS) {
        printf("Init resource manager failed!\n");
    }


    engine_config.pResourceManager = resource_manager;

    result = ma_engine_init(&engine_config, engine);

    if (result != MA_SUCCESS) {
        printf("Engine init failed\n");
        return result;
    }

    return MA_SUCCESS;
}

uint64_t ShinobuAudio::get_dsp_time() const {
    return ma_engine_get_time(engine) / (ma_engine_get_sample_rate(engine) / 1000);
}

SH_RESULT ShinobuAudio::register_sound_from_memory(std::string name, const void* data, size_t size) {
    // Ensure that if there is a song data with the same name that it is destroyed before replacing it
    sound_datas.erase(name);
    std::unique_ptr<ShinobuSoundSourceMemory> temp = std::make_unique<ShinobuSoundSourceMemory>(engine, name.c_str(), data, size);
    SH_RESULT result = temp->get_result();
    if (result == MA_SUCCESS) {
        sound_datas.emplace(name, std::move(temp));
    }
    return result;
}

SH_RESULT ShinobuAudio::register_group(std::string name) {
    std::unique_ptr<ShinobuSoundGroup> group = std::make_unique<ShinobuSoundGroup>(engine, name);
    SH_RESULT result = group->get_result();
    if (result == MA_SUCCESS) {
        sound_groups.emplace(name, std::move(group));
    }
    return result;
}

void ShinobuAudio::unregister_sound(std::string name) {
    sound_datas.erase(name);
}

ShinobuSoundGroup* ShinobuAudio::get_group(std::string name) {
    ShinobuSoundGroup* out = NULL;
    if (!name.empty()) {
        if (sound_groups.find(name) != sound_groups.end()) {
            out = sound_groups.at(name).get();
        }
    }

    return out;
}

ma_sound_group* ShinobuAudio::get_ma_group(std::string name) {
    ShinobuSoundGroup* group = get_group(name);
    ma_sound_group *ma_group = NULL;
    if (group != NULL) {
        ma_group = group->get_sound_group();
    }
    return ma_group;
}

ShinobuSoundSource* ShinobuAudio::get_sound_source(std::string sound_name) {
    ShinobuSoundSource* out = NULL;
    if (!sound_name.empty()) {
        if (sound_datas.find(sound_name) != sound_datas.end()) {
            out = sound_datas.at(sound_name).get();
        }
    }

    return out;
}

void ShinobuAudio::set_group_volume(std::string name, float linear_volume) {
    ShinobuSoundGroup* group = get_group(name);
    if (group != NULL) {
        group->set_volume(linear_volume);
    }
}

float ShinobuAudio::get_group_volume(std::string name) {
    ShinobuSoundGroup* group = get_group(name);
    float volume = 1.0f;
    if (group != NULL) {
        volume = group->get_volume();
    }
    return volume;
}

// Call this to play back a sound that you don't want to have to manage,
// such as note sfx, nya~
SH_RESULT ShinobuAudio::fire_and_forget_sound(std::string sound_name, std::string group_name) {
    return ma_engine_play_sound(engine, sound_name.c_str(), get_ma_group(group_name));
}

std::unique_ptr<ShinobuSoundPlayback> ShinobuAudio::instantiate_sound(std::string name, std::string group_name) {
    return std::make_unique<ShinobuSoundPlayback>(engine, name, get_ma_group(group_name));
}

SH_RESULT ShinobuAudio::set_master_volume(float linear_volume) {
    ma_result result = ma_engine_set_volume(engine, linear_volume);
    if (result == MA_SUCCESS) {
        master_volume = linear_volume;
    }
    return result;
}

float ShinobuAudio::get_master_volume() {
    return master_volume;
}

void ShinobuAudio::set_buffer_size(uint64_t new_buffer_size) {
    if (!is_initialized) {
        // Buffer size can only be set pre-initialization!
        buffer_size = new_buffer_size;
    }
}

uint64_t ShinobuAudio::get_buffer_size() const {
    return buffer_size;
}


void ShinobuAudio::ma_data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    ShinobuAudio* shinobu = (ShinobuAudio*)pDevice->pUserData;
    if (shinobu != NULL) {
        ma_engine_read_pcm_frames(shinobu->engine, pOutput, frameCount, NULL);
    }
}

ShinobuAudio::ShinobuAudio() {
    engine = new ma_engine;
    device = new ma_device;
    resource_manager = new ma_resource_manager;
}

ShinobuAudio::~ShinobuAudio() {
    ma_engine_uninit(engine);
    ma_device_uninit(device);
    ma_resource_manager_uninit(resource_manager);
    delete engine;
    delete device;
    delete resource_manager;
}
