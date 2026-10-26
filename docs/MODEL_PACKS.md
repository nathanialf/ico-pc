# Model packs

[Back to the front page](../README.md)

A model pack replaces some of the game's 3D models with new ones, for
example a smoother Ico or a more detailed sword. The game works fine
without one.

The first half of this page is for players who want to use a pack. The
second half is for people who want to make one.

## Put a model pack in

Model packs go in the `models` folder that comes with the game, next to
the `textures` folder. It is already there, with a short note inside it.

Each disc has its own folder for packs, named after the number printed on
the disc: `SCES-50760` for the European disc, `SCPS-11003` for the
Japanese disc, `SCPS-56001` for the Korean disc, `SCUS-97113` for the North
American disc and the August 2001 preview disc (which has no number of its
own and shares this folder), `SCUS-97158` for the June 2001 magazine demo and
`SCPS-55001` for the Asian disc. A pack goes in the
folder of the disc it was made for, and of these six folders the game
reads only the one of the disc you play.
The other places below (`models/replacements`, and files loose in
`models`) are read for every disc, so a pack there shows on any disc.
The steps below use
the European disc's folder; for another disc, read its number from the
list above instead of `SCES-50760`.

1. Open the `models` folder beside the program. On Windows it is inside
   the `x64` folder:

   ```
   x64\models\SCES-50760\replacements\
   x64\models\SCPS-11003\replacements\
   x64\models\SCPS-56001\replacements\
   x64\models\SCUS-97113\replacements\
   x64\models\SCUS-97158\replacements\
   x64\models\SCPS-55001\replacements\
   ```

   On Linux and the Steam Deck it is `models/SCES-50760/replacements/`
   (and the same with the other discs' numbers)
   next to `ico_pc`. On a Mac or an iPhone there is no such folder in the
   download: make it in the folder with your saves, as below. On Android,
   [the Android guide](ANDROID.md#where-your-files-are) says where it goes.

2. Copy the pack's files into `replacements`. The pack's own folders
   inside it can stay as they are.
3. Start the game.
4. On the title screen, open **Options > Picture** and check that **Model
   pack** says **On**.

That row turns the pack on and off at once, so you can compare. It says
**None installed** when the game found no pack. Models the pack does not
cover stay as they are. The row is only on the title screen's Options.

The pack can also go in a `models` folder in the same folder as your saves
([where that is](PORTABLE_MODE.md#where-your-saves-are)). On a Mac that is
this folder (in Finder, choose **Go > Go to Folder** and type the path):

```
~/Library/Application Support/ico-pc/ico-pc/models/SCES-50760/replacements/
```

and on an iPhone it is `models/SCES-50760/replacements` in the game's
Documents folder, the same folder the disc image goes in, which you reach
through LiveContainer.

If the pack's files were copied loose straight into `models`, or its
`replacements` folder was copied into `models`, the game finds them too,
for every disc. Other folders of the pack put straight into `models` are not searched:
move them into `models/SCES-50760/replacements`.

The game's log file, `ico-pc.log` ([where to find it](TROUBLESHOOTING.md#the-log-file)),
says what it found, in a line such as:

```
models: 12 replacements from ...
```

or, when it found nothing, where it looked:

```
models: no model pack (looked in ...)
```

## Make a model pack

You need a 3D program that reads and writes glTF files. glTF is a common
file type for 3D models. Blender is free and reads and writes glTF, and
these notes are written for it.

### Get the game's models out

1. Open **Options > Developer** and turn on **Developer mode**.
2. Then do one of these:
   - To save everything as you play: turn on **Dump models**, the row that
     appears under Developer mode on the same page. Every model part the game draws is
     saved once. Parts that change shape every frame are not saved.
   - To save one model: open **Options > Extras > Models** on the title
     screen, pick the model, and press **Select** (Backspace on a
     keyboard). A message says how
     many files it saved and where.

The files go in `models/SCES-50760/dumps` (European disc) or
`models/SCPS-11003/dumps` (Japanese disc),
`models/SCPS-56001/dumps` (Korean disc), and under the other discs' numbers
the same way, in the folder with your saves.
Make a pack from the dumps of the disc it is for.

You cannot earn achievements while Developer mode is on, so turn it off
again when you are done. Achievements stay off until you go back to the
title screen.

### What the files are

Each model is made of parts, and the game saves each part, or each section
of a part, as its own glTF file: a `.gltf` file and a `.bin` file with the same name. The name is
a 16-character code, such as `3f9a07c2b1d4e865.gltf`. The code is how the
game recognises the part, so a replacement must keep it.

A list named `models.txt` in the same folder says which model and part
(and which section of it) each code belongs to, and how many points (the
corners the shape is made of), pieces and bones it has. The number of
pieces is in the column named `batches`.

A part's file can have several **pieces**: the game splits a part into
chunks. All pieces of one file use the same texture.

### Make a replacement

1. Import the part's `.gltf` file into Blender (**File > Import > glTF
   2.0**).
2. Change it.
3. Export it with **File > Export > glTF 2.0**, and in the **Format** list
   choose either **glTF Binary (.glb)** or **glTF Separate (.gltf +
   .bin)**. Do not choose glTF Embedded: the game does not read that kind.
4. Give it the same name as the original, for example
   `3f9a07c2b1d4e865.glb`. You can put a word and a dash in front to keep
   track, for example `sword-3f9a07c2b1d4e865.glb`.
5. Put it in `models/SCES-50760/replacements` (or
   `models/SCPS-11003/replacements` for the Japanese disc,
   `models/SCPS-56001/replacements` for the Korean disc, and so on with the
   other discs' numbers) and start the
   game.

### What you can change

- The shape: move, add or remove points and faces (the flat surfaces
  between points) as you like.
- Where the texture sits on each face.
- The colours painted on the points.
- How the shape bends with the skeleton (the bones that move a part, such
  as an arm): the bone weights, which say how strongly each bone pulls
  each point, for parts that bend.

### What you cannot change

- **The textures.** The game keeps its own. To change a texture, make a
  [texture pack](TEXTURE_PACKS.md) instead.
- **The number of pieces.** A replacement can have the same number of
  pieces as the original, or fewer. It cannot have more.
- **Shadows, collisions and when a part is hidden.** These still follow
  the original model. The game also stops drawing a part when the original
  would be off screen, so keep your replacement about the same size and in
  the same place as the original. Something that sticks out far past it
  can vanish at the edge of the screen.
- **Parts that change shape every frame** on their own, not through a
  skeleton. The dump does not save these, and the game uses the original
  for them. The log says so, with a line that
  ends in "changes shape every frame; the original is used".
- **The rest pose of the skeleton** (where the bones sit before any
  animation moves them). The game uses its own starting
  positions for the bones. If the file has a different rest pose, the
  game uses its own anyway, and the log has a line that ends in "which
  differ from the game's; the game's are used".
- **Bending or not bending.** A part that bends with a skeleton must keep
  its bone weights. A part that does not bend must not get any.

### Notes for Blender

- **Keep the bone names.** The bones are called `bone_00`, `bone_01` and
  so on. The game finds the bones by these names, so do not rename them.
- **Two bones per point.** The game uses at most two bone weights for each
  point. If a point has more, it keeps the two strongest.
- **Colours can shift a little.** The game stores colours with less
  detail than Blender, so a colour can come back very slightly different.
- **Keep the normals** (which way each point faces, for lighting). Leave
  **Normals** ticked in Blender's export options (it is on by default). A
  part that the game lights needs them.

### When a replacement is not used

If the game cannot use a file, it draws the original and writes one line
in the log that starts with `models:` and says why. A file the game cannot
read at all gets a line with `skipped` in it. A file that does not match
the original gets a line like this one:

```
models: .../sword-3f9a07c2b1d4e865.glb not used for ...: it has 3 pieces and the original 2
```

Look there first when a replacement does not show up.
