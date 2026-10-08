#pragma once

// mod (gpad): retail declared the GamePad struct (with XInput members) and the GPad_* API here. Both moved to the
// platform-neutral client/gpad_core.h (no <XInput.h>); win_gamepad.cpp is now only the XInput backend
// (client/gpad_backend.h). Kept so the existing includes (ui_shared, devgui, demo playback, cl_input_mp, ...) work
// unchanged. See docs/controllers.md.

#include <client/gpad_core.h>
