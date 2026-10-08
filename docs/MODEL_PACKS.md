# Model packs

[Back to the front page](../README.md)

A model pack replaces some of the game's 3D models with new ones, for
example a smoother Ico or a more detailed sword. The game works fine
without one.

The first half of this page is for players who want to use a pack. The
second half is for people who want to make one.

## Put a model pack in

Model packs go in a `models` folder, next to the `textures` folder that
comes with the game. The game does not make this folder for you, so make
it yourself.

1. Beside the program, make the folders `models`, then `SCES-50760`
   inside it, then `replacements` inside that. On Windows they go inside
   the `x64` folder, so you end up with:

   ```
   x64\models\SCES-50760\replacements\
   ```

   On Linux and the Steam Deck it is `models/SCES-50760/replacements/`
   next to `ico_pc`.

2. Copy the pack's files into `replacements`. The pack's own folders
   inside it can stay as they are.
3. Start the game.
4. On the title screen, open **Options > Display** and check that **Model
   pack** says **On**.

That row turns the pack on and off at once, so you can compare. It says
**None installed** when the game found no pack. Models the pack does not
cover stay as they are. The row is only on the title screen's Options.

The pack can also go in a `models` folder in the same folder as your saves
([where that is](PORTABLE_MODE.md#where-your-saves-are)). If a pack was
copied one level too high, straight into `models`, the game finds it too.

The game's log file, `logs/ico-pc.log`, says what it found, in a line
such as:

```
models: 12 replacements from ...
```

or, when it found nothing, where it looked:

```
models: no model pack (looked in ...)
```

## Make a model pack

You need a 3D program that reads and writes glTF files. glTF is a common
file type for 3D models. Blender is free and does this well, and these
notes are written for it.

### Get the game's models out

1. In Options, turn on **Developer mode**.
2. Then do one of these:
   - To save everything as you play: turn on **Dump models**, the row that
     appears under Developer mode. Every model piece the game draws is
     saved once.
   - To save one model: open **Options > Extras > Models** on the title
     screen, pick the model, and press **Select**. A message says how
     many files it saved and where.

The files go in `models/SCES-50760/dumps` in the folder with your saves.

You cannot earn achievements while Developer mode is on, so turn it off
again when you are done.

### What the files are

Each model is made of parts, and the game saves each part as its own
glTF file: a `.gltf` file and a `.bin` file with the same name. The name is
a 16-character code, such as `3f9a07c2b1d4e865.gltf`. The code is how the
game recognises the part, so a replacement must keep it.

A list named `models.txt` in the same folder says which model and which
part each code belongs to, and how many points, pieces and bones it has.

A part can have several **pieces**. Each piece is drawn with one of the
game's textures. In Blender, each piece shows up as its own material.

### Make a replacement

1. Import the part's `.gltf` file into Blender.
2. Change it.
3. Export it as glTF, choosing either **glTF Binary (.glb)** or **glTF
   Separate (.gltf + .bin)**. Do not choose glTF Embedded: the game does
   not read that kind.
4. Give it the same name as the original, for example
   `3f9a07c2b1d4e865.glb`. You can put a word and a dash in front to keep
   track, for example `sword-3f9a07c2b1d4e865.glb`.
5. Put it in `models/SCES-50760/replacements` and start the game.

### What you can change

- The shape: move, add or remove points and faces as you like.
- Where the texture sits on each face.
- The colours painted on the points.
- How the shape bends with the skeleton (the bone weights), for parts
  that bend.

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
  skeleton. The game uses the original for these. The log says so, with a line that
  ends in "changes shape every frame; the original is used".
- **Bending or not bending.** A part that bends with a skeleton must keep
  its bone weights. A part that does not bend must not get any.

### Notes for Blender

- **Keep the bone names.** The bones are called `bone_00`, `bone_01` and
  so on. The game finds the bones by these names, so do not rename them.
- **Two bones per point.** The game uses at most two bone weights for each
  point. If a point has more, it keeps the two strongest.
- **Colours can shift a little.** The game stores colours with less
  detail than Blender, so a colour can come back very slightly different.
- **Keep the normals.** Blender exports them anyway. A part that the game
  lights needs them.

### When a replacement is not used

If the game cannot use a file, it draws the original and writes one line
in the log that starts with `models:` and says why. For example:

```
models: sword-3f9a07c2b1d4e865.glb not used for ...: it has 3 pieces and the original 2
```

Look there first when a replacement does not show up.
