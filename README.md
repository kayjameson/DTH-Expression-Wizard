# DTH Expression Wizard

An Unreal Engine 5.8 editor plugin for facial expressions on DazToHue Genesis 9 characters. It builds an expression
library from the Daz Studio expression packs you own, and gives you a Mixer: pick expressions, dial each one in, see
the result live on your character, and key it into an animation sequence.

The sequences it writes are plain animation sequences holding only the DTH face-board curves
(`CTRL_expressions_*`). DTH's own post-process blueprint and face rig turn them into the face, exactly as they do for
DTH's face board. The plugin creates no blueprint, rig, pose asset or morph, and changes nothing on your character, so
one library works for every DTH Genesis 9 character on the same skeleton.

## Requirements

- Unreal Engine 5.8 (Windows).
- A Genesis 9 character imported with DazToHue (for example with the DTH Import Wizard): the DazToHue content pack,
  the character's copy of DTH's post-process blueprint, and the face-board curves registered on the mesh.
- Daz Studio with Genesis 9, the DazToHue Daz content (its ROM presets, `DazToHue/Poses/Genesis 9/DQS`), and the
  expression packs you want in the library.

## Install

Copy the `DTHExpressionWizard` folder into your project's `Plugins` folder and restart the editor. Open it from
**Tools > DazToHue > DTH Expression Wizard**.

## 1. Sample your expressions in Daz Studio

The library is made from what Daz Studio itself does with each preset, so Daz runs once per library build.

1. Save or close your Daz scene: the script replaces it with a fresh Genesis 9 and refuses to run over unsaved work.
2. In Daz Studio, open `Resources/Daz/DTH_SampleExpressions.dsa` from the plugin folder (File > Open, or
   double-click it). The wizard's Settings page shows the path and has **Copy Path** and **Show in Explorer**.
3. It finds every expression preset in your content library the way Daz content is laid out (pose presets in any
   folder named `Expressions` under `People/`, in every content directory), records DTH's face ROM and each preset,
   and writes `Documents/DTH Expression Wizard/DTH_ExpressionSamples.json`. Folders made for another figure
   generation are listed and skipped. Nothing is saved in Daz. Expect a few seconds per preset. To include presets
   kept elsewhere, run it with `var args = {folders: ["Your/Folder"]};` defined first.

## 2. Build the library

On the **Settings** page, set **Daz Samples File** to that JSON, pick a **Character** (any DTH Genesis 9 character
in your project) and, if you like, where the library asset goes. Then on **Build Library**, press **Build Library**.

The first build measures how DTH's face rig behaves on that character: it plays every face-board curve, alone and in
pairs, through the character's own DTH post-process blueprint off screen and records what comes out (a few
minutes). The measurement is kept in your project's `Saved/DTHExpressionWizard` folder and reused until DTH's
content changes. Nothing about DTH's rig ships with the plugin.

Each expression is fitted to DTH's face rig: the plugin finds the face-board values that make DTH's face as close to
the Daz expression as DTH's face can get. The report lists, for every expression, anything DTH's face cannot make and
why:

- *no pose in DTH's face ROM makes this shape*: Daz shapes DTH's ROM does not capture;
- *only DTH poses the face rig does not drive make this shape*: some of DTH's face poses are not reachable from the
  face board (the measurement lists them).

Each expression also gets a match percentage and a maximum strength (how far it can be overdriven before a curve
leaves its range). **Strip Gaze** (Settings, on by default) leaves eye direction to you: expressions do not turn the
eyes.

## 3. Mix and key

On the **Mixer** page:

- pick the **Character** to preview (any DTH Genesis 9 character);
- double-click expressions (or press +) to add them to the mix; set each one's strength, mute or remove it;
- the preview shows the mix live, through your character's own DTH setup;
- pick a **Sequence** on the character's skeleton, or type a name and press **Create** (made in the Sequence Folder);
- set the **Frame** and press **Key**. Every face-board curve is keyed, so a key fully sets the face at that frame;
  other keys are kept. Save the sequence when you are done;
- **Save Mix as Sequence** writes the current mix as its own short sequence.

Face sequences are additive (local space, against the reference pose) and hold no bone motion, so in Sequencer you
can put one on its own Animation track over a body animation; its weight curve scales the expression.

## Scripting

Every step is available to Python as `unreal.DTHExpressionScripting`: `open_wizard`, `measure_face_rig`,
`build_library`, `list_expressions`, `mix_values`, `key_mix`, `set_mix`, `capture_preview`. Results are lists of lines; errors start with `ERROR:` and
warnings with `WARNING:`.

## What stays on your machine

The plugin and the Daz script are only the pipeline. Your expression samples, the measurement of DTH's face rig, the
library and the sequences you key are made in your own project from your own content, and none of them belong in
this repository (`.gitignore` keeps measurements and samples out).
