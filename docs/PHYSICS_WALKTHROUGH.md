# Bike physics and the big-wheel experiment

## Try the wheels

Startup uses normal **1.0x wheels**, **1.0x throttle** and **1.0 traction**.
The normal wheel radius is 0.4 metres (diameter 0.8 metres).

During play, tap **P** to enlarge wheels or **O** to shrink them in 0.1x steps,
between 0.5x and 3.0x. **Ctrl+P / Ctrl+O** increase/decrease throttle torque in
0.25x steps, between 0.25x and 5.0x. Both Control keys work. The current wheel
and throttle scales appear in a status message. These controls are ignored
while typing, dead, spectating or waiting for the countdown.

**Options > Experiments** also lets you cycle values with Enter: wheel size,
throttle power, shot speed, shot gravity, shot angle, firing delay and square
collisions (Stop / Pass through), plus killer speed and delay. Reset experiments restores 1.0x wheels,
1.0x power, 1.0 traction and cannon defaults. These settings persist between runs in this
session, but are not written to disk and reset when the application restarts.

From the repository in WSL:

```sh
meson compile -C build
./build/elma
```

Run from the directory containing your game assets (the repository root in
the current setup). Both wheels get larger collision circles and matching
sprites. Their mass stays 10; their moment of inertia grows from 0.32 to 0.72
because it scales with radius squared. Wheel size alone leaves engine torque,
angular-speed limit, chassis, rider and suspension attachment positions unchanged. Expect
different acceleration, rolling speed and clearance, not just bigger artwork.
Live resizing rebuilds the collision grid using the new radius, updates both
players' wheels/inertia and preserves their tread speed. The wheel centres are
not teleported; the usual contact solver resolves any new ground overlap, so
growing wheels in a tight tunnel can jolt or trap the bike. Throttle scales
engine torque, not the angular-speed cap or the brake/volt strengths.
Flag Tag's wheel-touch and immunity distances scale too.

This is a local physics experiment. Ordinary replay files do not record the
wheel-size setting, so playback in another build can draw the wrong size.
Existing replays also render with this build's wheel size. Do not treat times
from this experiment as standard-physics results. The change does not add an
online mode or change recording formats. Tight starts/tunnels may no longer fit;
the old solver still retains at most two terrain contact points.

## Read these files in order

1. `include/physics/init.h`: the state being simulated (`rigidbody`, `motorst`)
   and the live experiment declarations. `src/physics/tuning.cpp` holds the
   wheel/throttle controls and rebuilds the terrain grid when resizing.
2. `src/physics/init.cpp`: masses, radii, inertia, gravity, initial positions,
   spring strength, damping and neutral suspension offsets.
3. `src/game/game.cpp`: `physics_subframe` is the bridge from controls to
   simulation to gameplay events. The `pacer::subframe` loop calls it.
4. `src/physics/pacer.cpp` and `include/physics/pacer.h`: how elapsed real time
   becomes small simulation steps.
5. `src/physics/forces.cpp`: `simulate_bike_physics` is the main physics update;
   `calculate_wheel_forces` builds suspension forces; `check_object_collision`
   handles head contact and finds object interactions.
6. `src/physics/move.cpp`: `rigidbody_movement` integrates motion and resolves
   wheel contact; `body_movement` and `body_boundaries` move/restrict the rider.
7. `src/physics/collision.cpp`: circle-versus-edge tests and contact anchors.
8. `src/level/segments.cpp`: converts level polygons into cached edges and
   organises them into a collision grid. `src/game/level_load.cpp` prepares it.
9. `src/renderer/render.cpp`: draws wheel sprites, chassis and rider using the
   simulated positions. Sprite drawing does not determine collisions.

## What is a bike?

The chassis and each wheel have a centre position `r`, linear velocity `v`,
rotation, angular velocity, mass, radius and rotational inertia. Coordinates
are in metres with **positive y upwards**; level-file y coordinates are inverted
when building the collision segments. Angles use radians. Time in the physics
code uses the game's own time scale; do not assume `dt` is wall-clock seconds.

There is no rigid triangle holding the wheel centres to the chassis. Springs
pull each wheel towards a neutral attachment position rotated with the chassis.
Damping resists relative motion, including motion from chassis rotation. The
wheel pushes back on the chassis with an opposite force and a turning effect
(torque). This is why the suspension can stretch and the bike can bounce.

The rider's body is a separate spring-driven point, constrained to a limited
region relative to the chassis. The head position is derived from that body
and bike orientation. Wheels collide with terrain; the chassis does not.
Head/terrain contact ends the run instead of bouncing the head off the ground.

## One game-loop update

1. **Read input and set a time target.** The outer game loop processes events.
   `pacer::new_frame` converts elapsed wall time into target physics time.
