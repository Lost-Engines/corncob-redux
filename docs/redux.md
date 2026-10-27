# Redux behaviour and development

Rendering uses a depth buffer, full-detail source models and wider intermediate
coordinates; it no longer depends on the DOS projection tiers or streamed tiles.
A zero draw-distance setting has no far clipping plane. Distant polygons naturally
become subpixel. Ground dots and model points/lines scale up to the screen size
of one original-mode pixel nearby, tapering towards a physical pixel with
distance. A one-metre footprint controls that taper (one native EGA pixel at
300 metres with its original focal length); it does not enlarge authored discs
or stars. Line width and reciprocal depth vary along each segment, including
viewport clipping and occlusion. The rear view uses its own panel scale.
Authored discs retain their model diameters and a physical-pixel minimum.
The ground crew's eyes and mouth share the head disc's depth, keeping the full
face visible from an angle while still respecting foreground occlusion. The
injured pose's blood backdrop uses that same layer behind the head.
Ground-point generation has a separate spread multiplier, defaulting to eight
rather than the DOS radius of 32 times camera height. The larger field lets
points shrink before they reach its edge. `--ground-detail-range` and the saved
menu setting change this spread independently of the point count; a value of
one restores the DOS field extent. The point budget is distributed across
logarithmically spaced patch sizes, from a quarter of the native range up to
the configured limit, retaining nearby coverage with staggered far edges.
Points stay world anchored and wrap at patch boundaries around the camera; looking away no longer replaces them or favours distant survivors.
Climbs, descents and travel recenter the patches independently of visibility.
Point sizes have no added random variation.
Palette indices and authored coplanar markings remain intact. The 3D view has
configurable horizontal FOV; its horizontal/vertical focal ratio preserves the
original projection style.
Cockpit instruments render on the original panel, which is scaled with nearest
neighbour sampling and centred on wider screens. Menus retain their original
pixel dimensions.

The gunsight's four solid strokes, gap and thickness scale together from the
640x350 display, using the smaller of the horizontal and vertical scale factors.
Its saved size multiplier ranges from 0.5 to 3, with 1 as the default. It remains
an indexed overlay and retains the original four-stroke artwork.

Options include independent 0..100 percent music and effects sliders. Zero mutes
that stream while its playback/synthesis continues, so raising the level does not
restart music or leave stale effects. Gain requests are atomic; the PCM callback
applies a short ramp on live changes. Existing music and sound on/off controls
remain available independently of the volume settings.

Alt+Enter and the fullscreen menu option share the saved preference. Fullscreen
uses the current desktop mode on the monitor containing most of the window;
Wayland uses the primary monitor because window positions are compositor-owned.
Returning restores the previous window geometry and maximized state. Switching
resets the simulation timestamp and mouse deltas. CLI switches `--fullscreen`
and `--windowed` override the saved choice for that run; the gunsight and volume
controls also have command-line overrides.

The platform calls `session::update_frame(timestamp, input, accelerated)` with
monotonic seconds. Native GLFW supplies its timestamp; a future browser callback
can supply the requestAnimationFrame timestamp after converting milliseconds to
seconds. Live simulation advances immediately by elapsed game time and renders
its latest state. There is no display interpolation, prediction or buffered frame.
Each integration step is at most 1/60 second. Long foreground intervals are capped
at 100 ms of wall time before fast-forward is applied; suspension resets the
host timestamp, so menus, lost focus and hidden tabs have no catch-up burst.

Flight evaluates the original force and control laws using the current state,
with elapsed-time integration, exponential inertia and retained fractional
position, velocity and orientation. Walking, falling, projectiles, model spin,
KLA/turret steering and saucer rotation advance on each substep. New collision
contacts react immediately; persistent overlap damage retains its reference-time
cadence instead of multiplying with frame rate. Map and editor
movement also use elapsed time. Small numerical integration differences between
callback cadences are expected; physical speeds and game timings are tested
across regular and irregular callbacks.

AI random decisions, production countdowns, effect lifetimes and authored
animation advance on elapsed-game-time clocks at their original reference pace
(about 29.13 passes per second). This preserves their intended durations without
limiting continuous motion or collision checks to that frequency. Recorded
demonstrations retain their original sampled pose cadence. The explicit
`session::advance` reference-step path remains for native comparison checks;
normal play uses the timestamp entry point.

Holding T multiplies elapsed game time by ten, using the same bounded integration
steps. It accelerates enemies, projectiles, production, expiry and the sortie
clock along with the player. Its binding is configurable for flying and walking.
Map inspection, the mission builder and paused play do not accelerate.
Actor movement, production and expiry run across the world. Production caps,
mission censuses and force-field checks remain local to each installation.
Weapon targeting ranges remain gameplay rules. Map and menu screens pause the
sortie. Visual drawing has its own random stream, so resolution, FOV, ground
point density and display frame rate cannot consume gameplay randomness.

Walking retains the ten-unit reference gait as a rate, with fractional
centimetres carried between elapsed-time updates. This gives a steady pace of about
29 metres per second along the axes. T also restores the original fifty-unit
walking stride, accounting for the tenfold clock acceleration already in force;
accelerated walking covers about 25 times the normal distance per real second.
Fractional centimetres carry between updates, including changes in walking speed.
The DOS game applies time scaling twice to walking and also gates steps by its
hardware timer, so its real walking speed varies with rendering frame rate.

Grounded aircraft exits use a heading-relative position just aft of the left
wing, with extra aft clearance for the experimental aircraft. Airborne ejection
still transfers the aircraft's position and velocity without that offset.

Craters retain their original blast volume against enemy structures. They are
excluded from player contact and abandoned-aircraft collisions. The preceding
explosion and physical debris retain their original damage.

`redux.cfg` is a separate, readable settings file under the Redux save directory.
It records display preferences, walking mouse options and keyboard/mouse
bindings independently of the active pilot. CLI display options override it;
menu edits save the current settings. Rebinding replaces a conflicting binding
within the same control context. Flight and walking can reuse a key. F1-F4 and
Escape retain their interface roles. Original joystick buttons remain supported
for flight, and the analogue flight response is unchanged.

Run the checks with `BUILD_TESTING=ON`. Native reference checks remain for
unchanged subsystems. Redux-specific depth, real tower-door occlusion, full-world
visibility, local objectives, enemy production, input, settings and PDF tests
replace assertions that required the old bugs. `tools/rebuild_documentation.py`
regenerates the checked-in manuals and embedded documentation deterministically.
