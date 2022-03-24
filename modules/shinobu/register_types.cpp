/* register_types.cpp */

#include "register_types.h"

#include "core/class_db.h"
#include "core/engine.h"
#include "shinobu_godot.h"

static ShinobuGodot *shinobu_ptr = NULL;

void register_shinobu_types() {
	ClassDB::register_class<ShinobuGodotAudioFile>();
	ClassDB::register_virtual_class<ShinobuGodotSoundPlayback>();
	shinobu_ptr = memnew(ShinobuGodot);
	ClassDB::register_class<ShinobuGodot>();
	Engine::get_singleton()->add_singleton(Engine::Singleton("ShinobuGodot", ShinobuGodot::get_singleton()));
}

void unregister_shinobu_types() {
	memdelete(shinobu_ptr);
}
