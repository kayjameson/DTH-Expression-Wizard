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
- The DTH Pose Wizard plugin (0.2.0 or later): the Expression Wizard keys control rigs and reads the face board
  through its control-rig keying module.

## Install

Copy the `DTHExpressionWizard` folder (and the `DTHPoseWizard` folder, which it needs) into your project's `Plugins`
folder and restart the editor. Open it from
**Tools > DazToHue > DTH Expression Wizard**.

## 1. Sample your expressions in Daz Studio

The library is made from what Daz Studio itself does with each preset, so Daz runs once per library build.

1. Save or close your Daz scene: the script replaces it with a fresh Genesis 9 and refuses to run over unsaved work.
2. In Daz Studio, open `Resources/Daz/DTH_SampleExpressions.dsa` from the plugin folder (File > Open, or
   double-click it). The wizard's Settings page shows the path and has **Copy Path** and **Show in Explorer**.
3. It finds every expression preset in your content library the way Daz content is laid out (pose presets in any
   folder named `Expressions` under `People/`, in every content directory), records DTH's face ROM and each preset,
   and writes `Documents/DTH Expression Wizard/DTH_ExpressionSamples.json`. For every preset it also records the face's
   shape: how far each vertex of the head and of the Mouth figure (teeth, tongue) moves from rest (`face.json` and
   `geo_*.txt`, next to the JSON). Folders made for another figure generation are listed and skipped. Nothing is saved
   in Daz. Expect about 3 seconds per preset. To include presets kept elsewhere, run it with
   `var args = {folders: ["Your/Folder"]};` defined first.

## 2. Build the library

On the **Settings** page, set **Daz Samples File** to that JSON, pick a **Character** (any DTH Genesis 9 character
in your project) and, if you like, where the library asset goes. Then on **Build Library**, press **Build Library**.

The first build measures how DTH's face rig behaves on that character: it plays every face-board curve, alone and in
pairs, through the character's own DTH post-process blueprint off screen and records what comes out (a few
minutes). The measurement is kept in your project's `Saved/DTHExpressionWizard` folder and reused until DTH's
content changes. Nothing about DTH's rig ships with the plugin.

Each expression is fitted by its **shape**: the plugin finds the face-board values that make the character's face, as
DTH's face rig and its dual-quaternion deformer show it, as close as possible to the Daz expression's face, vertex by
vertex (the whole face, teeth and tongue included, the eyeballs left out). Any combination of board curves can be
used, within what a face-board key can hold: only face-board curves, each slider at one end at a time. Every step of
the fit is measured on the character's real rig, and a fit takes at most 2 seconds.

Build on a plain Genesis 9 exported through DazToHue (for example `G9_Base`): it is the figure the expressions were
sampled on, so the face is compared point for point. The values then play on every DTH Genesis 9 character, as a Daz
expression does on any Genesis 9 character.

Each expression gets a **match percentage**: how much of the Daz face's movement the character's face reproduces
(100% = identical). On the 42 test expressions, 26 reach 94% or more. What keeps the others lower is DTH's own face
poses: its brow poses reproduce Daz's brows at 87 to 90%, its tongue-tip poses move the tip about two thirds as far,
and some packs push brows past their full value or below zero. The report also lists anything DTH's face cannot make
and why:

- *no pose in DTH's face ROM makes this shape*: Daz shapes DTH's ROM does not capture;
- *only DTH poses the face rig does not drive make this shape*: some of DTH's face poses are not reachable from the
  face board (the measurement lists them);
- *the preset's own sculpted shape*: a pack that ships its own sculpted morph instead of Genesis 9's FACS shapes
  (for example HS Icey); DTH's face poses can only approximate it.

Presets that leave the face neutral (resets) are skipped. Each expression also gets a maximum strength (how far it
can be overdriven before a curve leaves its range). **Strip Gaze** (Settings, on by default) leaves eye direction to
you: expressions do not turn the eyes.

Samples from an older version of the Daz script (without the face shape) still build, with the older fit by FACS
values; sample again to get the shape fit.

## 3. Mix and key

On the **Mixer** page:

- pick the **Character** to preview (any DTH Genesis 9 character);
- double-click expressions (or press +) to add them to the mix; set each one's strength, mute or remove it;
- the preview shows the mix live, through your character's own DTH setup;
- pick a **Sequence** on the character's skeleton, or type a name and press **Create** (made in the Sequence Folder,
  with the current mix keyed at the frame);
- set the **Frame** and press **Key**. Every face-board curve is keyed, so a key fully sets the face at that frame;
  other keys are kept, and the sequence is saved;
- **Save Mix as Sequence** writes the current mix as its own short sequence.

