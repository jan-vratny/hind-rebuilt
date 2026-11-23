#pragma once
#include <glm/glm.hpp>

#include "render/mesh.hpp"

namespace render {

// Shahed-136 (Geran-2) loitering munition: cropped delta wing with fins at the tips, piston
// engine and two-blade pusher propeller at the back. 3.5 m long, 2.5 m span, painted black.
// Body coordinates as the Mi-24 (x right, y up, nose to -z), origin at the centre of gravity.
void buildShahed(MeshBuilder& mb, const glm::mat4& m);

// Geran-3 (Shahed-238): the same airframe with a turbojet, its nozzle sticking out of the tail
// and the intake on the back ahead of it; all black.
void buildGeran3(MeshBuilder& mb, const glm::mat4& m);

// Litavr interceptor drone (ours): a 1 m rocket-like body with a camera head of two lenses in
// the nose and a cross of small fins at the tail; instead of a rocket motor four arms in an X
// carry four nacelles with pusher propellers (blades at `propAngle`). Dark grey, red stripes
// along the arms and rings round the nacelles. Axes as above; origin mid-body.
void buildLitavr(MeshBuilder& mb, const glm::mat4& m, float propAngle);

// S8000 Banderol jet cruise missile (Kronstadt, air-launched): a 5 m body about 0.55 m across,
// rounded on top and flat with bevelled edges below, its nose a wedge (pointed seen from above,
// flat from the side); straight wings on a pivot under the body swept back 45 deg (2.2 m span
// unswept), a scoop intake under the body behind them, three tail fins (two level, one on top)
// and the turbojet's round nozzle at the end of a short cone. Light grey, brass lugs on the back,
// a red line round the tail cone. Axes as above; origin mid-body.
void buildBanderol(MeshBuilder& mb, const glm::mat4& m);

// R-60 (AA-8 Aphid) air-to-air missile, 2.09 m long, 0.127 m across: the seeker's glass dome
// in an orange-red ring, two crosses of small plates behind it (the pointed destabilisers, then
// the canards), the white body, at the tail four clipped delta wings (0.39 m span) with the
// rollerons' wheels at their trailing tips and the motor's nozzle. The crosses stand in an X.
// Axes as above; origin mid-body (the nose 1.045 m ahead).
void buildR60(MeshBuilder& mb, const glm::mat4& m);
constexpr float kR60Half = 1.045f;

// 9M114 Shturm (AT-6 Spiral) anti-tank missile out of its tube, 1.83 m long, 0.13 m across, olive
// green: an ogive nose with a silver band behind it, the body with its joints and the fitting
// on top, a steel band, and the narrower tail with the four wrap-around fins sprung out of their
// wells (curved blades, a notch in each trailing edge); they spin it in flight. `smear` > 0:
// the fins three times over that roll (radians), as the eye sees them spinning. Axes as above;
// origin mid-body (the nose 0.915 m ahead).
void buildShturm(MeshBuilder& mb, const glm::mat4& m, float smear = 0.0f);
constexpr float kShturmHalf = 0.915f;
// Its roll in flight, turns a second (8 to 15 on the real one); at 60 frames a second the fins
// would strobe (stand still, turn backwards), so they are drawn smeared over a frame's roll.
constexpr float kShturmSpin = 11.0f;

// In flight the missiles are drawn so within this many metres; further off a streak among the
// effects.
constexpr float kMissileModelRange = 1500.0f;

}  // namespace render
