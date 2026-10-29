# Developer screenshots

`corncob_capture` renders PNGs with Redux's scene renderer and live game session.
It runs without a window, audio device or save directory. It uses the embedded
worlds and models, including the original destruction and particle effects.

The tool is optional, excluded from the normal build and never installed. It
requires libpng development files and a `Debug` or `RelWithDebInfo` build:

```sh
cmake -B build-rel -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCORNCOB_BUILD_CAPTURE=ON
cmake --build build-rel --target corncob_capture --parallel
./build-rel/corncob_capture --help
./build-rel/corncob_capture --list-theatres
```

On a multi-configuration generator, build with `--config Debug` or
`--config RelWithDebInfo`. Compiling the tool with `NDEBUG` is an error.

A close encounter with the White Sands ground crew, at the default 3840×2160:

```sh
./build-rel/corncob_capture --world=white_sands \
  --eye=-7220,6,2.5 --at=-7212.9,15.3,2.2 \
  --advance=.1 --hold --fov=70 --output=crew.png
```

An aircraft attacking the Atkinsville fuel dumps:

```sh
./build-rel/corncob_capture --world=morning --flight \
  --eye=-4150,-7120,250 --at=-3590,-6740,10 --bank=-35 \
  --fire=both --advance=2 --hold --output=attack.png
```

Positions are **metres** in the game's X/Y/Z coordinate system, with Z upwards.
Angles are **degrees**; positive pitch looks down. `--at` aims the camera at a
world position; `--bank` adds roll to that view. `--mission N` selects a
zero-based mission start, and `--slalom` selects the recovered slalom course.
Use `--size=1280x720` for quick previews and omit it for native 4K output.

The player starts at the mission or theatre spawn unless `--eye` or `--pilot`
sets their position. An explicit `--pilot` separates the player from the
camera; `--attitude heading,pitch,roll` then controls the aircraft's initial
orientation. `--external` makes the controlled aircraft visible to that
separate camera. `--cockpit` includes the instruments; clear views are the
default. `--gunsight` adds the gunsight without the panel.

`--advance SECONDS` runs the normal simulation at 60 updates per second before
capture. `--hold` keeps the player's position and attitude fixed while enemies,
weapons, timers and particles continue. The player is invulnerable and radio
message interruptions are disabled. `--fire=pistol|cannon|missile|both` holds
weapons during that interval; ammunition rules still apply. `--rocket` holds
boost. `--bomb-at`, `--eject-at`, `--parachute-at`, `--rescue-at`, `--charge-at`
and `--detonate-at` schedule the corresponding in-game action, in seconds.

`--destroy=51,52 --destroy-at=1.75` applies a destruction hit through the original
object damage handler at the specified time. Native restrictions still apply:
for example, hitting a forcefield reveals it, and certain enemies require
specific weapons. This can catch a particular destruction stage without
waiting for a lucky collision. No geometry or effects are added by the capture
renderer itself.

`--list-objects` prints present actor indices, types and positions after the
simulation. `--track N` shifts the camera and aim with an existing world object,
useful for following a moving saucer. `--seed N` makes a setup repeatable;
`--fov`, `--detail` and `--range` adjust the view and ground detail. The final
line on stdout reports resolution and render/combat counts. The only file
written is the requested PNG; an existing output file is overwritten.
