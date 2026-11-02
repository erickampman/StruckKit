<div style="display: flex; align-items: center; gap: 0.75rem; margin: 1.5rem 0 0.5rem;">
  <a href="https://www.unlikelyware.com" target="_blank" rel="noopener noreferrer">
    <img src="Unlikelyware.png" alt="Unlikelyware" style="width: 56px; height: auto; border-radius: 10px; margin: 0; display: block;">
  </a>
  <h1 style="margin: 0;">StruckKit</h1>
</div>

<p>
  <em>StruckKit is a CLAP plugin for macOS (15 and later) and Linux. It needs a host that loads CLAP plugins.</em>
</p>

~~~note [card]
## On This Page
[Overview](#overview) · [Getting Started](#getting-started) · [The Kit](#kit) · [The Panel](#panel)

**The drums —** [Crashes](#crashes) · [Snare](#snare) · [Toms](#toms) · [Hi-Hats](#hats) · [Hand Drums](#hand-drums) · [Percussion](#percussion) · [Master](#master)

**Playing —** [MIDI](#midi) · [Presets](#presets) · [Tips](#tips) · [Troubleshooting](#troubleshooting)
~~~
~~~note [card]
(#overview)
## Overview
StruckKit is a **drum kit in one plugin**: kick, snare, toms, crashes and hi-hats, plus congas, bongos, timbales and small percussion, all played from one instance on one MIDI track.

Every drum except the kick is a **physical model**. Instead of playing back samples, StruckKit works out how the real instrument vibrates and lets those vibrations ring:
- **Snare and toms** are two-headed drums: the batter head's own vibration modes, the resonant (bottom) head coupled to it through the air inside the shell, and, on the snare, eight wires resting on the bottom head that rattle when it moves.
- **Crashes, ride and hi-hats** are bronze plates with around a thousand modes. Hard hits on thin cymbals spread their energy upward into a wash, as real ones do. The hi-hats are the same plate with a second cymbal underneath, touching it or not.
- **Congas, bongos and timbales** are single-headed drums open at the bottom. The timbales' metal shells ring too.
- **Cowbell, agogo, claves, wood blocks and triangle** are struck bells, bars and blocks, each with its own series of modes.
- **The kick** is a swept sine with a beater click. It isn't a physical model and has no controls; only Level affects it.

Because the drums are models, **velocity changes the sound, not just the level**: a soft hit is a longer contact, which darkens the tone, and a hard hit on a tom or hand drum starts slightly sharp and settles, as a real head does. The knobs are physical too: a cymbal's diameter, a head's tension, where the stick lands.

Every drum rings out on its own; releasing the key doesn't stop it.
~~~
~~~note [card]
(#getting-started)
## Getting Started
1. **Add StruckKit** to an instrument track in your host.
2. **Play the kit.** With the default map, C1 (note 36) is the kick and E1 (note 40) the snare; the legend at the top of the panel shows every key. Notes 36–47 are the kit, 48–59 the hand drums and percussion, and the pattern repeats every two octaves.
3. **Using GM drum patterns?** Set **Map** (in the Master panel) to **GM**, and General MIDI note numbers play the matching drums.
4. **Shape each drum** with the knobs in its panel, and set the overall level with **Level**.

Your settings are saved with the host's project.
~~~
~~~note [card]
(#kit)
## The Kit
**Map** chooses which note plays which drum.

**StruckKit map** (the default): two octaves, repeating every 24 notes. The kit octave is 36–47, 60–71, 84–95 and so on; the percussion octave is 48–59, 72–83, 96–107 and so on. In both, the white keys are drums. In the kit octave the black keys are the cymbals; in the percussion octave they are the small metal and wood instruments. Notes below 36 follow the same pattern.

| Key | Kit octave (36–47, 60–71, …) | Percussion octave (48–59, 72–83, …) |
|---|---|---|
| C | Kick | Tumba (low conga) |
| C# | Crash 1 | Cowbell |
| D | Kick 2 | Conga |
| D# | Crash 2 | Claves |
| E | Snare | Conga, muted |
| F | Rimshot | Bongo, hembra (low) |
| F# | Hi-hat, open | Wood block |
| G | Floor tom (16") | Bongo, macho (high) |
| G# | Hi-hat, half-open | Triangle, muted |
| A | Rack tom (12") | Timbale, low |
| A# | Hi-hat, closed | Triangle, open |
| B | Rack tom (10") | Timbale, high |

**GM map**: General MIDI percussion note numbers. Where GM has a variant StruckKit doesn't model, it plays the nearest drum: the pedal hi-hat is the closed hat, the Chinese cymbal is Crash 1, the splash is Crash 2, and side stick is the rimshot.

| Drum | GM notes |
|---|---|
| Kick, Kick 2 | 36, 35 |
| Snare | 38, 40 |
| Rimshot | 37 |
| Floor tom (16") | 41, 43 |
| Rack tom (12") | 45, 47 |
| Rack tom (10") | 48, 50 |
| Hi-hat, closed | 42, 44 |
| Hi-hat, open | 46 |
| Crash 1 | 49, 52 |
| Crash 2 | 57, 55 |
| Ride | 51, 59 |
| Ride bell | 53 |
| Congas (low, high), muted | 64, 63, 62 |
| Bongos (low, high) | 61, 60 |
| Timbales (low, high) | 66, 65 |
| Cowbell | 56 |
| Agogo (high, low) | 67, 68 |
| Claves | 75 |
| Wood blocks (high, low) | 76, 77 |
| Triangle, muted and open | 80, 81 |

The ride, ride bell, agogos and low wood block are only in the GM map, and the half-open hi-hat only in the StruckKit map. GM percussion StruckKit has no model for (clap, tambourine, maracas, cabasa, guiro, whistles, cuica, vibraslap) is silent.

**Chokes.** The hi-hats cut each other off: a closed hat stops a ringing open one within a few milliseconds, as closing the pedal does. The two triangles do the same, so a muted triangle stops an open one. Nothing else chokes.
~~~
~~~note [card]
(#panel)
## The Panel
**Across the top:** the Unlikelyware logo (click it to open unlikelyware.com), the title, and on the right **Save** and **Load** (see [Presets](#presets)), **Help** and the **Preset** menu.

**The legend** under it shows which key plays which drum for the current map. With the StruckKit map it draws the kit octave and the percussion octave as two keyboards; with GM it lists the note numbers by drum. Keys and names are coloured by drum family, matching the panels below.

**Seven panels** follow, in two columns: Crashes and Snare, Toms and Hi-Hats, Hand Drums and Percussion, with Master beside Percussion. A panel's title names its drums, so each knob is captioned by just its control.

**Knobs:**
- **Drag** up to turn a knob up, down to turn it down; one full drag covers its whole range. The scroll wheel turns it too.
- **Ctrl** while dragging or scrolling moves it ten times more finely.
- **Shift-click** sets it back to its default.

Each knob shows its value under it. All the drum knobs read **0.00 to 1.00**, the position of the knob in its range; the sections below give the real quantity each one sets. **Level** reads 0.00 to 2.00.

**Menus** (Preset, Map, Voices): click to open, then click a choice. Clicking elsewhere, or switching to another window, closes the menu.

The editor can be resized to any shape from 864 × 640 up (the full size is 1080 × 800); everything scales with it.
~~~
~~~note [card]
(#crashes)
## Crashes (and ride)
Two crash cymbals, C# and D# in the kit octave, modelled as free-edge bronze plates. They are different cymbals, not one cymbal at two sizes: Crash 2 is also slightly thinner and more domed, which puts its low clang well above Crash 1's.
- **Size 1**: Crash 1's diameter, from 13" to 22". Default 20". Bigger is lower and longer.
- **Size 2**: Crash 2's diameter, from 13" to 22". Default 14".
- **Thin**: the thickness of both crashes, from heavy (1.2 mm) to thin (0.6 mm). A thinner cymbal swings further for the same hit, so it blooms into a wash sooner and more easily.
- **Decay**: how long both crashes ring, set as the bronze's internal loss: short at the left, long at the right.
- **Stroke**: where and how the stick hits. At the left, the shoulder of the stick on the edge: a crash. Toward the right, the tip of the stick on the bow, nearer the centre: a sharper, ride-like ping.

**The ride** (GM map only: notes 51, 59, and 53 for the bell) is a 21" cymbal, heavier and longer-ringing than the crashes, played with the tip on the bow or on the bell. It has no panel of its own: it follows the crashes' **Thin** and **Decay**. Size and Stroke don't affect it.
~~~
~~~disclosure How the cymbals are modelled [disclosure-title subtle]
Each cymbal is a plate with about a thousand vibration modes: the lowest worked out exactly for a free-edge plate, the rest approximated. The dome stiffens it, where the stick lands decides which modes it drives, higher modes lose energy faster, and a bigger cymbal radiates its low modes better.

What makes a cymbal a cymbal rather than a bell is a **nonlinear cascade**: energy flows from the low modes the stick excites into higher ones, building the shimmer after the hit. How strongly it flows follows how far the rim swings compared with the cymbal's thickness, so hard hits on thin cymbals bloom into a wash and soft ones stay clean. The cascade moves energy without creating it.
~~~
~~~note [card]
(#snare)
## Snare (stroke, rimshot)
E plays a normal stroke and F a rimshot (kit octave). The snare is a two-headed drum: the batter head's vibration modes, the snare-side head coupled to it through the air in the shell, and eight wires resting on the snare-side head.
- **Tune**: the batter head's tension, which sets its pitch. The head's fundamental runs from 150 Hz to 300 Hz across the knob (as an unloaded head; the air in the shell pulls the sounding pitch around that).
- **Ring**: the head's damping, from muffled to open. The ring time runs from about 0.12 s to 1.5 s.
- **Position**: where the stick lands, from the centre (rounder, darker) toward the edge (more ring and overtones). The rimshot always lands near the edge and ignores Position.
- **Tension**: the snare wires' tension. Loose wires give a long, loose buzz; tight wires a short, crisp burst. Tighter wires also damp the snare-side head more.
- **Wires**: how loud the wires are, from none (a snare with the wires off) to full.

The **rimshot** is always a hard hit: the stick strikes head and rim together, with a crack from the rim.
~~~
~~~disclosure How the snare wires work [disclosure-title subtle]
The wires don't add noise on top of the drum. They follow the snare-side head's motion: when the head moves far enough to lift them they slap back against it, so they rattle in step with the drum, harder after a harder hit, and stop by themselves when the head no longer moves enough to lift them. Each of the eight strands sits at a slightly different tension and position, so their impacts interleave instead of landing together.
~~~
~~~note [card]
(#toms)
## Toms (16" floor, 12", 10")
G, A and B in the kit octave: a 16" floor tom and 12" and 10" rack toms. They are the snare's two-headed drum without wires, at real sizes and with deeper shells, so they sustain longer. All four knobs act on all three toms.
- **Tune**: the heads' tension. All three move together and keep the intervals their sizes give them (the smaller toms are tuned a little tighter, as drummers do). Across the knob the floor tom's fundamental runs from about 65 Hz to 135 Hz and the 10" from about 125 Hz to 255 Hz.
- **Ring**: the batter head's damping, from muffled to open. The ring time runs from about 0.25 s to 2.5 s; the floor tom rings longer than that and the 10" shorter.
- **Position**: where the stick lands, from the centre toward the edge.
- **Reso**: the resonant (bottom) head's tuning against the batter head, from three-quarters of its pitch to one and a half times it. Near the middle the two heads trade energy and the drum sustains longest; further apart, the note gets shorter and more defined.

Hard hits start slightly sharp and fall as the head settles (the tom's "doom"), more so the harder you hit.
~~~
~~~note [card]
(#hats)
## Hi-Hats (open, half, closed)
F# (open), G# (half-open) and A# (closed) in the kit octave. The hi-hat is the crash's plate model with the bottom cymbal as a second plate underneath: open, the two never touch; half-open, they chatter against each other; closed, they are clamped together for a short "chick". A new hi-hat hit cuts off the one before it.
- **Size**: the cymbals' diameter, from 12" to 15". Default 14".
- **Thin**: their thickness, from heavy (1.4 mm) to thin (0.8 mm).
- **Sizzle**: how close together the half-open cymbals sit, and so how much they chatter. Higher is closer and more sizzle. Sizzle affects only the half-open hat.
~~~
~~~note [card]
(#hand-drums)
## Hand Drums (congas, bongos, timbales)
The white keys of the percussion octave. All are single-headed drums open at the bottom, so the lowest mode drains quickly down the shell and the pitch you hear comes from the next one up.
- **Congas**: head tension for the three congas (tumba, conga, muted conga), together. Across the knob the tumba's open tone runs from about 150 Hz to 220 Hz and the conga's from about 215 Hz to 320 Hz.
- **Bongos**: head tension for both bongos. The hembra (low) runs from about 350 Hz to 510 Hz, the macho (high) from about 490 Hz to 720 Hz.
- **Timbales**: head tension for both timbales, together from about 270 Hz to 510 Hz.
- **Ring**: head damping for all of them, from muffled to open. The congas and timbales ring longest, the bongos shorter; the timbales' metal shells ring longer with it too.

The **muted conga** is the open conga with the hand left on the head: it's gone within about a tenth of a second, whatever Ring is set to. Congas and bongos are played with the hand, a softer, longer contact than a stick; timbales with sticks, with a ringing metal shell.
~~~
~~~note [card]
(#percussion)
## Percussion
The black keys of the percussion octave: cowbell, claves, wood block, and the muted and open triangle. In the GM map, the two agogo bells and the low wood block too. These are struck bells, bars and blocks, each with its own series of modes; the triangle is a bent steel rod.
- **Tune**: the size of all of them together, from 5 semitones down to 5 semitones up.
- **Decay**: how long all of them ring, from half to twice their natural ring.

The muted triangle stops a ringing open one, as a hand closing on it does.
~~~
~~~note [card]
(#master)
## Master
- **Level**: the output level of the whole kit, from silence (0.00) to twice the default (2.00). Default 1.00. At 1.00, full-velocity hits peak just under full scale.
- **Map**: which note plays which drum, **StruckKit** or **GM** (see [The Kit](#kit)). The legend changes to match.
- **Voices**: how many drums can sound at once: **10**, **16** (the default), **20** or **32**. Past that, the oldest one still ringing is cut off for the new hit. 16 is enough for rolls under ringing crashes; raise it if long cymbals get cut short in busy parts.

The **kick** (C and D in the kit octave) has no controls of its own; only Level affects it. Kick 2 is higher and tighter than Kick.
~~~
~~~note [card]
(#midi)
## MIDI
StruckKit has one note input, which takes **MIDI 1.0 and MIDI 2.0**, and prefers MIDI 2.0.
- **Every channel** plays the kit.
- **Note on** strikes a drum. **Velocity** sets how hard: it changes the tone as well as the level. With MIDI 2.0, velocity has 16-bit resolution instead of 7-bit.
- **Note off** is ignored: every drum rings out on its own (or until it's choked or its voice is taken by a newer hit).
- Controllers, pitch bend and pressure have no effect.

Notes and knob changes take effect at their exact sample position within the host's buffer, not at the start of the buffer.

Every control is a plugin parameter, so the host can automate it. The host sees the drum knobs as 0 to 1, as the panel shows them.
~~~
~~~note [card]
(#presets)
## Presets
The **Preset** menu, top right, lists the factory presets. It shows **--** until you pick one. Choosing a preset sets every control it has a value for; any it doesn't mention keep their current values.

There is one factory preset so far, **Init**: every control at its default, with the StruckKit map. It doesn't set Voices.

**Your own kits:** **Save** and **Load**, left of Help, write the kit to a file and read one back, through the system's file dialog. The first time, the dialog opens on **Documents/StruckKit**; after that, on the last folder you used.
- A saved kit has every control, Voices included. Loading one sets every control the file has a value for.
- The menu then shows the kit's name, dimmed, in place of a factory preset's.
- The file is the same JSON as the factory presets, so a kit you save can be put in the source's `Presets` folder to build it in as a factory preset.
- The host still saves every control with its project, whether or not you saved it to a file.
~~~
~~~note [card]
(#tips)
## Tips
- **Play with velocity.** The models respond to it in tone as well as level, so a pattern with varied velocities sounds much more like a drummer than one at a fixed velocity.
- **Thin crashes and hard hits** give the biggest wash. For a cleaner, more controlled crash, turn Thin down or play softer.
- **Stroke toward the right** turns a crash into a ride-like ping, which is useful with the StruckKit map, which has no ride.
- **For a dead, punchy tom**, turn Ring down and Reso away from the middle. For a long, singing tom, Ring up and Reso near the middle.
- **A snare without wires**: Wires all the way down.
- **Retune the hand drums to the song.** Each family's knob keeps its drums' intervals, so one knob moves a whole set of congas or bongos.
- **Two crashes, two sizes.** Size 1 and Size 2 are independent, so you can have a large dark crash and a small bright one.
~~~
~~~note [card]
(#troubleshooting)
## Troubleshooting
- **Some notes are silent.** Check **Map**. A GM drum pattern played through the StruckKit map (or the other way round) lands on the wrong drums or on notes the map leaves silent. In the GM map, unmodelled percussion (clap, tambourine and so on) is silent.
- **A drum plays, but not the one you expected.** Look at the legend at the top of the panel: it always shows the current map.
- **Cymbals cut off in busy passages.** All the voices are in use, and the oldest is being taken for new hits. Raise **Voices**.
- **The open hi-hat stops early.** Another hi-hat hit chokes it, as on a real kit. The same goes for the open triangle and the muted one.
- **The kick doesn't respond to any knob.** It has none; only Level affects it.
~~~