2. **Catch up in substeps.** `pacer::subframe` returns a `dt` no larger than
   `0.0055` physics-time units. The final step can be shorter; it is not a
   strictly fixed-timestep simulation. One displayed frame can need many steps.
3. **Filter controls for each living player.** `physics_subframe` applies
   gameplay restrictions and volt cooldowns to the frame's input. Gas and brake
   become wheel torque requests; left/right volts request chassis impulses.
4. **Calculate forces.** `simulate_bike_physics` finds the chassis's local axes,
   applies engine/brake torque, and computes both suspensions' spring/damping
   forces and chassis reactions before advancing any of those rigid bodies.
   Braking uses an angular spring/damper relative to the chassis, not simply
   setting wheel speed to zero. Volts add/remove angular velocity according to
   game-specific timing rules; they are not an energy-conserving force model.
5. **Advance the rider, chassis and wheels.** Add gravity in the current gravity
   direction. Update the rider, then chassis, left wheel and right wheel.
   `rigidbody_movement` handles each rigid body as follows:
   - Find up to two contact anchors near the circle's current centre.
   - Push an overlapping wheel out to its radius minus a tiny deformation.
   - Discard contacts the wheel can escape and remove inward velocity.
   - With no contacts: `v += (force / mass) * dt`, then `r += v * dt`.
     Angular velocity and rotation update in the same order using torque/inertia.
   - With one contact: combine force and torque about the contact and advance
     the wheel as a rolling circle; linear speed is linked to angular speed.
   - With two retained contacts: stop linear and angular movement.
6. **Handle consequences.** Recompute the head position. Check head death,
   bounds, apples, hazards and finish interactions; update sound and recording
   and consume the resulting events. The outer loop advances time and repeats.
7. **Draw.** After catch-up, the outer loop updates presentation and renders.
   Turning also has frame-level input handling and visual interpolation.
   Rendering wheel images larger alone would not change their collisions.

## Terrain contacts and the moving-square experiment

An anchor is the nearest point on an edge: its endpoint if the circle is beyond
the edge, otherwise its perpendicular projection onto the edge. The grid is a
fast candidate lookup; the circle/segment distance test decides actual contact.
Two nearly identical anchors are merged to handle shared polygon vertices.
The solver does not compute a full contact manifold for arbitrary shapes.

Level polygons are converted into cached segments and rendering structures.
Changing a polygon's vertices during play does not automatically move those
cached collision edges. The current wheel solver also assumes stationary
terrain and queries the current position rather than sweeping through the
entire movement, so fast bodies can tunnel through thin obstacles.

The K-key prototype is implemented in `src/physics/projectile.cpp`, with its
interface and size in `include/physics/projectile.h`. Press or hold **K** during
play to launch squares, with a default cooldown of approximately **0.4 real seconds**.
Up to 64 coexist; when full, the oldest is recycled. By default they stop on
contact with each other. Each is one metre wide (normal wheel diameter is 0.8 metres), drawn
as a bright outline above foreground art.

The cannon follows the bike's full rotation and facing, elevated **15 degrees
toward the bike's local up**. Its muzzle clears the wheels and head along that
direction. Initial velocity is **the chassis's complete linear velocity plus
6.2 times the aiming unit vector**. Downward acceleration is **10.0**, matching
normal bike gravity (ten times stronger than the original square prototype).
The projectile still uses world-down gravity when the bike has gravity apples.
For a stationary upright bike on flat ground, this is tuned for about **6 metres
(20 feet) from the bike's centre** to the square's landing centre, including the
muzzle offset. Bike speed, tilt, launch height and terrain change the range.
Squares do not rotate, bounce, or react to being hit by the bike.

The squares update **once per physics substep, before either bike**. A continuous
box/segment test stops it at its first terrain hit, where it remains a solid
obstacle. Square-to-square impacts use relative swept motion and are processed
in time order alongside terrain impacts. Both freeze where they meet, even
in midair: there is no bouncing, sliding or physically realistic stacking yet.
A muzzle blocked by another square cannot fire. The menu's Pass through mode
restores the earlier noninteracting behaviour; changing this mode clears shots
to avoid enabling solidity on squares that already overlap.

The wheel update then handles circle/square contact at that substep's
position: push the wheel out, cancel inward velocity relative to the moving
surface, and couple wheel spin to tangential movement with a no-slip impulse.
The suspension transfers that wheel movement to the chassis on the next step.
Head contact kills the rider. The chassis remains non-colliding, as with terrain.

This is separate dynamic geometry, not a level polygon inserted into the static
grid. It cannot introduce intersecting polygons into the level's topology.
It rejects a spawn that intersects terrain; clears on leaving/restarting a run;
expires each shot after 30 physics-time units (about 69 real seconds); and ignores K while
typing in chat, dead, spectating, viewing the map, or waiting for the countdown.
In split-screen, K launches from player 1 and both bikes can hit it.

