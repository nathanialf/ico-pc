# Questions and answers

[Back to the front page](../README.md)

## Ico walks instead of running

**Ico walks when I push the stick all the way, or keeps switching between
walking and running. Why can't he run?**

That is how the original game works. It only counts a push as "all the way"
in eight directions: up, down, left, right and the four diagonals. Push
anywhere in between and Ico never reaches running speed.

The game keeps that behaviour unless you change it. To run in any
direction, open **Options > Gameplay** and turn on **Analogue stick fix**.

(People reported this in
[issue 5, "Controller Stick Issue"](https://github.com/nathanialf/ico-pc/issues/5)
and [issue 6, "Ico can't run"](https://github.com/nathanialf/ico-pc/issues/6).)

## How do I use a texture pack?

A texture pack is a set of sharper pictures that fans made to replace the
game's own. The game comes with a `textures` folder for one.

1. Copy the pack's `SCES-50760` folder into the `textures` folder beside
   the program. On Windows that is inside the `x64` folder.
2. Start the game.
3. Open **Options > Display** and check that **Texture pack** says **On**.

Packs made for PCSX2, a program that plays PS2 games on a PC, work as
they are. You do not need to rename or convert anything. [The full guide](TEXTURE_PACKS.md) has more.

## Can I turn off the blur, the glow or the fog?

Yes. Open **Options > Effects**. There are six switches: Glow, Depth of
field, Screen softening, Motion blur, Fog and Cinematic bars (the black
bars of cutscenes). All of them On is the picture
the PS2 gives. [What each one does](OPTIONS.md#effects).

(Asked for in [issue 11](https://github.com/nathanialf/ico-pc/issues/11).)

## Why is there a thin black border around the picture?

The PS2 itself drew a thin black border around the picture, a few dots wide
at the sides and a little more at the top and bottom, and the game keeps
it. To hide it, turn on **Full pixel (no border)** in **Options > Effects**.
It shows the strip of picture the PS2 hid under the border, so the picture
fills the box. Nothing is cut off or enlarged, and the picture stays as
sharp as it is without the option. With Video mode at 50 Hz the strip at
the top and bottom is a little taller.
[What it does](OPTIONS.md#effects).

## Why does the CRT filter switch the picture back to the original resolution?

The CRT filter draws the PS2's own picture dots with dark lines between
them, like a tube television. Drawn over a sharper picture, every tiny dot
would get its own dark lines, and the whole screen would look like a fine
mesh, a bit like looking through a screen door. So the filter keeps the
original resolution on purpose. Texture packs still show through it.

## Why did the movies show comb lines on moving edges?

The movies on the disc are stored the way a TV shows them: each frame is
two half-pictures, one made of the even lines and one of the odd lines,
taken a moment apart. Shown together, anything that moves between the two
gets jagged comb lines along its edges. The game now shows the two halves
one after the other and fills in the missing lines of each, so moving
edges stay smooth and still parts keep their full detail.
(Reported in [issue 41](https://github.com/nathanialf/ico-pc/issues/41).)

## Where are my saves?

In a folder the game makes in your user folder, not beside the program.
[Where that is on each system](PORTABLE_MODE.md#where-your-saves-are).

If you would rather keep everything beside the program, for example on a
USB stick, use [portable mode](PORTABLE_MODE.md#keep-everything-in-one-folder).

## Can I bring my PS2 save over?

Yes, if you have it as a memory card file, the kind PS2 emulators and
save backup tools make. [How to bring it over](PORTABLE_MODE.md#bring-a-ps2-save-over).

## Does it work on my phone?

On most 64-bit Android phones and tablets from 2022 or newer, with
Android 10 or later. [The Android guide](ANDROID.md) has the details.

## Can I use ReShade?

Yes, on Windows. On Linux, vkBasalt does a similar job.
[How to set it up](RESHADE.md).

## Do I need the disc image every time?

No. The first start copies the game's files out of the disc image. After
that the game never opens the image again, so you can move it or delete
it.

## Which disc image works?

Only the PAL version of ICO, numbered SCES-50760, as a `.iso`, `.chd` or
`.bin` file (a `.cue` file that points to the `.bin` works too). The US, Japanese and later editions do not work. If the image is not
the right one, the game says so when you start it.

## How do I take a photo?

Press Start during play and choose **Photo mode**. The game pauses, and you
move the camera around and see the scene as the game would draw it from
there, with all its effects. Press Cross to save a picture.
[The photo mode controls](CONTROLS.md#take-a-photo).

## How do I close the game?

Choose **Quit to desktop** on the title screen (**Quit game** on Android).
While you play, open the pause menu (Start, or Escape on a PC), choose
**End Game** to reach the title screen, and quit from there. On a PC you
can also close the window.

## Something went wrong. What now?

Look at [When something goes wrong](TROUBLESHOOTING.md). It explains the
error messages and how to send a useful report.
