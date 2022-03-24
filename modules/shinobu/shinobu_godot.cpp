#include "shinobu_godot.h"

ShinobuGodot *ShinobuGodot::singleton = nullptr;

void ShinobuGodot::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_dsp_time"), &ShinobuGodot::get_dsp_time);
	ClassDB::bind_method(D_METHOD("register_group", "group_name"), &ShinobuGodot::register_group);
	ClassDB::bind_method(D_METHOD("register_sound_from_path", "path", "sound_name"), &ShinobuGodot::register_sound_from_path);
	ClassDB::bind_method(D_METHOD("register_sound", "audio_file", "sound_name"), &ShinobuGodot::register_sound);
	ClassDB::bind_method(D_METHOD("unregister_sound", "sound_name"), &ShinobuGodot::unregister_sound);
	ClassDB::bind_method(D_METHOD("instantiate_sound", "sound_name", "group_name"), &ShinobuGodot::instantiate_sound);
	ClassDB::bind_method(D_METHOD("fire_and_forget_sound", "sound_name", "group_name"), &ShinobuGodot::fire_and_forget_sound);
	ClassDB::bind_method(D_METHOD("set_group_volume", "group_name", "linear_volume"), &ShinobuGodot::set_group_volume);
	ClassDB::bind_method(D_METHOD("get_group_volume", "group_name"), &ShinobuGodot::get_group_volume);
	ClassDB::bind_method(D_METHOD("set_master_volume", "linear_volume"), &ShinobuGodot::set_master_volume);
	ClassDB::bind_method(D_METHOD("get_master_volume"), &ShinobuGodot::get_master_volume);
    ClassDB::bind_method(D_METHOD("get_buffer_size"), &ShinobuGodot::get_buffer_size);
    ClassDB::bind_method(D_METHOD("set_buffer_size", "buffer_size"), &ShinobuGodot::set_buffer_size);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "buffer_size"), "set_buffer_size", "get_buffer_size");
    ClassDB::bind_method(D_METHOD("initialize"), &ShinobuGodot::initialize);
}

uint64_t ShinobuGodot::get_dsp_time() const {
    return shinobu->get_dsp_time();
}

int64_t ShinobuGodot::register_group(String m_group_name) {
    return shinobu->register_group(std::string(m_group_name.utf8().get_data()));
}

void ShinobuGodot::unregister_sound(String m_sound_name) {
    return shinobu->unregister_sound(m_sound_name.utf8().get_data());
}

Error ShinobuGodot::register_sound_from_path(String m_path, String m_sound_name) {
    Ref<ShinobuGodotAudioFile> audio_file = Ref<ShinobuGodotAudioFile>(memnew(ShinobuGodotAudioFile));
    Error err = audio_file->load_from_file(m_path);
    if (err == OK) {
        if (register_sound(audio_file, m_sound_name) != SH_SUCCESS) {
            return ERR_BUG;
        }
    }
    return err;
}

int64_t ShinobuGodot::register_sound(Ref<ShinobuGodotAudioFile> audio_file, String m_sound_name) {
    std::string sound_name_str = std::string(m_sound_name.utf8().get_data());
    return shinobu->register_sound_from_memory(sound_name_str, audio_file->ptr(), audio_file->get_size());
}

Ref<ShinobuGodotSoundPlayback> ShinobuGodot::instantiate_sound(String sound_name, String group_name) {
    std::unique_ptr<ShinobuSoundPlayback> playback = shinobu->instantiate_sound(sound_name.utf8().get_data(), group_name.utf8().get_data());
    ShinobuGodotSoundPlayback* godot_playback = memnew(ShinobuGodotSoundPlayback(std::move(playback)));
    Ref<ShinobuGodotSoundPlayback> out(godot_playback);
    return out;
}

int64_t ShinobuGodot::fire_and_forget_sound(String sound_name, String group_name) {
    return shinobu->fire_and_forget_sound(sound_name.utf8().get_data(), group_name.utf8().get_data());
}

void ShinobuGodot::set_group_volume(String name, float linear_volume) {
    shinobu->set_group_volume(name.utf8().get_data(), linear_volume);
}

float ShinobuGodot::get_group_volume(String name) {
    return shinobu->get_group_volume(name.utf8().get_data());
}

uint64_t ShinobuGodot::set_master_volume(float linear_volume) {
    return shinobu->set_master_volume(linear_volume);
}

float ShinobuGodot::get_master_volume() {
    return shinobu->get_master_volume();
}

uint64_t ShinobuGodot::get_buffer_size() const {
    return shinobu->get_buffer_size();
}

void ShinobuGodot::set_buffer_size(uint64_t m_buffer_size) {
    return shinobu->set_buffer_size(m_buffer_size);
}

uint64_t ShinobuGodot::initialize() {
    return shinobu->initialize();
}

ShinobuGodot::ShinobuGodot() {
    singleton = this;
    shinobu = memnew(ShinobuAudio);
}

ShinobuGodot::~ShinobuGodot() {
    memdelete(shinobu);
}