Prototype limits: no replay/network serialization, no custom
key binding. Fast circle/square sweeps use a conservative expanded box, so
corner contacts can occur slightly early; resting contacts use exact circle
geometry. Static-terrain and square contacts are solved sequentially, so a bike
trapped between them can jitter or be pushed into terrain. This is not yet a
general solver for moving platforms, crushing, or multiple dynamic polygons.

Focused geometry and response tests (no game assets/window required):

```sh
meson test -C build projectile tuning killer-shot --print-errorlogs
```

## L-key killer launcher

Hold **L** for small black-circle shots. Hold **N / M** to decrease/increase the
aim angle continuously (60 degrees per real second). A contrasting barrel line
starts near the rider's handlebars and points along the muzzle direction. The
angle follows chassis tilt and mirrors when the bike turns. It initially points
10 degrees above the bike's forward direction. Rotation wraps through 360 degrees.

The new `src/physics/killer_shot.cpp` pool is separate from native level killers
and K-key squares. Default speed is 24 physics metres/time-unit plus the bike's
velocity, radius is 0.12 metres, gravity is 6.0, and delay is 0.25 real seconds.
**Options > Experiments > Killer speed / Killer delay** cycle the two settings.
Aim and settings last for the session; resetting experiments restores defaults.

Each moving circle is swept against terrain edges and their endpoints, stopping
at the earliest impact. From that impact it remains a lethal stationary hazard
for **three real seconds**. During the final second it alternates black/white
five times per second, without disabling collision or changing its position.
A thin contrasting rim keeps it visible against dark backgrounds. The renderer
only reads timers; rendering frequency does not determine expiry or lethality.

Like native killers, contact with either wheel or the head kills. Sweeps use
relative rider/shot motion to catch fast crossings between substeps; the impact
step is split into a moving portion and stationary portion. There is no permanent
shooter immunity: a returning shot or a hazard you drive into can kill you too.
The muzzle clears the shooter's wheel/head circles, and its full path from the
handlebars is checked to prevent firing through an adjacent wall.

L/N/M ignore chat, death, countdown and spectator/map views. In split-screen,
player 1 operates the launcher and either local player can be hit. Shots clear
on leaving/restarting a run. Airborne shots that never hit terrain expire after
15 real seconds; the bounded pool holds 128 and recycles the oldest when full.
They ignore each other and the K-key squares for now. Shots/aim are not recorded
in replays or sent over the network; online CTF synchronization is future work.

## Ice / traction

**9** decreases traction by 0.1; **0** increases it. The range is 0 (ice) to 1
(original handling), also selectable in **Options > Experiments**. It lasts
between levels until the game closes. A status message shows the current value.

Read `src/physics/move.cpp` and `src/physics/traction.cpp`: after removing motion
into the ground, the ice branch keeps tangential translation and wheel rotation
separate. The contact slip is tangential speed minus angular speed times radius.
A friction impulse reduces this slip, capped by traction times normal impulse
(impact plus supporting force over the timestep). Thus engine torque spins tyres
on ice rather than magically accelerating the bike, and braking cannot instantly
stop sliding. Gravity and normal collision support remain. At exactly 1 the
original tuned rolling solver runs unchanged. The existing two-contact corner
solver still stops trapped wheels; this is not a rewrite of Elma's corner physics.
Square contacts also limit their tangential impulse using the selected traction.

## Rain and terrain pits

**Options > Experiments > Rain / metre / second** cycles Off (0), 0.25, 0.5,
1 (default), 2, 5 and 10. **Clear rain** removes drops and empties puddles. Frequency persists
between levels; drops are reset for each run. Disabling emission leaves existing
water in place until cleared. These are local experiments, not replay/network data.

Read `include/physics/rain.h`, `src/physics/rain.cpp`, `rain_level.cpp` and
`src/renderer/rain.cpp`. The level adapter excludes grass and converts editor Y
coordinates to physics coordinates. The largest-area polygon is treated as the
outer boundary; its edges with sky immediately below supply rain. Each source
is weighted by length, and emission uses elapsed real time rather than frames.

Each drop has one bottom-centre contact point. Falling points sweep against
static edges, attach on impact, and travel downhill at at least 0.6 physics units
per timestep-unit. Connected downhill edges continue the flow, valleys collect
it, and ledges release it back to gravity. Drops do not touch the bike, square
projectiles, or other moving drops. On open horizontal ground drops drift right
at the minimum slide speed, crossing consecutive flat edges and falling off
ledges. Existing overflow particles keep their outward direction instead.