Face sequences are additive (local space, against the reference pose) and hold no bone motion, so in Sequencer you
can put one on its own Animation track over a body animation; its weight curve scales the expression.

## Key onto a Control Rig

Both wizards key the same control rig track on the same frames without touching each other's channels: the Pose
Wizard keys the body controls (`Body/` on DTH's modular rig), the Expression Wizard the face board (`Face/`). So one
pose can hold for four frames with a different expression on each, or the other way round.

Pick the **Level Sequence**, the **character** (its binding) and the **Frame** in the "Key onto a Control Rig" bar. The
choice is shared by both wizards, and the frame moves on by itself after each key. **Stepped** (on by default) holds
each key until the next one; turn it off to blend between keys. If the character has no control rig track yet, its
default animating rig (`MCR_<character>`) is added.

What you see in the wizard is what the track plays, exactly:
- The values are found with the rig's Forwards Solve, never its Backwards Solve (DTH's backwards solve, which
  Sequencer's own **Bake to Control Rig** uses, bakes with the IK legs and spine on and drops the head's own turn and
  the pelvis; see the bake-fixed copy of the rig for a bake that works).
- What the rig cannot show goes on a **layered FK Control Rig** track that the wizards add to the same binding, which
  Sequencer applies after the rig: bones no body control of DTH's rig drives (spine_05, the metatarsals, the pec
  clavicles, the twist bones, the eyes) and face curves the board cannot write (six lip curves have no control, and
  some expressions need both ends of one slider at once, such as nose wrinkle with nostril depress). Poses own the
  layer's bones, expressions its curves. The twist bones matter: DTH's post-process blends each one's own twist with
  its limb's, so the pose's twist shows on the character. An expression from the shape fit is held by the face board
  as it is (it uses board curves only, one slider end at a time), so it needs no layer curves and stays
  MetaHuman-board compatible; a mix of several expressions can still pull one slider both ways and then uses the layer.
- Measured on Milly (2026-10-28): every body bone within 0.02 deg of the Pose Wizard's preview, every one of the 154
  expression curves within 0.005 of the Expression Wizard's mix, on every frame, with poses and expressions keyed
  together and alone; and on the character as shown (after DTH's post-process) the twist bones of a keyed pose the
  same as the pose played as an animation (they were up to 69 deg off before the layer carried them).
- The wizards key the bake-fixed copy of the rig the same way; it has controls of its own for spine_05, the
  metatarsals, the pec clavicles and the twist bones.

**Key Expression** keys the current mix at the frame.

## Python scripting (optional)

Everything the wizard's buttons do can also be run from Python in the Unreal editor, for example to rebuild the
library or key many expressions into a sequence in one go. Run the code in the editor's Python console
(**Window > Output Log**, set the input line to **Python**) or from your own editor scripts. The functions are on
`unreal.DTHExpressionScripting`; empty paths use the ones on the wizard's Settings page.

| Function | What it does |
|---|---|
| `build_library(samples_file, library_asset, strip_gaze, character)` | Builds the expression library, as **Build Library** does. |
| `list_expressions(library_asset, filter)` | Lists the expressions, one line each, starting with the index the other functions use. |
| `key_mix(sequence, skeletal_mesh, frame, expressions, strengths)` | Keys a mix of expressions (by index, with a strength each) at a frame, creating the sequence if needed. |
| `mix_values(expressions, strengths)` | Shows the face-board values a mix comes to, without keying anything. |
| `key_mix_onto_control_rig(sequence, binding_name, skeletal_mesh, frame, expressions, strengths, stepped)` | Keys a mix onto the character's control rig track at a frame, as **Key Expression** does. |
| `measure_face_rig(character, force)` | Measures DTH's face rig on a character (the build does this when it needs to). |
| `open_wizard(page)`, `set_mix(skeletal_mesh, expressions, strengths)`, `capture_preview(file, view)` | Open the wizard on a page, set the Mixer's mix, save a picture of the preview. |

They return text: a list of lines (a single line for `open_wizard`, `set_mix` and `capture_preview`). Errors start with
`ERROR:`, warnings with `WARNING:`.

```python
import unreal
lib = unreal.DTHExpressionScripting
for line in lib.list_expressions("", "Smile"):
    print(line)                      # e.g. "12 | People/Genesis 9/Expressions/... | Smile 01 | 96% | 1.00 | 22 | 0"
# Smile 01 at full strength on frame 0, back to neutral on frame 30:
lib.key_mix("/Game/Faces/A_Smile", "/Game/Characters/MyCharacter/SKM_MyCharacter", 0, [12], [1.0])
lib.key_mix("/Game/Faces/A_Smile", "", 30, [], [])
```
