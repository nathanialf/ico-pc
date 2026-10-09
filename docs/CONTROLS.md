# Controls

[Back to the front page](../README.md)

## Play with a gamepad, a keyboard or a mouse

A gamepad, the keyboard and the mouse all work, even at the same time.

Gamepad buttons go by where they sit, not by what is printed on them. The
bottom face button is always Cross, whether your pad is an Xbox,
PlayStation or Switch pad.

| PS2 button | Gamepad | Keyboard | Mouse |
| --- | --- | --- | --- |
| Cross (confirm, jump, call Yorda) | bottom face button (A on Xbox) | Space | left button |
| Circle | right face button | E | right button |
| Square | left face button | Q | |
| Triangle (back) | top face button | R | |
| L1 / R1 | shoulder buttons | Tab or ` / F | middle button (R1) |
| L2 / R2 | triggers | Z / X | |
| L3 / R3 (press a stick in) | press a stick in | V / B | |
| Start / Select | start / back (or view) | Enter / Backspace | |
| D-pad (menus) | d-pad | arrow keys | |
| Left stick (move) | left stick | W A S D (hold Left Shift to walk) | |
| Right stick (look) | right stick | I J K L | move the mouse |

A few more keys on a PC:

- **Escape** closes the game.
- **Alt+Enter** switches between a window and the whole screen (the
  Borderless or Fullscreen choice you made under Window mode).
- **F12** saves a picture of the current moment for a bug report. See
  [When something goes wrong](TROUBLESHOOTING.md#save-a-picture-of-a-problem).

Escape and F12 cannot be changed. Everything else can be, in **Options > Controls > Remap controls**.

## Look around with the mouse

On a computer, the mouse moves the camera while you play. The pointer
disappears as soon as Ico is on the move, and moving the mouse turns the
camera the way the right stick does. Keep the mouse still for a moment and
the camera swings back behind Ico on its own, just like letting go of the
stick.

The pointer comes back whenever a menu is open, during a film, while the
game is loading, and when you switch to another window.

You can change how it feels in **Options > Controls**:

- **Mouse camera** turns it off if you would rather keep the pointer.
  The mouse buttons still work.
- **Mouse sensitivity** sets how far the camera turns for the same move of
  the mouse.
- **Invert mouse up/down** swaps up and down.

These rows are not shown on phones and tablets.

## Look at the models

The model viewer lets you look closely at the characters and some of the
objects. Open it from the title screen: **Options > Extras > Models**.
Pick a model from the list and press Cross.

| To do this | Press |
| --- | --- |
| Turn the model | right stick |
| Move the model around the screen | left stick |
| Zoom out / zoom in | L2 / R2 (keyboard Z / X) |
| Pick the previous or next animation | L1 / R1, or Up / Down |
| Play the animation | Cross |
| Repeat the animation over and over, or stop repeating | Square |
| Go back to the list | Triangle or Circle |

With Developer mode on, **Select** also saves the model's files for
people who make model packs. A message tells you how many files it saved
and where. [More about model packs](MODEL_PACKS.md#make-a-model-pack).

## Change the colours of Ico and Yorda

**Options > Extras > Character Customization** has a row for each part you
can recolour. On the title screen the character is shown beside its own
rows, and each colour you pick shows on the model at once.

| To do this | Press |
| --- | --- |
| Pick a row | Up / Down |
| Pick a colour for the row | Left / Right |
| Put the row back to its original colour | Square |
| Show the other character (title screen) | L1 / R1, or Cross on **Switch to Yorda** / **Switch to Ico** |
| Turn the model (title screen) | right stick |
| Zoom out / zoom in (title screen) | L2 / R2 (keyboard Z / X) |
| Go back | Triangle or Circle |

From the title screen, going back returns you to the title screen with
Options open on Extras. In the pause menu, the game behind the menu
changes colour as you choose. [All the rows](OPTIONS.md#character-customization).

## Take a photo

Photo mode pauses the game and shows it from a camera you move around,
with every effect the game has, so you can save a picture. During play,
press Start and choose **Photo mode**.

The camera starts as a free camera. You fly it around like a drone:

| To do this | Press |
| --- | --- |
| Move forward, back and to the sides | left stick |
| Look around | right stick, or move the mouse |
| Rise / sink | Up / Down |
| Tilt the picture | L1 / R1 |
| Zoom out / zoom in | L2 / R2 |
| Change the speed (Slow, Normal, Fast) | R3, press the right stick in (keyboard B) |
| Put the camera back where it started | Select |
| Save a picture | Cross, or click the left mouse button |
| Hide or show the help panel | Square |
| Leave photo mode | Triangle, Circle or Start, or click the right mouse button |

Press **L3** (press the left stick in, or keyboard V) to switch to the
orbit camera. It circles around the spot you were looking at:

- The left stick, or moving the mouse, circles around that spot.
- The right stick moves nearer, farther and to the sides.
- Up and Down zoom.

Press L3 again to go back to the free camera. Select puts back only the
camera you are using.

The help panel at the bottom tells you which camera you are using and its
speed, and shows each button beside what it does: the game's own button
pictures, and the names of the buttons it has no picture for (the sticks,
L3, R3, Up, Down and Select). If you play on the keyboard, it shows the
names of your keys instead, like [Space] (the panel changes as soon as
you press a key or a mouse button). When you hide
it with Square, the game remembers, and photo mode opens with the panel
hidden next time too. Press Square again to show it.

If you move the camera quickly into another room, you may see parts the
game had not prepared yet.

Your pictures are saved as PNG files in a `screenshots` folder, in the
same folder as your saves. [Where that is](PORTABLE_MODE.md#where-your-saves-are).

You can change how fast the camera moves, or swap up and down when you
look around. Close the game, open `config.toml` (it is next to your saves)
in a text editor, and find the `[photo]` part. For example, this line
makes the camera move half as fast:

```
stick_speed = 0.5
```

and this one swaps up and down:

```
invert_y = true
```

## Touch controls (phones and tablets)

On a phone or tablet, buttons are drawn on the screen over the game:

- **The stick** is on the left. Put your thumb down anywhere in the lower
  left part of the screen, and that spot becomes the middle of the stick.
  Slide your thumb to walk. Slide it out to the ring around the stick to
  run.
  As with a real stick, running between the eight main directions
  needs **Options > Gameplay > Analogue stick fix**.
- **The look pad** is the upper right part of the screen. Slide a finger
  there to look around, as you would with the right stick.
- **Cross, Circle, Square and Triangle** sit in the lower right corner,
  with the PlayStation symbols on them.
- **The D-pad** sits above the stick.
- **L1 and L2** are in the top left corner, **R1 and R2** in the top
  right.
- **Start and Select** are at the top in the middle.

You can use several fingers at once, for example the stick and a button.

The buttons fade away 5 seconds after you last touched the screen. Touch
the screen and they come back. When you connect a gamepad, they go away
and the screen stops acting as buttons, so a stray finger does nothing.

Three rows in **Options > Controls** change them. These rows only show up
on a device with a touch screen.

- **Touch controls:** Auto is the normal choice and works as described
  above. Always keeps the buttons on the screen all the time, and they
  keep working with a gamepad connected. Off turns them off.
- **Touch size:** Small, Medium or Large.
- **Touch opacity:** how see-through they are, from 25 % (faint) to 100 %
  (solid).

On Android, the phone's **Back** button or gesture opens the pause menu,
like Start. [More about Android](ANDROID.md#controls).