Pits are detected from connected terrain with sky above it. Starting at each
local minimum, both banks extend through multiple edges and terraces to their
crests. Floors may be flat, split into several edges, or slightly tilted; walls
may be vertical. The complete floor profile is retained and capped at the LOWER
rim. Each drop contributes 0.01 square metres of 2D volume. Capacity is the
integral of depth above every floor segment. A monotone search computes a level
surface for the current volume; this height is cached until water is added.
Drawing and wheel submersion use the same complete profile. Drops reaching existing
water are absorbed there. Full puddles keep their shape and turn excess water
back into drops at the lower rim. Equal-height rims alternate. Overflow starts
up to 0.02 metres along the adjoining outside edge; downhill edges slide normally,
flat spillways retain an outward direction, and ledges release falling drops.
Fractional drop volumes are retained until a whole drop can be emitted. A full
particle pool defers overflow rather than losing it. Spawning occurs after all
particles advance, preventing recycled slots from moving twice in one tick.
Once neighboring pits fill to the same shared crest, they merge into one lake.
Their stored water and pending overflow are conserved, and their higher outer
banks determine the new capacity. This repeats across a jagged bucket floor.
Clearing rain restores the original empty pits, ready to fill and join again.
Overhanging profiles remain unsupported. An island above a broad pit caps water
at the island's lowest obstruction rather than disabling the entire pit; flowing
around submerged islands is still outside this simplified height-profile model.

Rain on dry banks stays visible as it slides downhill until it reaches actual
water or a local minimum. Nearly flat edges within 2 cm of the basin bottom
absorb runoff directly, avoiding a long wait across kilometre-long floors. Tests cover
10,000 simultaneous impacts on a two-kilometre-wide pit. Open ground without a
pit still drifts right and drains normally.
The HUD shows active raindrops out of 30,000 and the number of nonempty puddles.

Terrain and basin spatial lookups avoid testing every polygon for each drop.
Rain updates at a fixed 60 Hz rather than at every tiny bike substep; swept
contacts prevent skipping thin terrain. The pool has 30,000 slots; excess rain
is skipped until absorption or clearing frees slots. Foreground water approximates a 50% light grey-blue
blend through a lookup table for the game's indexed palette. It is drawn after
the rider and terrain but before the HUD. Each water scanline draws separate
wet intervals, clipped against the floor profile and the level's actual sky
intervals, so merged lakes do not paint over internal ridges or solid terrain.

`src/physics/water.cpp` applies drag after bike integration on every physics
substep. Each wheel and the chassis use their own circular submerged fraction,
estimated with 32 vertical slices clipped against both banks and the surface.
The rider uses a small circular proxy. Linear velocity is multiplied by
exp(-8 * resistance * submerged_fraction * dt); spin by
exp(-5 * resistance * submerged_fraction * dt). Resistance defaults to 0.35,
35% of the original heavy drag. **Options > Experiments > Water resistance**
cycles 0%, 15%, 35%, 50%, 75%, and 100%; it persists for the session.
This is stable damping, cannot reverse velocity, and slows sinking as well as
horizontal motion. Gravity still acts on both wheels and chassis normally;
there is no buoyancy or fluid pressure yet. Dry bodies remain unchanged.

Volts retain their timing and original gameplay rules. While a volt is active,
water also damps its saved pre-volt spin and tracks the surviving fraction of
its temporary 12-radian/time-unit boost. The delayed correction removes only
that remaining boost, avoiding a full-strength correction after water already
removed part of it. In dry play the factor stays exactly 1.

A fully submerged head gets a white breathing-apparatus ring; there is no
drowning death. Every 2.5 real seconds underwater, each living rider exhales
three small white outline bubbles. They rise with a slight wobble and disappear
at the surface. Bubbles are cosmetic, use a separate bounded 128-slot pool, and
are cleared with rain or on level restart. The ring disappears when any part of
the head is above water. Water drag still affects movement normally.

## Rain impact audio

`src/sound/rain.cpp` synthesizes two families of 60 ms impact sounds: four soft
filtered-noise patters for dirt, and four low, rounded plops for existing water.
Both have a gentle attack and faded tail. No external sound file is needed.
Actual falling-drop terrain/water
impacts trigger them, with interpolated impact times. Sliding along a bank does
not continuously retrigger audio. Volume falls with distance and impacts beyond
35 metres are silent; split-screen uses the nearer rider. Normal sound settings
and game muting apply.

A randomized 12–35 ms minimum spacing limits dense impacts without tying their
timing to rendered frames.
Clearly louder nearby impacts can bypass that cooldown so distant drops cannot
use up the local rain sound budget. A bounded single-producer/single-consumer queue sends
events to the audio callback, preserving event spacing within an audio buffer.
Eight dedicated voices keep rain from using the five ordinary game-effect slots.
Muted, old-level and stale queued impacts are discarded instead of played later;
the final mix is clamped to prevent rain from wrapping the audio amplitude.
