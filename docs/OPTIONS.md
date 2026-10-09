# Options

[Back to the front page](../README.md)

## Open the Options menu

There are two ways in, and both open the same menu:

- On the title screen, choose **Options**.
- During play, press Start and choose **Options**.

Up and Down move between rows. Left and Right change a value. Cross opens
a page or confirms. Triangle or Circle goes back.

Every change happens at once, and the game remembers it the next time you
play. A few rows only show up in one of the two places. This page says
which.

Some rows are the PS2's own settings, the ones the original Options screen
had. They only show up when you open Options from the pause menu, and they
are kept in your save, as on the PS2:

- Brightness (on the Display page; Square puts back the normal value, 7)
- Button configuration, Vibration and Hold type (on the Controls page)
- Film effect and Players (on the Gameplay page, once you have finished
  the game)

## Display

How the picture looks.

- **Preset** is a shortcut. **Original** sets the four rows below back to
  the PS2 picture. **Enhanced** makes the picture as sharp as your
  window, fits it to the window's shape, smooths the textures and shows
  the full-height picture. Once you change one of the four rows yourself, it says
  **Custom**. The Preset does not touch the Effects page.
- **Resolution** is how sharp the picture is: 1x is the PS2's, up to 4x,
  or the size of your window. **Auto** starts at the size of your window
  and steps down (3x, then 2x, then 1x) when your computer or phone
  cannot draw the pictures in time. It never steps back up while the
  game runs; the row then reads, for example, **Auto (2x)**. On Android,
  the Enhanced preset uses Auto. It stays at 1x while the CRT filter is on:
  the filter draws the PS2's own picture dots with dark lines between them,
  and over a sharper picture it would look like a fine mesh.
  [Why](FAQ.md#why-does-the-crt-filter-switch-the-picture-back-to-the-original-resolution)
  The menus' lettering follows it too: at 2x and above it is drawn that
  much finer, so it stays crisp, in the same style.
- **Aspect ratio** is the picture's shape: 4:3 (the PS2's), 16:10, 16:9,
  21:9 or 32:9. **Auto** follows the shape of your window or screen.
  On a wider picture you see more of the world to the sides. Menus,
  subtitles and movies stay in a 4:3 box in the middle. At the far edges
  of a very wide picture, a few things can pop in a moment late, because
  the game only ever expected a 4:3 view.
- **Window mode** is **Windowed** (a normal window), **Borderless** (a
  window without a frame that fills the whole screen, so switching to
  another program is quick) or **Fullscreen**. In Steam Deck Game Mode the
  game always fills the screen, whatever this says. This row is not on
  phones.
- **Vertical sync** stops the picture from tearing. With it off, the
  picture can tear if your screen allows it.
- **Texture filtering** is how smooth the textures look up close.
- **Texture pack** turns an installed texture pack On or Off. It says
  **None installed** when there is none. [About texture packs](TEXTURE_PACKS.md).
- **Model pack** turns an installed model pack On or Off. It says **None
  installed** when there is none. This row is only on the title screen's
  Options. [About model packs](MODEL_PACKS.md).
- **Full-height picture** shows every line of the picture. The PS2 showed
  half of them, which looks a little softer.
- **Frame rate** is Original (as on the PS2), Uncapped, or a fixed limit
  up to 240 frames a second. On Android it starts at 60.
- **Video mode** is PAL 50 Hz or 60 Hz. The normal choice is 60 Hz. This
  row is only on the title screen's Options.

## Effects

This page is on the first Options page, under Display.

The CRT filter and the game's own picture effects.

- **CRT filter** makes the picture look like an old tube television, with
  several styles. **CRT strength** is how strong it is. It always draws at
  the PS2's original resolution, on purpose (the dark lines would look like
  a fine mesh on a sharper picture). Texture packs still show through it.

The game's own effects are each On or Off. All On is the picture the PS2
gives you, and that is how the game starts.

- **Glow:** the soft glow around bright light, and the flare of the sun.
- **Depth of field:** the blur on things far away.
- **Screen softening:** a slight blur over the whole picture that smooths
  jagged edges.
- **Motion blur:** the trail that moving things leave behind.
- **Fog:** the haze that hides the distance.
- **Cinematic bars:** the black bars at the top and bottom of the picture
  during cutscenes. With them Off you see the whole picture, and the
  subtitles are not dimmed.

The Preset on the Display page does not change these. Changes you make
here show in photo mode at once.

## Graphics driver

