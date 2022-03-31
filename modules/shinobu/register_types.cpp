/* register_types.cpp */

#include "register_types.h"

#include "core/class_db.h"
#include "core/engine.h"
#include "shinobu_godot.h"

static ShinobuGodot *shinobu_ptr = NULL;

void register_shinobu_types() {
	ClassDB::register_class<ShinobuGodotAudioFile>();
	ClassDB::register_virtual_class<ShinobuGodotSoundPlayback>();
	ClassDB::register_virtual_class<ShinobuGodotEffect>();
	ClassDB::register_virtual_class<ShinobuGodotEffectSpectrumAnalyzer>();
	ClassDB::register_virtual_class<ShinobuGodotEffectPitchShift>();
	shinobu_ptr = memnew(ShinobuGodot);
#ifdef TOOLS_ENABLED
	// Shinobu has to be initialized manually from GDSCript, but we can't do it
	// if we are in the editor
	if (Engine::get_singleton()->is_editor_hint()) {
		shinobu_ptr->initialize();
	}
#endif
	ClassDB::register_class<ShinobuGodot>();
	Engine::get_singleton()->add_singleton(Engine::Singleton("ShinobuGodot", ShinobuGodot::get_singleton()));
}

void unregister_shinobu_types() {
	memdelete(shinobu_ptr);
}
