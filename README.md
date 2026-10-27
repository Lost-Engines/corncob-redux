# Corncob Redux

The **Lost Engines Project**'s enhanced native version of **Corncob 3D** and
**The Other Worlds Campaign**. Fly, land anywhere, explore on foot, plant bombs,
steal planes and remotely control your aircraft in this wonderfully strange
alien-invasion game.

Redux builds on our [faithful C++ reconstruction](https://github.com/Lost-Engines/corncob-cpp).
All artwork, worlds, music and sound effects are embedded; no DOS installation
or separate game files are required.

## What's improved

- The 3D view renders at your window's resolution, with adjustable field of view.
  Original cockpit artwork stays crisp with nearest-neighbour scaling. Live
  movement updates on each frame, with consistent game speed across frame rates.
  The gunsight scales with resolution and has an adjustable size.
- Unlimited draw distance by default, with full-detail models and depth testing
  that fixes the tower door showing through walls and other overlapping objects.
- WASD movement and mouse look outside the aircraft; Space jumps and Q opens the
  parachute. Stepping out places you just behind the wing. Keyboard and mouse
  bindings are configurable in the menu.
- Separate, saved music and sound-effects volumes. Alt+Enter switches fullscreen
  and restores the previous window size when returning.
- Adjustable ground detail and field range give a clearer sense of movement.
  Nearby dots and lines scale with resolution, becoming fine single-pixel details
  in the distance.
- Hold T for tenfold fast-forward while flying or walking. Enemies, projectiles
  and production keep pace, and distant enemies remain active.
- Lingering bomb craters no longer damage the player or their aircraft. Explosions
  and debris still do.
- Mousewheel scrolling in menus, documentation and tower briefings.
- Reviewed manuals with corrected spelling, updated controls and more complete key
  descriptions. PDF export includes clickable contents and chapter bookmarks.

Press **Escape** or **P** during play for the pause menu and options, or **F4** for
current control bindings. Changes in the options menu save automatically.
Losing window focus releases the mouse and pauses play.

## Build and run

Requires CMake 3.30+, a C++26 compiler, Boost 1.85+ with Program_options, and
OpenGL and window-system development headers.

```sh
cmake -B build-rel -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-rel --parallel
./build-rel/corncob
```

`Release` is also supported. On Windows with a multi-configuration generator,
select the desired configuration when building and running. For other build
options and dependencies, see [Corncob C++](https://github.com/Lost-Engines/corncob-cpp#build-and-run).
Tests are off by default; enable `-DBUILD_TESTING=ON` when developing.

## Display and controls

| Option | Purpose |
| --- | --- |
| `--resolution window` | Match the 3D view to the window; the default. |
| `--resolution 1920x1080` | Use a fixed 3D rendering resolution. |
| `--fullscreen` / `--windowed` | Choose fullscreen or override a saved fullscreen setting. |
| `--gunsight-size 1.5` | Gunsight size multiplier, from 0.5 to 3; default 1. |
| `--music-volume 75` | Music volume, from 0 to 100 percent. |
| `--effects-volume 75` | Sound-effects volume, from 0 to 100 percent. |
| `--fov 110` | Set horizontal FOV, from 30 to 150 degrees; default 94. |
| `--draw-distance 0` | Unlimited visibility; a positive number sets a limit in kilometres. |
| `--ground-detail 8` | Ground-point count relative to DOS; default 8, zero disables it. |
| `--ground-detail-range 8` | Ground-field spread relative to DOS; default 8, use 1 for the original range. |
| `--mouse-sensitivity 24` | Walking mouse sensitivity, from 1 to 200; default 24. |
| `--joystick auto\|none\|N` | Automatically select a joystick, use keyboard only, or select a device index. |
| `--skip-intro` | Open the main menu immediately. |
| `--slalom` | Play the recovered experimental flying course. |
| `--save-dir PATH` | Choose the directory for careers, settings and exported manuals. |

Use `--help` for all options, including world selection and alternate colour
modes. Command-line options override saved display settings for that run.

A wider ground-detail range adds more distant coverage while retaining nearby
dots. Increase `--ground-detail` for a denser field. Both settings are
available in Options and controls and save automatically when changed there.

Create a named pilot to save your career; the default trainee is temporary.
Redux uses its own save directory, separate from Corncob C++. Display and binding
preferences live in `redux.cfg` there, independently of the active pilot.

## Manuals

[Earth campaign](assets/documentation/earth.pdf) ·
[Other Worlds](assets/documentation/other_worlds.pdf) ·
[Flying hints](assets/documentation/hints.pdf) ·
[Mission builder](assets/documentation/mission_builder.pdf)

The menu's **Save documentation as PDF** option writes all four to a
`documentation` subdirectory inside your save directory and shows its location.
Historical DOS hardware requirements and contact details are retained for
reference and clearly labelled.

Visit [Lost Engines](https://lostengines.com) for our other reconstructed games.
Original reconstruction details are in [the reference notes](docs/reconstruction.md);
Redux's changes above deliberately depart from some DOS behaviour.
