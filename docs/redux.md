# Redux behaviour and development

Rendering uses a depth buffer, full-detail source models and wider intermediate
coordinates; it no longer depends on the DOS projection tiers or streamed tiles.
A zero draw-distance setting has no far clipping plane. Distant polygons naturally
become subpixel; points and discs retain a one-pixel minimum. Palette indices and
authored coplanar markings remain intact. The 3D view has configurable horizontal
FOV; its horizontal/vertical focal ratio preserves the original projection style.
Cockpit instruments render on the original panel, which is scaled with nearest
neighbour sampling and centred on wider screens. Menus retain their original
pixel dimensions.

The simulation advances at the original fixed five-unit step, forty PIT ticks
per update (about 29.13 updates per second), independently of display refreshes.
Holding T schedules ten times as many complete fixed updates; releasing it
restores normal speed. It accelerates player travel, enemies, projectiles,
production, expiry and the sortie clock together, without enlarging physics
steps. The binding is configurable for both flying and walking. Map inspection,
the mission builder and paused play do not accelerate.
Actor movement, production and expiry run across the world. Production caps,
mission censuses and force-field checks remain local to each installation.
Weapon targeting ranges remain gameplay rules. Map and menu screens pause the
sortie. Visual drawing has its own random stream, so resolution, FOV, ground
point density and display frame rate cannot consume gameplay randomness.

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