This page only exists on Android phones and tablets with an Adreno
graphics chip (most Qualcomm Snapdragon phones); on other phones it is not
shown. It lets you try a different graphics driver instead of the one that
came with the phone. [How to get one and add it](ANDROID.md#using-a-different-graphics-driver).

- **Driver** picks the driver for the next start. **Built-in** is the
  phone's own driver. Every driver you added is listed after it. If the
  driver you chose does not start, the game tells you, goes back to
  **Built-in**, and the note at the bottom of this page says so.
- **Add a driver** opens the file picker so you can choose a driver's zip
  file. It is hidden on phones without an Adreno chip.
- **Remove this driver** deletes the driver you have chosen. It only shows
  while one of your own drivers is chosen.

A change takes effect the next time you start the game.

## Audio

- **Volume, Music volume and Effects volume** set how loud everything is.
- **Sound output** is stereo or mono.
- **Output device** picks the speakers or headphones to use.

## Controls

- **Remap controls** lets you change which button or key does what.
- **Mouse camera** lets the mouse move the camera while you play, with
  the pointer hidden. It is on unless you turn it off. When you keep the
  mouse still, the camera swings back behind Ico.
  [More about the mouse camera](CONTROLS.md#look-around-with-the-mouse).
- **Mouse sensitivity** is how fast the mouse turns the camera, in play and
  in photo mode.
- **Invert mouse up/down** swaps up and down for the mouse.
- **Mouse camera speed** is how quickly the camera reaches where you point
  it. 1.0x is the normal speed; Instant moves it at once.
- **Mouse camera range** Normal keeps each area's own limit on how far the
  camera turns. Full lets you look all the way around.
- **Camera swings back** Off keeps the camera where you left it until you
  move the mouse again. The six mouse rows do not show on phones and
  tablets.
- **Circle goes back** makes Circle leave menus, like Triangle.
- **Touch controls, Touch size and Touch opacity** set up the buttons on a
  touch screen. They only show up on a device with one.
  [About touch controls](CONTROLS.md#touch-controls-phones-and-tablets).

## Gameplay

Both of these are off unless you turn them on.

- **Shadows never take Yorda** makes the game gentler. A few scenes in the
  story still show her being taken.
- **Analogue stick fix** lets Ico run in any direction you push the stick,
  not only the eight the original game knows.
  [Why this is needed](FAQ.md#ico-walks-instead-of-running).

## Language

English, French, German, Italian or Spanish. The game starts in your
system's language when it has it. If you change it during play, the menus
change at once and the game's own text and subtitles change at the next
area.

## Achievements

The list of achievements and what each one asks. Secret ones show as ???
until you earn them. You cannot earn achievements while Developer mode is
on.

## Extras

On the title screen's Options this page has Music, Models, Credits and
Character Customization. In the pause menu's Options it has only Character
Customization.

- **Music:** listen to the game's music and sound effects.
- **Models:** the model viewer. Look closely at the characters and some
  objects, and play their animations. [The viewer's controls](CONTROLS.md#look-at-the-models).
- **Credits:** watch the ending credits again. It unlocks once you have
  finished the game.
- **Character Customization:** change the colours of Ico and Yorda. [Details below](#character-customization).

### Character Customization

This page recolours Ico's skin, poncho, tunic and shorts, and Yorda's skin
and dress. Ico's poncho has four colour groups, so it has four rows: navy,
pink, light and dark. Every row starts at Original, the game's own colour.

On the title screen, the character is shown beside the rows, and only that
character's rows are on the page: Ico's seven, or Yorda's two. Ico comes
first; the screen takes a few seconds to load each character. Every colour
you pick shows on the model at once. In the pause menu, the page has the
rows of both characters.

- **Left and Right:** pick a colour for the row. Clothes have 24 named
  colours, from Red to Black. Skin has twelve tones, from Tone 1 (the
  lightest) to Tone 12 (the darkest), and then the same 24 named colours.
- **Square:** puts the row back to Original.
- **Switch to Yorda / Switch to Ico** (title screen only), or **L1 / R1**:
  shows the other character and its rows.
- **Right stick, L2 / R2** (title screen only): turn the model, zoom out
  and in.
- **Randomize:** gives every row a random colour. The four poncho groups
  always get four different colours. On the title screen it changes only
  the character shown; in the pause menu it changes both.
- **Reset to original:** puts every row back to Original. On the title
  screen it changes only the character shown; in the pause menu both.
- **Triangle or Back:** on the title screen, takes you back to the title
  screen and opens Options on Extras again. In the pause menu, back to
  Extras.

In the pause menu, the game behind the menu changes colour as you choose.

Each row has a small square on its right showing the colour you picked. In
the game, Ico looks a little darker than the square and Yorda a little
paler, because the game lights them.

A texture pack that has its own pictures of Ico and Yorda replaces these
colours. While a texture pack is on, the page reminds you of this.

Your choices are saved when you leave the page.

## Photo mode

Photo mode is a row in the pause menu during play. The game stays paused
while you move a camera around and save pictures, and the picture is
drawn from the camera with every effect the game has.
[The photo mode controls](CONTROLS.md#take-a-photo).

## Developer mode

Developer mode brings back the menu the game's makers used while they
built it. Leave it off for normal play. While it is on, you cannot earn
achievements.

With Developer mode on, two more rows show up under it. They are for
people who make packs:

- **Dump textures** saves every texture as a picture file when the game
  loads it. [More](TEXTURE_PACKS.md#make-a-texture-pack).
- **Dump models** saves every model piece as a 3D model file when the game
  draws it. [More](MODEL_PACKS.md#make-a-model-pack).

## The pause menu's journey panel

The pause menu shows your journey's numbers on the right: play time,
deaths, how often Yorda was taken, saves, enemies defeated, whether New
Game+ and Mirror mode are on, and how many achievements you have. Any
helpers you turned on (Shadows never take Yorda, the stick fix, Developer
mode) are listed too. Where the game has a name for the area you are in,
the panel shows it.

A save made before version 0.4.0 never counted its saves and enemies, so
those two lines stay hidden until you start a New Game.

## Starting a New Game

**Mirror mode** and **New Game+** are not in Options. When you start a New
Game, the screen after "Vibration" has a row for each, Off or On. Up and
Down move between them, Left and Right pick, and Cross starts the game.

- **Mirror mode** plays the whole game flipped left to right.
- **New Game+** plays the second journey, the one the PS2 gives you after
  you finish the game. Yorda's words are translated, and the ending and
  some items change. It starts On when you begin from a finished game's
  save, and Off otherwise. You can change it either way.

Two things follow from that choice. If you turn New Game+ On before you
have finished the game, the ending does not offer to save a finished game,
and finishing counts for the "Once More" achievement. If you turn it Off
after finishing, Film effect and Players are not in Options for that
journey. Both choices stay with that save.

## Settings you can only change in a file

A few settings are not in the menu. The game keeps all its settings in a
text file named `config.toml`, in the same folder as your saves
([where that is](PORTABLE_MODE.md#where-your-saves-are)). To change one:

1. Close the game.
2. Open `config.toml` in a text editor, such as Notepad.
3. Find the line, change the value after the `=`, and save the file.

Each line has a short note above it saying what it does. Lines that start
with `#` are notes, or settings switched off. To switch one on, delete
the `#` at the start of its line.

A `config.toml` from an older version may not have a line yet. You can add
it yourself, under the heading shown below in square brackets.

Under `[photo]`, for photo mode:

| Line | What it does |
| --- | --- |
| `stick_speed = 1.0` | How fast the camera moves, turns, tilts and zooms. `0.5` is half as fast, `2.0` twice as fast. |
| `invert_y = false` | `true` swaps up and down when you look around. |
| `hide_ui = false` | `true` opens photo mode with its help panel hidden. Square in photo mode changes this for you. |
| `png_dir = "screenshots"` | The folder your pictures go in, inside the folder with your saves. |

Under `[video]`:

| Line | What it does |
| --- | --- |
| `window_mode = "windowed"` | The Window mode row: `"windowed"`, `"borderless"` or `"fullscreen"`. A `config.toml` from an older version has a `fullscreen` line instead, and it is still read. |
| `effect_glow = true` and the five other `effect_` lines | The six switches of the Effects page. |
| `gpu_driver = ""` | Android only. The Driver row of the Graphics driver page: empty is the phone's own driver. The game also writes a `gpu_driver_failed` line when a driver did not start. [About Android](ANDROID.md#using-a-different-graphics-driver). |
| `model_pack = true` | The Model pack row. `false` shows the game's own models. |
| `dump_models = false` | The Dump models row. Only works with Developer mode on. |
| `effects_depth = true` | Lets an effects program such as ReShade see how far away things are. [About ReShade](RESHADE.md). |
| `texture_pack_budget_mb`, `texture_pack_precache`, `texture_pack_cache_mb` | How much memory a texture pack may use. [About texture packs](TEXTURE_PACKS.md#if-the-pack-uses-a-lot-of-memory). |

Under `[input]`, for the mouse camera:

| Line | What it does |
| --- | --- |
| `mouse_camera = true`, `mouse_sensitivity = 1.0`, `mouse_invert_y = false` | The three mouse rows of the Controls page. |
| `mouse_camera_speed = 1.0` | The Mouse camera speed row, from 0.5 to 10. 10 is Instant. |
| `mouse_full_range = false` | The Mouse camera range row. `true` is Full. |
| `mouse_return = true` | The Camera swings back row. |
| `mouse_hold = 0.75` | How many seconds the mouse must stay still before the camera swings back behind Ico. |

Under `[characters]`, for the Character Customization page. A line that is missing means
Original:

| Line | What it does |
| --- | --- |
| `ico_skin`, `yorda_skin` | `"original"`, a skin tone, `"tone1"` (lightest) to `"tone12"` (darkest), or one of the colour names in the next line. |
| `ico_poncho_navy`, `ico_poncho_pink`, `ico_poncho_light`, `ico_poncho_dark`, `ico_tunic`, `ico_shorts`, `yorda_dress` | `"original"` or a colour name: `"red"`, `"crimson"`, `"rose"`, `"pink"`, `"magenta"`, `"plum"`, `"violet"`, `"indigo"`, `"navy"`, `"blue"`, `"sky"`, `"teal"`, `"cyan"`, `"green"`, `"moss"`, `"olive"`, `"gold"`, `"orange"`, `"rust"`, `"brown"`, `"sand"`, `"white"`, `"grey"` or `"black"`. |

A value the game does not know counts as Original.

On Android, under `[paths]`:

| Line | What it does |
| --- | --- |
| `keep_image = false` | `true` keeps the copy of your disc image that the first start makes. Under `[paths]`. On Android, a line `keep_image=1` in `ico-pc.ini` works as well. [About Android](ANDROID.md#the-first-start). |
