# Reconstruction reference

The target is **normal Corncob 3D 3.42**, shared by the retained shareware and
Personal Companion retail flight engines. `3.EXE` has SHA-256
`c411dde7d18e1dc19d718327b70d1e69321448520344d3baa4a0c8e68a71e81e`.
Research inputs, disassembly and original DOS captures live in the sibling
`corncob-reverse` workspace. Browser previews are inspection aids; unchanged
executable routines supply the numerical reference.

The application is C++ throughout. It does not execute original x86 instructions,
launch DOS helpers or require an external installation. CMake embeds the original
data and artwork; Python, Unicorn, DOSBox and the research workspace are development
inputs only. Original music and sound effects are included.

## Original source and provenance

The public [foone/Corncob3D repository](https://github.com/foone/Corncob3D) contains
original assembly for flight, objects, rendering, instruments, sound and SRATS.
The **Initial Import** commit `2759414da1e6e645c3640288577d1e0cc8c32be4` is
preserved under `corncob-reverse/analysis/original_source/`, without subsequent
MASM edits or uploader documentation. The source distinguishes shareware and
registered builds but omits the C menu program and a complete build environment.
Some paths differ from the retained executable; target-binary behaviour takes
precedence.

The upstream account describes an original 2001 release, republication in 2018
and informal permission from Kevin Stokes, with no formal source licence. That
account is not a new licence grant. Embedded input hashes and origins are recorded
in [assets/provenance.json](../assets/provenance.json); third-party terms remain
under `third_party/`.

## Connected game

The executable also includes [Other Worlds Campaign](other_worlds.md), with a grouped world selector, separate campaign careers, multiple airfields, recorded demonstrations and its mission builder.

One session retains the aircraft, on-foot pilot, world, weapons and active actors.
It supports walking/jumping, boarding and re-entry, powered flight and landing,
ejection, free fall, parachuting and remote flight. Four original theatres and
procedurally generated training are available. Fixed five-unit simulation steps
advance forty fast PIT ticks; T changes physical time while retaining frame timers.
The animation word restarts at 128 on reaching 16,384, as in the native loop;
512 unchanged counter cases and a connected long-flight check cover the restart.

Weapons use the original separate 32-bullet and 320-effect pools: guns/cannon
mode, finite pistol ammunition, unguided missiles, replenishing bombs and the
planted assassin charge. A sortie begins with no carried pistol ammunition or
assassin charge; leaving an aircraft supplies twenty rounds and one charge.
Boarding or visiting a tower does not grant that equipment. Effects include
debris, incendiaries, mortar barrages,
concealed sites, orbiting/swaying enemies, forcefields, generator attraction and
mindbender palette effects. Aircraft damage retains its separate engine, wing,
rudder and stability channels rather than generic hit points.

Destruction retains each target's own wreck, fragments, weapon restrictions and
score credit. Surviving saucers tilt when struck; launch sites reset their rest
intervals and vehicles retain the same pursuit reset used by ordinary movement.
Shards retain their authored initial tilt, 6,200-frame lifetime and original
per-slot red/yellow flicker, becoming black on the ground. Explosions retain their
random drift. Anti-aircraft shells use the original nine-fragment damaging burst;
bombs preserve the native crater transition and time-adjusted lifetime. Rescue
vans share the original vehicle hit/wreck handler. Ordinary flotsam ignores hits,
while mortar fragments burst on contact; doorways remain harmless and forcefield
hits briefly reveal their near model at a distance.

All 992 independent hit callbacks and 128 paired SLAM contacts match unchanged
native executions: both object records/models, all 130 score bytes, 3,916 emitted
effects and complete PRNG continuation. A second collision callback sees the
first object's changed state. TETTIM also leaves its scratch pointer at a harmless
fragment, so SLAM can skip the original projectile's second callback; this quirk
is explicit rather than accidentally depending on a C++ reference. The fixture
supplies effect allocation only. Separate comparisons cover 192 debris/shard/
explosion constructors and 512 shard colour passes. These checks do not establish
identical ordering of all contacts within a complete moving scene.

Aircraft damage/ejection, actors, weapon constructors and scene drawing share one
random stream. Each simulation pass draws once, after nearby handlers and before
portal transfer/player physics; additional host refreshes reuse the retained
flight page. Paused/developer previews copy visual state and randomness instead
of advancing the sortie. Connected comparisons verify identical gameplay and
simulation frames despite extra previews. Exact interleaving of individual
original painter-table handlers and primitives remains under investigation. The saucer's modes select retreat, circling, approach and coasting around
the current airbase. Its death uses debris-before-spin random ordering; ground
impact produces upward fragments and fifteen incendiaries. Barrage balloons
survive ordinary contacts. Bees retain the accidental RAND_W carry-flag dependency
that sometimes skips their vertical update. Dead aliens disappear outside their
near tier. Launch sites retain quality-specific reset/retry behaviour and update admission
counts immediately, including failed allocation attempts. Vehicle sites wait
three reset periods; saucers retain their original 32,000-frame lifetime and
unshifted constructor position. A pass captures its existing active actors before
handlers run, so a newly constructed child first updates when admitted on the
following pass, as with EXPMOV's original painter-table insertion. The new-object
bit is consumed by effect movement. Completed destroyed orbs retain DHQTIM's repeated spin/gravity assignment and
settle at 3,100 cm after crossing its 11,200 cm threshold. Stored objects use their
mutated motion fields, so the drop actually progresses across frames. Near/far
orbs and mindbenders overwrite their shared visual effects in handler order;
objective callbacks retain the previous census and one-time credit. All 768 native
orb/objective/mindbender cases match without stubs, including preceding-orb effects,
all qualities, attraction, regenerated health and PRNG continuation. Remaining
actor equivalence is under comparison. The live census includes concealed fuel
and counts rescue vans in the shared vehicle total. Alien corpses and overturned
vehicles remain visible without blocking their clear-all objectives; connected
checks exercise these states through destruction and objective completion.

Portal quality rows now drive their original transfer, damage, repair and objective
bits. Transfers occur on the following pass, preserve full coordinate words, consume
four random draws in the original order, and arrive at 229,376 cm. Repair preserves
historical worst damage and invulnerability. Noncollision portal updates retain
the original timer, completed-white/stippled flash colours and PRNG continuation;
each portal retains the colour selected by its handler for its model primitives.
The destination block matches 512 native cases and the complete noncollision
handler matches 256; connected cases cover objective credit, delayed transfer,
repair, and a damaging portal followed by a repair portal.

The original autopilot includes pitch-by-speed feedback and altitude capture.
Optional beginner aerodynamics adds the original weaker PITCHLOCK correction.
When both are enabled, the two original calls remain. The standalone engine defaults to advanced; MOAG creates pilots with beginner
aerodynamics. The unified launcher follows MOAG; `--flight-model advanced`
overrides the saved pilot setting. Combat Missions selects the flight model;
keyboard/joystick setup selects the input device before a new flight. The global keyboard/joystick choice uses header byte two of PILOT.SCR; the
pilot’s model uses bit `0x20` at `0x23b` (set for advanced). New pilot setup
bytes match an unchanged MOAG-created record. Explicit device indices remain
startup choices because DOS only stores keyboard versus joystick.

Parked aircraft now run the original placement/repair handler. Eligible standard
and reserve aircraft line up 7,200 cm apart; a new arrival takes a positioned
normal aircraft to the runway automatically. Experimental and permanently
crashed aircraft retain their distinct paths. The tower keeps parked poses within
4,200 scaled distance units and resets them outside that radius; its separate
5,200 limit controls home/radar status. A manually retained or abandoned aircraft
keeps its pose until the original reset clears its placement lock. All 512 native
parked-aircraft cases and 256 tower reset cases match; connected checks cover the
normal arrival, stepping out and avoiding immediate automatic re-entry.

Digital joysticks supply two calibrated axes and two buttons through GLFW. The
original squared response and clipping are retained, without analogue-port timing
or calibration prompts. Arrows override both stick axes; head-view keys leave the
physical stick available for flight. Buttons select guns/missiles. Keyboard
weapon bindings remain Space for guns, Shift for cannon mode and C for missiles,
even with a joystick connected. This deliberately avoids DOS's joystick-mode
remapping of Space to missiles, so automatic discovery cannot change keyboard
weapon controls. `--joystick auto|none|N` selects a device, and
`--list-joysticks` reports indices/names. Polling is restricted to focused flight.
Device discovery detects the development host's T.16000M; physical control motion
has not been exercised during automated checks.

## Unified interface and campaign

The executable joins title/story, roster, theatre selection/intelligence, training,
controls/help, flight, outcomes, reports and the interactive Earth-saved ending.
The missing C front end is reconstructed from unchanged MOAG 3.42 captures. Twenty retained
complete 640 x 400 screens match the DOS RGB pixels exactly, covering the duty
roster, career reports, training and control setup, score tables, documentation,
Other Worlds information and theatre reports. The main menu and new-world
selector deliberately extend the originals to expose both campaigns. The renderer retains CP437 framing, original
colours, shadows, highlighted letters and character-cell positions.

Arrow keys and the original letter shortcuts follow the DOS hierarchy. Training
counts and wickedness use separate numeric-entry dialogs with their original
0–25 and 0–8 bounds. Pilot creation asks whether to activate the new pilot; Enter
confirms deletion and another key cancels. Insert switches the career overview
and its decorations to the four-column statistics table. Context help, all
seventeen documentation chapters and the original Other Worlds notice are
embedded. The Save as plain text entry exports `CORNCOB.txt` into the save directory
and shows its full path. The export removes in-game emphasis markers, converts DOS
line endings to ordinary newlines and replaces chapter form feeds with blank lines.
The embedded originals remain unchanged, including literal key names and DOS paths.

Theaters can be opened, activated, viewed and deleted without starting a sortie.
Repeated definitions have separate numbered THT files. New theater headers match
the original first 48 bytes, including the separate reserve-aircraft count at
offset 38. Deleting one compacts its
pilot's references and preserves any other active world. Trainee worlds remain
available for the current run and are discarded when it closes. Cached menu
summaries avoid reading and decoding every saved world on each rendered frame.
Combat shows the original starting-airfield screen and training its preflight
briefing. The live survey follows INT's vocabulary, objective descriptions,
integer ranges/bearings/altitudes, grouping and Borland sort order. Its complete
Morning text matches TOWERS.INF byte for byte, backed by 1,952 original object
formatter executions and 96 full grouping/sort cases. A single-airfield base
world reports all 64 tiles in the original 15-line scrolling document.

Empty fields count unwrecked planes in INT's start box. P boards a lower-powered
Dumpy at the saved start; Enter continues on foot. A credited return consumes one
stored plane, preserving THT word 38 across restarts. Aborted/invulnerable returns
do not consume stock; trainee reserves remain transient. Completed theaters warn
before launch and require Y before deletion. Activation offers resurrection for
dead/captured pilots; manual resurrection separately asks about activation.
Stockade confinement cannot be bypassed by resurrection.

F3 reproduces ISCORE's preliminary report on a temporary record, including its
assumed safe landing, time, score factors and record hearts. Training uses the
global pilot even when a combat theater remains active. Returns show the mission
report with the prior rank, promotions/medals in their original order, and the
post-mission career overview. Decorations use abbreviations and repeated glyphs
with fourteen per line. Unavailable pilots revert to the trainee after reports.

Eleven further unchanged-DOS screens match RGB pixels: populated score tables,
preliminary and mission reports, promotion, decoration, the post-mission overview,
survey, starting airfield and reserve warning. Availability, resurrection,
exhausted stock and deletion also have state/persistence tests. This does not
claim pixel identity for every possible record/dialog combination. The original
analog-centering labels remain; GLFW supplies digital axes without DOS drift
calibration. Reference provenance and research details are in the sibling
corncob-reverse tree's analysis/airfield-campaign.md.

F1 retains BOSS.TXT's DOS-prompt disguise. F2 and walking into a tower now use
the reconstructed TOWER.EXE briefing reader. It selects one of nine form-feed
sections, retaining the 22-row window, proportional scrollbar, original rich-text
attributes and 127-byte input-line chunks. Arrows move one line, PgUp/PgDn and
Space move twenty, U/D move eleven, Home/End and G/g reach the ends. The original
numeric and control-letter aliases also work. Enter returns the first Sxyth
start visible on the current page; Escape, Backspace and Q cancel it. An empty
section shows the original notice and accepts any key. F1 inside the reader
opens the boss disguise and restores the same briefing when dismissed.

Forty-nine complete unchanged-DOS screen captures match RGB pixels exactly,
including all four theatres, all nine section indices, generated training,
scroll limits, long lines, multiline emphasis, literal backslashes and the boss
round trip. Selected coordinates and absent/cancelled return files also match.
The captured expectations are retained in `tests/reference/tower_briefings.h`;
`tools/generate_tower_briefing_reference.py` imports them from the research
repository's `capture_native_tower.py` output. The game and tests require no DOS
executable or Python at runtime. These checks cover the helper's text and
selection behaviour; MOAG's live TOWERS.INF survey and reserve-aircraft allocation
are covered separately above.

The flight engine ignores a selected airborne start while the pilot is on foot,
and damaged aircraft retain the original jump restriction and message. An
airborne start explicitly releases the stopped runway latch so movement begins
without another throttle input. F3 reports the retained sortie without recording it.
F4 traverses ten original help pages. Pause also accepts Space for help and
P/Escape to resume, as documented on those pages. Eleven BLACKBOX records supply native warning
rectangles, text and placements. Flight Escape warns once when messages are enabled
and the pilot is beyond the tower's 5,200-unit range; a later Escape ends the sortie.
Control-C follows the distinct uncounted abort path.

Permanent scenery retains its original timer refreshes and nearby-update gate.
Ground crew keep their latched gesture or corpse pose; spy contact awards credit
once, and radar rotation follows the original fast tick word. Local contacts use
EVALDIST’s word-coordinate gate and integer magnitude, including aircraft boarding
and the separate departure/re-entry thresholds. All 576 complete native scenery
handlers and 512 distance cases match without stubs.

Walking into a tower opens intelligence and pauses the retained simulation.
DOTOWER continuation places the pilot outside the doorway, repairs the retained
aircraft and recharges its rocket, preserving historical worst damage. It does
not end or record a sortie. Invulnerable ground crashes level and bounce the
plane through RESETPLANE/BOUNCEVERT instead of killing the pilot. Rescue calls
create one friendly van; hostile vehicles draw an exposed pilot toward actual
capture collision bounds.

Named pilots retain changed worlds independently. Roster operations create,
activate, deactivate, rename, delete and resurrect pilots; records display career
statistics and original medal glyphs/colours. Three native high-score lists cover
missions, careers and integer points-per-sortie careers with at least three
sorties. The three required normal theatres grant Combat Master clearance; the
experimental aircraft's altitude condition leads to the Earth-saved ending.
Invulnerable and aborted flights leave career bytes and high-score lists unchanged.
World persistence remains separate from career credit.

All 5,040 bytes of the high-score update match 96 native insertion cases. Equal
points use objective, ground-kill and air-kill word totals as successive tie
breakers; equal integer career rates use total points. Signed mission counts,
retired names, empty rows and padding are retained. Existing careers only move
upward, including the original odd ordering after a declining average. Original
theatre titles are recorded. White Sands exceeds the original 22-byte theatre
field and overwrites the first statistics bytes; this saved-record quirk is
reproduced explicitly within bounds, without unsafe string writes.

PILOT.SCR retains opaque 623-byte records, both observed header layouts and the
original Borland random mask. PILOT.HSC retains its 5,040-byte layout and padding.
THT uses the native 671-byte container and compact 64-tile world stream. Both plain
and coordinate-XOR type words are accepted only after a complete world validates;
new saves use XOR. All four original definitions round-trip byte for byte. Save
replacement uses a completed temporary write. New worlds use numbered `THT/N.THT`
files linked by the pilot’s original twenty-entry table (`0x23c` count, `0x23d`
active index, `0x23e` file numbers). References preserve ownership even when DOS
has left the embedded pilot name stale. Earlier native slot-named files remain
readable. Header campaign words are preserved, though their full bookkeeping
is still being investigated. Renaming updates live high-score names; deletion
removes owned worlds and sets the original retired word in all three score lists.
A reused name starts a distinct career. Training counts/qualities and invulnerability
also use the original per-pilot setup bytes. New pilots retain the native five KLA,
six AAA and two vehicle sites, with strength-four vehicles and random KLA/AAA
strengths. The original menu limits each site count to twenty-five.

Career records retain distinct latest sortie, highest-scoring sortie, individual
peaks and cumulative totals; PgUp/PgDn switches between them. Native comparisons
cover all 540 bytes of counters/statistics/scores, including signed peaks and word
wrapping. Cumulative damage adds each sortie's maximum, rather than storing a
second peak. MOAG's campaign score is intentionally separate from ISCORE's flight
report: it adds successful suicide-objective credit and its ejection-factor test
contains an always-false mask. Both original paths are preserved.

Elapsed flying time is recorded in sortie and career statistics. Map inspection
counts toward flying time; pause, help, radio, tower and menu handoffs commit
whole seconds and stop the clock. Time advance affects physics without multiplying
flying time. The original elapsed-time helper ignores hours within each uninterrupted
segment and wraps its flight result at sixteen bits; both quirks are retained.
The two adjacent saved words are treated as a cumulative dword by MOAG.

The original campaign scan is now executed independently for 512 promotion,
medal and court-martial reference cases, including cached theatre lookup and
native string comparisons. Saved theatres keep their own career totals instead
of a copy of the global career. The completion word at header offset 46 prevents
repeat awards; offset 40 stores remaining objectives. A completion needs at least
one credited objective, and only the current, newly completed theatre grants
Theatre Combat/Distinguished Flying Cross medals. The latter requires no aircraft
lost across that theatre, rather than merely a clean final sortie. Combat Master
requires all three normal theatres and a surviving pilot. Friendly losses suspend
a living pilot for one day per loss, extend an existing sentence and skip that
sortie's awards. The release date survives restarts. Killed/captured pilots remain
eligible for the original resurrection operation; living pilots do not.

## Rendering, presentation and sound

The software renderer retains 640 x 350 indexed EGA output, three distance/detail
tiers, authored octant painter lists, fixed-point projection and focal-plane
clipping. Gauge lines use EGALINE's direction-normalised pixel decisions; model
wires use KLINE's horizontal spans and long shallow-line runs. Both paths match
304 complete native indexed frames. Wire viewport clipping retains the original
inclusive borders and X-first/Y-second order, verified against 512 native cases. OpenGL only presents the CPU framebuffer. Floating window sizing fits
available desktop space. Square pixels are the default, matching DOSBox with
aspect correction disabled; `--aspect crt` selects the original 4:3 CRT shape.
VGA presentations use 320 x 200; menus use 640 x 400 BIOS text. Integer enlargement
follows the current video mode so enlarged rows and columns remain uniform;
nearest-neighbour sampling preserves pixel boundaries. The original cockpit mask's stray diagonal is retained.

Ground-level runway pieces use CHECKFORRUNWAY's preliminary drawing pass;
elevated runway pieces retain ordinary distance ordering. Forty-eight scene
regions match unchanged DRAWNEAR calls across all three runway types, both
altitudes and eight view directions. The grey gunsight also appears on foot,
hides while walking and returns when stopped. Both viewport layouts match
complete original DRAWGS frames.

The control-tower door showing through its side/back walls is also an original
rendering quirk. The tower and doorway are separate objects: TBSORT compares their
centre distances, and the elevated tower centre causes its walls to be drawn
before the ground-level doorway. The doorway's polygon is drawn from either side.
Executing unchanged DISTEVAL, TBSORT and D3DOBJ reproduces this at eight ground-level
viewpoints; sixteen central scene regions (with and without the door) match the
reconstruction exactly after normalizing sky/ground to the probe's flat background.
[The fixture metadata](../tests/reference/native_tower.json) records the routine
addresses and scope. This quirk is retained for fidelity; it does not establish
identical painter-table ordering for complete moving scenes.

Cockpit buttons retain all fourteen native search masks and colour substitutions;
56 complete native frames cover their exact bounds. Bomb-supply annunciators,
flashing low-altitude/eject warnings, high-altitude marks and the rocket needle's
charging/ready/firing colours follow live state. Low-altitude hysteresis remains
active when explanatory message boxes are disabled. Autopilot and objective lamps
retain their original blue and bright-blue backgrounds.

Rocket charge follows the original BIOS clock, including time-advance scaling and
its overflow guard. The original leaves the stored charge unchanged when the
next increment crosses the readiness threshold; the green lamp and ignition
condition follow that branch. Discharged rockets recharge and cannot restart
on a partial charge. Native comparisons cover 1,024 clock updates, with a full
connected burn/recharge/reignition scenario.

The original per-frame particle block changes all thirty-five large-cloud,
small-cloud and flotsam discs, including their individual positions and diameters.
It consumes 141 shared random draws after physics and supplies the following
frame, with unchanged authored colour references. All 512 complete native blocks
and the model-resource bindings match. Previously these clouds retained static
geometry. The shared random headquarters colour now also comes from that original
block rather than an extra draw during display refresh.

Ground dots and stars now use their runtime generators rather than a fixed star
list. Complete startup retains all three eighty-point banks, including the unused
far bank and final generated records beyond the draw loop. The 512 ground-point,
256 star and 64 complete initialisation cases match native positions and PRNG
continuation, including signed range wrapping and underground no-ops.

Original instruments, propeller, ground points, stars, sun directives, radar,
homing pointer and animated model geometry are connected. Wire forcefields pulse
with matching collision bounds; saucer discs pulse in opposition. Quality-zero
filled barriers reveal their distant representation briefly on contact. Null
model pointers intentionally draw nothing. Radar retains its unusual square-root
range compression and draws relative to the aircraft after ejection. Num Lock and
Control select eased head views. The overhead map retains depth reduction, stored
survey/player markers, pan/zoom/rotation and frozen sortie state.

High-G vision filters the original vertical force accumulator before reducing the
six-bit DAC palette. Capture retains its fractional fade word, including native
word wrapping; ejection and tower return restore the intensity byte. Each scene
uses the resulting original quantised palette rather than an RGB overlay.

Original VGA/IMG data and presentation scripts supply two story panels, fifteen
outcomes and ten help screens. MEDAL.FNC replaces sixteen BIOS glyphs; ten award
entries use MOAG's original pair/colour table at file offset `0x1fa82`.

The story now precedes the animated CCTITLE sequence, with its sixteen-step DAC
fades. The flyby retains reflected terrain scrolling, plane and saucer sprites,
flak, muzzle flashes and the initial single pass. Later repeats retain the
original three-pass continuation. Lettering keeps five moving cloud-detail bits
and replaces the upper three index bits from the authored letter masks; its
separate GRADSKY palette, threshold fade and scrolling credits are retained.
The per-frame motion, state transitions and Borland PRNG continuation match
1,024 unchanged native passes; 6,144 composited pixels match original VGA writes.
The original CRTC page setter does not wait for retrace, so playback here uses a
stable 20 Hz clock rather than depending on CPU or DOSBox cycle speed. This chosen
presentation pace gives the five-step flak bursts 250 ms and the 176-row credit
scroll 8.8 seconds, replacing the initially too-fast 70 Hz playback without
changing the native motion steps. The title score uses the clean original-driver
recording described below. Whole title frames, including sprite clipping and text
rasterisation, are not yet claimed pixel-exact.

The Earth-saved transition retains the indexed cockpit, shrinks the Earth in the
rear window and fades only the sky before the SRATS starfield. Enter toggles warp
trails; Escape continues to outcome/report. The target flashes white after 270
warp ticks, unlike the later registered-build source conditional. Both EGA pages
retain independent trails; BIOS animation time continues while flight is stopped.

The twelve original 3.ADL patches drive Nuked OPL2; miniaudio supplies 48 kHz stereo
PCM. This is mono AdLib duplicated into two channels, not new spatial stereo.
Engine state, launches, explosions and nearby threats drive the original channels.
Crash timing continues through eighteen BIOS ticks and a 750 ms wait independently
of the menu loop. Register batches cross to the PCM callback without sharing live
world state. Muting does not queue stale events. Inactive chip output is suppressed while its
clock continues, so held envelopes do not leak underneath the menu music. Proximity inputs now come from
the actual actor handlers, preserving their overwrite order, KLA launch rest and
persistent quality. They are latched for the following frame, as in the original
pre-DRAWNEAR sound block. Radar-derived saucer attenuation uses compressed
horizontal range relative to the retained aircraft after ejection. Saucer pitch
tracks remaining health before objective-saucer repair; orb and KLA ranges keep
their distinct original scales. Orb-to-saucer transitions restore the intervening
screech patch, and KLA shutdown retains native VOICEOFF packing. All 10,535 OPL
writes in 512 connected native proximity frames match; 384 ordered full actor
passes independently verify the range/pitch inputs with real RADARPROCESS maths.
Audio is updated per simulation pass, so catch-up frames retain their events.
Complete combined DOS soundscape and interrupt behaviour still need broader comparison.

Music is recorded offline from unchanged MOAG and its bundled Ad Lib V1.51
driver. Private DOSBox copies repeat each ROL/CCMUSIC score twice; both complete
note-on streams must match before export. DRO captures preserve original register
values and millisecond timing. The pinned Nuked OPL emulator renders their mono
DAC output directly, with no normalisation or gain changes. FLAC masters use
maximum preset compression (`-8`); only Ogg Vorbis quality-6 copies are embedded.
The normal-game FLACs in the research audio browser now use these masters,
replacing the earlier AdPlug approximations at the same paths.

| Score | Complete native repeat boundary |
| --- | ---: |
| DESTINY3 | 84.834 s |
| RIDINGT | 204.155 s |
| DREAMSO2 | 178.795 s |

These boundaries include the short native restart gap and exclude the next note.
They come from driver resets and matching complete score passes, not an arbitrary
capture duration. Actual CCTITLE output independently confirms the same musical
register sequence; its separate effects are excluded from the music master.
The clean title score repeats during the animated title, and the two menu scores
cycle in playlist order. Story panels, flight and its helper pages, original
outcomes and the starfield ending remain without music.

A small Vorbis player reads the embedded bytes using the decoder already supplied
with the pinned miniaudio source. Decoders open before the callback starts; only
an atomic cue crosses from the interface thread. Complete tracks and playlist
transitions retain every sample across arbitrary callback sizes. Music uses the
same PCM device as the existing live AdLib effects. The main-menu music toggle saves the
original global music flag at byte 21 of the decoded 24-byte PILOT.SCR header
(`FF` on, `00` off). Older 23-byte rosters predate this flag and are upgraded only
when the music preference is written, preserving their pilot count and records.
`--no-music` overrides startup without writing the saved preference.

[Recording provenance](../assets/music/provenance.json) lists original inputs,
capture hashes, musical repeat checks and encoding settings. Lossless masters
and DRO files live in `corncob-reverse/analysis/resources/audio/`; the game needs
none of those files. `tools/render_music_capture.cpp` and `tools/import_music.py`
reproduce the masters and embedded copies from the retained captures.

## Verification

Fixtures execute unchanged target routines in development. Their JSON metadata
records executable/fixture hashes, addresses, scope and any explicit stubs. Ordinary
CTest runs are C++ only; checks do not merely compare two translations of a formula.

| Area | Independent native coverage |
| --- | --- |
| Fixed maths | All 65,536 sine inputs; 8,228 airflow angles; 512 forward/inverse rotation sets; 512 physics and fast vertex transforms each; 128 matrix compositions |
| Landing maths | 1,024 magnitudes; 256 wheel clearances; 512 suspension/brake passes; 512 invulnerable bounce cases |
| Flight | 256 force passes, including intermediate accumulators; 128 position integrations; 512 controls and attitude passes each |
| Portals | 512 unchanged destination/PRNG cases and 256 complete timer/colour updates; connected delayed objective transfer and damage/repair |
| Rocket recharge | 1,024 native BIOS-clock updates, including time advance, wrapping and readiness; connected discharge/recharge/reignition |
| Autopilot/beginner | 512 feedback cases covering both models, stall state and engagement combinations |
| Joystick | 512 SPKE/JOYTD response cases, plus keyboard/button/head-view integration |
| Person | 512 free-fall damping and 512 parachute swing cases |
| Motion/collision | 512 generic movement, 512 strict boxes, 512 fast and 512 ordinary projectile contacts |
| Damage/actors | 256 aircraft damage passes; 512 KLA and 512 living saucer passes; 512 vehicle updates with capture/rescue flags and PRNG continuation; 1,536 AAA/balloon/bee/alien updates, including shell initialization; 768 launch-site passes and 133 complete child constructors, including exhausted pools; 768 orb/objective/mindbender passes without stubs; 256 mortar passes and 104 complete emitted particles |
| Destruction | 992 hit callbacks and 128 paired contacts; complete object/models, 130 score bytes, 3,916 emitted effects and PRNG continuation |
| Scenery | 512 parked-aircraft handlers, 256 tower reset cases, 576 complete permanent handlers, 512 local interaction ranges and connected tower/boarding/contact checks |
| Debris/training | 192 complete debris/shard/explosion constructor runs; 512 shard colour/PRNG cases; 2,157 generated placements from 32 training runs |
| Raster/resources | All 224,000 cockpit pixels; 30 complete disc masks, 12 polygon hashes, 304 line frames and 512 wire-clip cases; four original definition round trips; one 252-object plain saved world |
| Ground/stars | 512 point regenerations, 256 stars and 64 complete three-bank initialisations with PRNG continuation |
| Particles | 512 complete 35-disc/shared-colour frame blocks and PRNG continuations, plus resource bindings across the original models |
| Model/map/radar | 64 tower/doorway/runway scene regions; 512 geometry-animation, 512 map reductions, 512 radar positions; all 32,768 compressed radar-distance inputs |
| Visual feedback | 512 force/fade states and 512 complete DAC palettes, including capture and frame wrapping |
| Title | 1,024 original animation-state/PRNG passes and 6,144 VGA letter-mask pixels |
| Ending | All 768 initial stars and 64 complete indexed native star frames |
| Animation clock | 512 native word-counter transitions and a connected 16,384-pass restart |
| Sortie clock | 512 unchanged two-segment DOS-clock conversions, including pause boundaries and wrapping; connected flight/pause/map timing |
| Score/persistence | 512 ISCORE calculations, 256 complete MOAG career updates and 512 complete promotion/medal decisions; 16,384 original mask bytes; original control/model/new-pilot fixtures; 96 full 5,040-byte native high-score insertion/tie/padding cases and exact retirement; roster/world restart, rename and deletion scenarios |
| Tower intelligence | Forty-nine complete unchanged-DOS screen/mission-return comparisons, all nine briefing sections, original scrolling and boss continuation |
| Menus | Thirty-one complete unchanged-DOS screens, populated reports and honors, navigation/readers, training editors, theater ownership, reserve exhaustion and resurrection/stockade transitions |
| Airfield survey | 1,952 original descriptions, 96 original grouping/sort cases and byte-identical Morning TOWERS.INF |
| Music | Three native-driver recordings; complete title/menu repeats, arbitrary 1–512-frame callbacks, saved native music flags and older-roster import |
| Sound | 512 engine/register-write cases; 10,535 proximity writes across 512 connected frames; 384 ordered actor-proximity passes; finite, non-silent PCM |

Connected scenarios cover takeoff, ejection/parachute/remote control, weapons and
persistent objectives, rescue/capture, damaged-aircraft tower visits, settings,
career clearance and the ending. A sustained run exercises 32 original-theatre
starts with mixed controls/weapons/ejection, more than 31,000 updates and 250
rendered frames, then round-trips resulting worlds/results. An additional
`endurance_checks --draw-every-frame` run completed 31,802 full simulation/render
passes across the same 32 starts in Release and under AddressSanitizer/UBSan.
The original sustained run exposed the hidden
far-model crash before interactive playtesting.

Private GLFW/X11 checks traversed pilot creation, mission selection, radio boxes,
map, boss screen, help, score, abort/report, tower intelligence, repair/resume and
Escape warnings, followed by both story fades and the animated title/credits. A further window
traversal checked the saved training defaults, live bomb-supply indicators, flap
state and cockpit warning colours. Pause/Space help, help-page continuation
and P/Escape return were also inspected in the actual window. A clean Release
installation contains only the embedded executable and opens without the research
workspace or external game assets. An installed music-enabled executable was
also checked through a private audio server containing only a null sink: music,
a sustained digitally silent mute and resumed playback were captured from the
actual window. The installed package still contains only `corncob`.
Captures were inspected without changing supplied DOS installs. The complete
54-check Debug and Release suites pass, including twenty full-screen DOS menu
references and the longer gameplay scenarios. The five affected menu, pilot,
score and campaign checks also pass under AddressSanitizer/UBSan. The MinGW GCC 16
Release build passes all 54 checks under Wine; its earlier window checks included
menus, training, flight and stepping out on foot. A further Linux GLFW traversal
checks the restored roster, nested training editors, combat/airfield screens,
preflight briefing and entry into a live training flight.
It imports only Windows system DLLs. This does not replace testing on Windows
hardware. Complete frame ordering, macOS runtime and interactive Wayland sizing
are not yet independently established.

Ground vehicles use the original renderer distance tiers for speed/pursuit, with
actual distance checked separately for capture. Near stopped vehicles bypass the
random reset draw; other resets preserve the original follow/random ordering.
Friendly and hostile stopped vehicles have distinct paths, and expired wrecks
are removed. All 512 native movement, capture/rescue and PRNG-continuation cases
match without stubs; the connected rescue scenario still passes.

## Remaining fidelity work

- Keep the intermittent training re-entry report open: the pilot was reported
  walking through the aircraft after leaving and returning. Re-entry later worked
  for the reporter; no cause or corrective gameplay change has been established.
- Extend whole-frame title comparisons beyond native motion, palette masking and
  the inspected presentation captures.
- Broaden conditional-dialog/record screenshot combinations and longer campaign runs; unlabelled theatre-header/configuration bytes remain preserved rather than reinterpreted.
- Broaden complete campaign comparisons against native executions.
- Broaden complete cockpit/gauge-frame comparisons and whole scene painter/
  collision ordering.
- Broaden connected special transitions against longer DOS sessions. Scripted
  coverage is not proof of every transition.
- Refine original painter-table handler/primitive interleaving before claiming
  identical whole-session random continuation; display refreshes now preserve the
  shared simulation stream.
- Compare the complete soundscape and confirm platform/device behaviour beyond
  the Linux development host.

## Recovered source experiments

`--slalom` restores the `KA.INC` slalom/guns=0/saucey=0 training branch as an
optional standalone run. The common training placements and PRNG sequence are
retained, followed by the source's three NPC aircraft, fifty trees, farm building,
a large blue crater polygon (apparently a pond) and two rows of six tall Tet markers.
The standalone source defaults `naas`, `nvest` and `ndbst` to zero, so this run
contains no AAA, vehicle or KLA sites; the three common forcefield barriers remain.
The checkpoint logic checks
ordered X-sector crossings, alternating sides of the original Y centreline, and
resets on a mistake or ground contact away from home. Completing all six awards
one local objective and queues the original generic mission-objective congratulations
message. Its normal F2/airstrip advice is retained even though this experiment does
not use the combat campaign. Source progress rectangles replace the bomb indicators;
player guns, missiles, dropped bombs and planted charges are disabled. Training
invulnerability is enabled. Exiting this experiment does not update pilot careers.

The hidden geometry is bundled as 935 bytes of generated data in
`resources/source_models.h`, with original-source hashes in
`assets/source_models_provenance.json`. No assembly instructions run in the port.
The independent extraction tool and browser in corncob-reverse also expose the
otherwise unused foliage-only tree helper. These are source-only reconstructions,
not comparisons against a retained released slalom executable. The source's
slalom object handlers and layouts are experimental, and no separate released
slalom edition has been identified.

The square and cross at the course origin are the shared 20-metre survey marker,
not a replacement generator. The three airborne props reuse the saucer-site type
with aircraft geometry and a straight-ahead movement handler; they neither fight
nor launch enemies, and cannot be boarded as parked player aircraft. The farm
uses GMDEST's fuel-dump wreck transition rather than saucer rolling/tumbling.
Tree collision callbacks are inert, and damage to traffic aircraft does not reset
a saucer launch timer. These source-only branches are covered separately from
the retained normal-game handlers.

`--graphics-mode` makes the `3DVID.INC` flight profiles selectable at runtime:
`ega` uses 640x350 colour, `ega-low` uses 320x200 colour, and `hercules` uses
720x348 monochrome. Scenery is projected and rasterized at the selected native
resolution, rather than shrinking a completed 640x350 scene. Hercules uses the
source's BKCLRS fill masks and HGCLINE solid-line interpretation. Default EGA
continues to pass the original pixel/raster/scene reference checks.

The alternative source paths are incomplete as complete historical builds:
several helpers and full-view paths assume the usual dimensions, and alternate
cockpit bitmap assets were not preserved. The port therefore adapts the known
cockpit and instruments with nearest-neighbour sampling, derives full-view bounds
for those modes, and retains the established display modes of menus, text helpers
and the ending. These options recover the alternate scene-rendering intent;
they do not claim an exact reproduction of an unknown shipped low-resolution or
Hercules edition.

Cockpit framing and the airflow ball retain the native clipping rules: EPAGE
writes the right frame at pixels 632–639, and DRAWMDS passes the radar window
(496,259)–(615,327) to DRAWDisc. In particular, the zero-speed ball projects
outside that window and must remain hidden. The research tool
`tools/verify_cockpit.py` executes both original routines without stubs. Warnings
now overlay the retained flight surface without changing its dimensions; alternate
modes adapt only the message and lower cockpit artwork, so warnings preserve the
display scale and the raw mask's construction diagonal cannot leak into corners.
