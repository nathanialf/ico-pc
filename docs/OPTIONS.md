# Options

[Back to the front page](../README.md)

## Open the Options menu

There are two ways in, and both open the same menu:

- On the title screen, choose **Options**.
- During play, press Start and choose **Options**.

Up and Down move between rows. Left and Right change a value. Cross opens
a page or confirms. Triangle goes back, and so does Circle (see **Circle
goes back** under [Controls](#controls)).

In a released download, the version of the game is shown in small grey
letters in the top-right corner of the Options menu's first page. Say
which version you have when you report a problem.

Every change happens at once, and the game remembers it the next time you
play. A few rows only show up in one of the two places. This page says
which.

Some rows are the PS2's own settings, the ones the original Options screen
had. They only show up when you open Options from the pause menu, and they
are kept in your save, as on the PS2:

- Brightness (on the Effects page; Square puts back the normal value, 7)
- Button configuration, Vibration and Hold type (on the Controls page)
- Film effect and Players (on the Gameplay page, once you have finished
  the game)

## Display

How the picture looks.

- **Preset** is a shortcut for four rows: Resolution, Aspect ratio,
  Texture filtering and Full-height picture. **Original** sets them back to
  the PS2 picture. **Enhanced** makes the picture as sharp as your
  window, fits it to the window's shape, smooths the textures and shows
  the full-height picture. Once you change one of those four rows yourself,
  it says **Custom**. Two of those four rows, Full-height picture and
  Texture filtering, are on the Effects page. The shortcut also sets Full
  pixel (Effects page): Enhanced turns it on, Original turns it off.
- **Resolution** is how sharp the picture is: 1x is the PS2's, up to 16x,
  or the size of your window. Very high values need a lot of graphics
  memory; when your graphics card cannot hold the picture, the game lowers
  it on its own and the row shows both, for example **16x (8x)**. **Auto**
  starts at the size of your window and steps down (3x, then 2x, then 1x)
  when your computer or phone cannot draw the pictures in time. It never
  steps back up while the game runs; the row then reads, for example,
  **Auto (2x)**. On phones and tablets, the Enhanced preset uses Auto. It
  stays at 1x while the CRT filter is on: the filter draws the PS2's own
  picture dots with dark lines between them, and over a sharper picture it
  would look like a fine mesh.
  [Why](FAQ.md#why-does-the-crt-filter-switch-the-picture-back-to-the-original-resolution).
  The menus' lettering follows it too: from 2x up to 4x it is drawn that
  much finer, so it stays crisp, in the same style. Above 1x, the game's
  glow, blur, shadow and fog effects are drawn to suit the finer picture,
  so they look soft instead of showing a grid; at 1x they are the PS2's
  exactly.
- **Aspect ratio** is the picture's shape: 4:3 (the PS2's), 16:10, 16:9,
  21:9, 32:9 or 48:9. **Auto** follows the shape of your window or screen,
  up to 60:9.
  The game draws the extra width itself (nothing is stretched), so on a wider
  picture you see more of the world to the sides, and it looks the way the
  original did. Menus, subtitles and movies stay in a 4:3 box in the middle.
- **Anti-aliasing** smooths jagged edges. The choices go in this order:
  **Off**, **MSAA 2x**, **MSAA 4x**, **MSAA 8x**, **2x supersampling** and
  **4x supersampling**. Off draws the picture as it is. MSAA smooths the
  edges of shapes and is lighter to run; a choice your graphics card cannot
  do is skipped when you step through the row. Supersampling draws the
  picture at a larger size and shrinks it back, which smooths everything
  (including the insides of textures) but needs more from your graphics
  card.
  While the Preset is **Original**, this row stays at **Off** and cannot be
  changed. Pick **Enhanced**, or change one of the Preset's rows, to use
  it. The row shows your saved choice. When the game is not using it,
  because your graphics card cannot do it, or cannot do it with the fog
  effect, it says **(off: not available)** after the name and the picture
  is drawn without it.
- **Window mode** is **Windowed** (a normal window), **Borderless** (a
  window without a frame that fills the whole screen, so switching to
  another program is quick) or **Fullscreen**. In Game Mode, the Steam Deck
  shows every game on the whole screen, whatever this row says. This row is
  not on phones and tablets.
- **Vertical sync** stops the picture from tearing. With it off, the
  picture can tear if your screen allows it. With it on, **Video mode** at
  60 Hz (the normal choice) and **Frame rate** at 60 on a 60 Hz screen or
  120 on a 120 Hz screen, the game takes its steps in time with the screen,
  so every picture is shown for the same length of time. With Video mode at
  50 Hz, none of the Frame rate row's limits do this. On Windows the game
  measures how fast the screen really refreshes and follows that, rather
  than the number Windows reports. With
  Direct3D 12 (see [Choosing the renderer on Windows](#choosing-the-renderer-on-windows))
  and Vertical sync on, the game waits until the screen is ready for a new
  picture before it draws one, so the picture you see is as fresh as it
  can be.
- **Texture pack** turns an installed texture pack On or Off. It says
  **None installed** when there is none. [About texture packs](TEXTURE_PACKS.md).
- **Model pack** turns an installed model pack On or Off. It says **None
  installed** when there is none. This row is only on the title screen's
  Options. [About model packs](MODEL_PACKS.md).
- **Frame rate** is Original (as on the PS2), Uncapped, or a fixed limit of
  60, 120, 144 or 240 frames a second. On a computer it starts at Uncapped.
  With Vertical sync on, Uncapped follows your screen: one new picture each
  time the screen shows one. If your screen says it refreshes fewer than
  sixty times a second, Vulkan usually still draws sixty a second (it
  depends on your graphics driver); Direct3D 12 always draws one picture for
  each refresh. With Vertical sync off, Uncapped draws as fast as your
  computer can. A limit equal to your screen's refresh rate (60 on a 60 Hz
  screen, 120 on a 120 Hz screen) follows the screen, so motion stays
  smooth. On a 144 Hz or 240 Hz screen the 144 or 240 limit counts as equal
  to it, even when the system reports 143 or 239. On phones and tablets it
  starts at 60. If motion ever looks uneven, check that this limit matches
  your screen's refresh rate (see your system's display settings). If a
  faint trailing image stays while you move, try another limit, or on
  Windows the Direct3D 12 renderer ([Choosing the renderer on
  Windows](#choosing-the-renderer-on-windows)).
- **Video mode** is PAL 50 Hz or 60 Hz. The normal choice is 60 Hz. This
  row is only on the title screen's Options.

## Effects

This page is in the Options menu, right under Display.

The picture's finish, the CRT filter and the game's own picture effects.

- **Full-height picture** shows every line of the picture. The PS2 showed
  half of them, which looks a little softer.
- **Texture filtering** is how smooth the textures look up close: Original
  (as on the PS2), Trilinear (smooth) or Anisotropic (smooth, and sharper
  on surfaces seen at an angle).
- **Brightness** sets how bright the picture is. Square puts back the
  normal value, 7.
- **CRT filter** makes the picture look like an old tube television. Off
  turns it off; the other choices are the styles Scanlines, Consumer TV,
  Trinitron, PVM and Shadow mask. **CRT strength** is how strong it is. It
  always draws at the PS2's original resolution, on purpose (the dark
  lines would look like a fine mesh on a sharper picture). Texture packs
  still show through it. To adjust a style's details, see
  [the lines you can change in a file](#settings-you-can-only-change-in-a-file).

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

The last row is not one of the game's effects:

- **Full pixel (exact screen pixels)** makes the picture out of your
  screen's own pixels: each pixel of the game's picture is one pixel on your
  screen, so nothing is rounded, cropped or stretched, and the strip the PS2
  hid under its thin black border is shown too. The picture is exactly as
  wide as the window shows it, at every size and shape of window. It needs
  Resolution set to the size of your window (the Enhanced Preset picks it).
  With Original, a fixed Resolution (1x to 16x), the CRT filter or
  supersampling, this row only opens up the thin black border. Picking the
  Enhanced Preset turns it on, and you can turn it on or off here at any
  time. Off shows the same picture, the same size and just as sharp, with
  the PS2's thin black border laid over its edges as the PS2 did: a few
  dots at the sides and a little more at the top and bottom. Nothing is
  shrunk or moved; the border only covers the edge. With Video mode at
  50 Hz the border at the top and bottom is a little taller. With Aspect
  ratio at 4:3, or at a shape the game's picture fills exactly (such as
  48:9), Off is the PS2's own picture and border, exactly as before.

The Preset on the Display page sets Texture filtering, Full-height picture and Full pixel on this
page too (Enhanced turns Full pixel on, Original turns it off), but it does not change the rest.
Changes you make here show in photo mode at once.

## Graphics driver

This page is in the Options menu, under Effects. It only exists on Android
phones and tablets with an Adreno graphics chip (most Qualcomm Snapdragon
phones); on other phones it is not shown. It lets you try a different
graphics driver instead of the one that came with the phone.
[How to get one and add it](ANDROID.md#using-a-different-graphics-driver).

- **Driver** picks the driver for the next start. **Built-in** is the
  phone's own driver, so choose it to go back to that one. Every driver you
  added is listed after it. If the driver you chose does not start, the
  game tells you, goes back to **Built-in**, and the note at the bottom of
  this page says so.
- **Add a driver** opens the file picker so you can choose a driver's zip
  file.
- **Remove this driver** deletes the driver you have chosen. It only shows
  while one of your own drivers is chosen.

A change takes effect the next time you start the game.

## Audio

- **Volume, Music volume and Effects volume** set how loud everything is.
- **Sound output** is Auto, Stereo or Mono. Auto is the normal choice: it
  follows the sound setting of your memory card, as the PS2 did, and the row
  shows which one is in use, for example **Auto (Stereo)**.
- **Output device** picks the speakers or headphones to use. The default is
  your system's choice.

## Controls

- **Remap controls** lets you change which button or key does what. It
  has a Keyboard column (which includes the mouse buttons) and a Gamepad
  column. Cross changes the one you are on, Square clears it, and the
  **Reset to defaults** row puts everything back. Giving a button to
  another action swaps the two. In the menus the gamepad's face buttons and
  d-pad always go by position.
- **Button configuration** opens the game's own screen for choosing which
  button does what. It applies on top of Remap controls. Pause menu only.
- **Vibration** is On or Off, as the game's own setting. Pause menu only.
- **Hold type** is **A (hold)**, where Yorda's hand stays held while you
  keep R1 down, or **B (toggle)**, where you press R1 once to let go of her
  hand. Pause menu only.
- **Analogue stick fix** is the same switch as the one on the Gameplay
  page, shown here too so you find it with the other controls. Turn it on
  in either place and it is on in both.
- **Mouse camera** lets the mouse move the camera while you play, with
  the pointer hidden. It is on unless you turn it off.
  [More about the mouse camera](CONTROLS.md#look-around-with-the-mouse).
- **Mouse sensitivity** is how fast the mouse turns the camera, in play and
  in photo mode.
- **Invert mouse up/down** swaps up and down for the mouse.
- **Mouse camera speed** is how quickly the camera reaches where you point
  it. 1.0x is the speed the camera has with a gamepad; Instant moves it
  almost at once.
- **Mouse camera range:** Normal keeps each area's own limit on how far the
  camera turns. Full lets you look all the way around.
- **Camera swings back:** On means that when you keep the mouse still, the
  camera swings back behind Ico. Off keeps the camera where you left it
  until you move the mouse again.

The six mouse rows do not show on phones and tablets.

- **Circle goes back** lets Circle leave the game's own menus, like
  Triangle. It starts On. In the Options menu, Circle always goes back.
- **Touch controls, Touch size and Touch opacity** set up the buttons on a
  touch screen. They only show up on a device with one.
  [About touch controls](CONTROLS.md#touch-controls-phones-and-tablets).
  Their size is a real size, so they feel about the same on every screen
  (smaller only when the screen cannot hold them).

## Gameplay

The first four are off unless you turn them on, on a phone too.

- **Shadows never take Yorda** makes the game gentler. A few scenes in the
  story still show her being taken.
- **Analogue stick fix** lets Ico run in any direction you push the stick,
  not only the eight the original game knows. It is also on the Controls
  page, and it is the same switch.
  [Why this is needed](FAQ.md#ico-walks-instead-of-running).
- **East Reflector door fix** lets Ico lead Yorda by the hand through a doorway that
  the original game sends you both back out of, such as the one at the top
  of the ladder in the East Reflector. For a few seconds after you come
  through a door holding her hand, that door does not take you back.
  [Why this is needed](FAQ.md#ico-and-yorda-come-straight-back-out-of-a-doorway).
- **Natural chains** changes how two chains look as they come down for Ico:
  the one at the Stone Pillar and one underground. In the original game the
  links look squashed until the chain is all the way down. With this on, the
  links stay the right size and more of the chain shows as it comes down. It
  only changes the picture, not the game.

Two more rows are the game's own, and only show up in the pause menu's
Options once you have finished the game:

- **Film effect** is Off or 1 to 4, the game's own film look (a colour tint and grain) over the
  picture. Photo mode has its own Film effect, which works without finishing the game and adds
  one more look, Deja vu (see [Controls](CONTROLS.md#take-a-photo)).
- **Players** is 1 or 2. With 2, a second controller controls Yorda.
## Language

English, French, German, Italian or Spanish. The game starts in your
system's language when it has it. If you change it during play, the menus
change at once and the game's own text and subtitles change at the next
area.

## Achievements

The list of achievements and what each one asks. Secret ones show as ???
until you earn them. You cannot earn achievements while Developer mode is
on.

**Achievement pop-ups**, the first row of the list, is on until you turn
it off. Off stops the pop-up when you earn an achievement. You still earn
them, and the list shows them. The same switch is also on the Extras
page, and it is the same setting in both places.

## Extras

On the title screen's Options this page has Music, Models, Credits,
Character Customization, Add to Steam (on Windows and Linux) and Achievement
pop-ups. In the pause menu's Options it has only Character Customization
and Achievement pop-ups.

- **Music:** listen to the game's music and sound effects.
- **Models:** the model viewer. Look closely at the characters and some
  objects, and play their animations. [The viewer's controls](CONTROLS.md#look-at-the-models).
- **Credits:** watch the ending credits again. It unlocks once you have
  finished the game.
- **Character Customization:** change the colours of Ico and Yorda.
  [Details below](#character-customization).
- **Achievement pop-ups** is the switch that is also the first row of the
  [Achievements](#achievements) list. Turn it on or off in either place.
- **Add to Steam** (on the title screen's Extras page only; it finds Steam
  on Windows and Linux, including the Steam Deck): adds the
  game to your Steam library as a non-Steam game, so you can start it from
  Steam. Close Steam first: the game will not change Steam's list while
  Steam is open, and says so if Steam is running. On a Steam Deck, use
  Desktop Mode, where you can quit Steam from its icon in the taskbar
  (right-click it, then Exit Steam). If Steam has several accounts, the game goes to the one
  used last. It adds no pictures, so Steam shows its plain tile for it. If
  the game is already in your library, the row says so and changes
  nothing. When it adds the game, it first keeps a copy of Steam's old
  list, `shortcuts.vdf.bak`, beside Steam's own list. If you move the game
  to another folder, add it again from there. The row is only on Windows
  and Linux, the Steam Deck included.

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

Developer mode brings back the menu the game's makers used while they built
it: while it is on, Select opens it. Leave it off for normal play. While it
is on, you cannot earn achievements, and they stay off for the rest of that
game even after you turn it off, until you go back to the title screen.

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

A save that has no count of saves and enemies keeps those two lines
hidden until you start a New Game.

## Starting a New Game

**Mirror mode** and **New Game+** are not in Options. When you start a New
Game, the screen after the Vibration question has a row for each, Off or
On. Up and Down move between them, Left and Right pick, and Cross or Start
starts the game.

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

Most lines have a short note above them saying what they do. Lines that
start with `#` are notes, or settings switched off. To switch one on, delete
the `#` at the start of its line.

If your `config.toml` has no such line, add it under the heading shown
below in square brackets. If the file has no such heading either, add the
heading on its own line first.

Under `[photo]`, for photo mode:

| Line | What it does |
| --- | --- |
| `stick_speed = 1.0` | How fast the camera moves, turns, tilts and zooms. `0.5` is half as fast, `2.0` twice as fast. |
| `invert_y = false` | `true` swaps up and down when you look around. |
| `hide_ui = false` | `true` opens photo mode with its help panel hidden. Square in photo mode changes this for you. |
| `film_effect = 0` | The Film effect photo mode opens with: `0` is off, `1` to `4` are the game's four looks, `5` is Deja vu (the grainy veil and the film scratches of Ico's vision). The D-pad Right button in photo mode changes this for you. |
| `png_dir = "screenshots"` | The folder your pictures go in, inside the folder with your saves. |

Under `[video]`:

| Line | What it does |
| --- | --- |
| `window_mode = "windowed"` | The Window mode row: `"windowed"`, `"borderless"` or `"fullscreen"`. If the file has no `window_mode` line, the game reads its `fullscreen =` line instead: `true` is Fullscreen, `false` is Windowed. |
| `effect_glow = true` and the five other `effect_` lines | The six switches of the Effects page. |
| `crt = false`, `crt_mode = "consumer"`, `crt_strength = 1.0` | The CRT filter and CRT strength rows. `crt_mode` is `"scanlines"`, `"consumer"`, `"trinitron"`, `"pvm"` or `"shadow"`. `crt_strength` goes from `0.0` to `1.0`. |
| `crt_scanlines`, `crt_mask`, `crt_halation`, `crt_bloom` | Fine tuning of the CRT filter, each from `0.0` to `1.0`: how strong the dark lines are, how strong the fine pattern of dots or stripes is, how much glow surrounds bright spots and how much bright areas spread softly. A line that is missing, or a negative number, keeps the chosen style's own value. |
| `crt_curvature` | How much the screen bulges, from `0.0` to `0.25`. Missing keeps the style's own value. |
| `gpu_driver = ""` | Android only. The Driver row of the Graphics driver page: empty is the phone's own driver. The game also writes a `gpu_driver_failed` line when a driver did not start. [About Android](ANDROID.md#using-a-different-graphics-driver). |
| `model_pack = true` | The Model pack row. `false` shows the game's own models. |
| `dump_models = false` | The Dump models row. Only works with Developer mode on. |
| `effects_depth = true` | Lets an effects program such as ReShade see how far away things are. [About ReShade](RESHADE.md). |
| `texture_pack_budget_mb`, `texture_pack_precache`, `texture_pack_cache_mb` | How much memory a texture pack may use. [About texture packs](TEXTURE_PACKS.md#if-the-pack-uses-a-lot-of-memory). |

Under `[input]`, for the mouse camera and vibration:

| Line | What it does |
| --- | --- |
| `mouse_camera = true`, `mouse_sensitivity = 1.0`, `mouse_invert_y = false` | The three mouse rows of the Controls page. |
| `mouse_camera_speed = 1.0` | The Mouse camera speed row, from 0.5 to 10. 10 is Instant. |
| `mouse_full_range = false` | The Mouse camera range row. `true` is Full. |
| `mouse_return = true` | The Camera swings back row. |
| `mouse_hold = 0.75` | How many seconds the mouse must stay still before the camera swings back behind Ico. |
| `rumble = true` | `false` stops all vibration: a gamepad's rumble and, on a phone, the phone's own. |

Under `[game]`:

| Line | What it does |
| --- | --- |
| `achievements = true` | The Achievement pop-ups row. |
| `circle_back = true` | The Circle goes back row. |

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
| `keep_image = false` | `true` keeps the copy of your disc image that the first start makes. On Android, a line `keep_image=1` in `ico-pc.ini` works as well. [About Android](ANDROID.md#the-first-start). |

## Choosing the renderer on Windows

The game draws with Vulkan unless you tell it otherwise. To try Direct3D 12 instead, open
`ico-pc.ini` in the `x64` folder, beside `ico_pc_x64.exe`, with Notepad and add this line
(or `backend = "d3d12"` under `[video]` in `config.toml`):

```
backend=d3d12
```

Start the game; a line near the top of `logs/ico-pc.log` that starts with `window:` then says
`D3D12` instead of `Vulkan`. Delete the line to go back to Vulkan.
