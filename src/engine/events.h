// What units find on the map: supply and unity pods, monoliths, and the
// alien artifacts they bring to a base.
#pragma once

// A unit opens the pod on its square: returns 1 if an artifact carrying it
// vanished (the unit is gone), else 0.
int goody_box(int veh);
// A unit at a monolith: healed, or its morale raised.
void monolith(int veh);
// An artifact studied at a base or network node: returns 0 if the base had
// already been linked, else 1 (the unit is gone and a tech may be won).
int study_artifact(int veh);
