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

## Ico and Yorda come straight back out of a doorway

**When I take Yorda's hand through the door at the top of the ladder in the
East Reflector, the screen goes black and we both walk straight back out.
Alone it works. Why?**

That is a bug in the original game, and it happens on a real PS2 too. When
Ico leads Yorda through that door, the game sends you both straight back
out as soon as you arrive.

To go through, open **Options > Gameplay** and turn on **East Reflector door fix**. For a
few seconds after you come through a door holding her hand, that door does
not take you back. It is off unless you turn it on.

## The game closed and opened again by itself

**The window disappeared, then came back. Why?**

Some graphics drivers crash while the game gets its graphics ready. On
Windows, Linux and Mac the game notices and starts itself again, up to four
times, each time trying a gentler way: first without the graphics it saved
last time, then with simpler blending (when the crash was in the way the
game draws see-through effects), then (on Windows) with Direct3D 12, then
getting the graphics ready as you play instead of all at once. You need do nothing, and your
settings are left as they are. If it still cannot start, Windows shows a
message that the graphics driver crashed; on Linux and Mac the game closes
and the log says why. On an Android phone a message says the graphics
driver crashed and the game closes; on an iPhone the game closes.

The game also starts itself again when the graphics card stops responding
while you play (often a card running faster than its maker set it). It
starts again with simpler blending, which can make a few see-through
effects look very slightly different; if the card stops again, on
Windows it then tries the other graphics driver type (Direct3D 12 or
Vulkan). It starts at the title screen, as a fresh start: anything you had
not saved is lost, as when you quit. Your settings are left as they are,
and the next time you start the game it is back to normal. If the card
still stops, a message says so and the game closes; see
[the graphics card stopped responding](TROUBLESHOOTING.md). On a phone the
message shows at once.

## The screen stays black but I hear the game

**The window or screen stays black, but the music and sounds play. What is
wrong?**

The game is running and only the picture is missing. That points to the
graphics driver. Update it from the maker's site (NVIDIA, AMD or Intel),
then start the game again. If the picture is still black, send a report as
described in [When something goes wrong](TROUBLESHOOTING.md#send-a-problem-report)
and attach the log.

## How do I use a texture pack?

A texture pack is a set of sharper pictures that fans made to replace the
game's own. The game comes with a `textures` folder for one.

1. Copy the pack's `SCES-50760` folder into the `textures` folder beside
   the program. On Windows that is inside the `x64` folder. On a Mac or an
   iPhone the download has no such folder: [the full guide](TEXTURE_PACKS.md)
   shows where to make one.
2. Start the game.
3. Open **Options > Display** and check that **Texture pack** says **On**.

Packs made for PCSX2, a program that plays PS2 games on a PC, work as they are. You do not need to
rename or convert anything. [The full guide](TEXTURE_PACKS.md) has more.

## Can I turn off the blur, the glow or the fog?

Yes. Open **Options > Effects**. There are six switches: Glow, Depth of
field, Screen softening, Motion blur, Fog and Cinematic bars (the black
bars of cutscenes). All of them On is the picture
the PS2 gives. [What each one does](OPTIONS.md#effects).

## Why is there a thin black border around the picture?

The PS2 itself drew a thin black border around the picture, a few dots wide
at the sides and a little more at the top and bottom, and the game keeps
it. To hide it, turn on **Full pixel (exact screen pixels)** in
**Options > Effects** (it is on by default with the Enhanced Preset).
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

## How are the movies shown without comb lines?

The movies on the disc are stored the way a TV shows them: each frame is
two half-pictures, one made of the even lines and one of the odd lines,
taken a moment apart. Shown together, anything that moves between the two
gets jagged comb lines along its edges. The game shows the two halves one
after the other and fills in the missing lines of each, so moving edges
stay smooth and still parts keep their full detail.

## How do I start the game from Steam?

On Windows, Linux and the Steam Deck: close Steam, start the game, and choose
Options > Extras > Add to Steam on the title screen.
The game then shows in your Steam library, and on a Steam Deck in Game
Mode. If Steam is open, the game tells you to close it first.
[Details](OPTIONS.md#extras) and [Steam Deck tips](TROUBLESHOOTING.md#steam-deck).

## My browser or Windows says the download is dangerous

The game is a small new program that is not signed by a company, and
browsers and Windows warn about any program they have not seen many people
download. The warning is about the file having no history yet, not about
anything found inside it.

In Chrome, open the downloads list, choose the three dots next to the file
and pick **Keep** (or **Keep dangerous file**), then **Keep anyway**. In
Edge, open the three dots next to the download, then pick **Keep**, **Show
more**, **Keep anyway**. If Windows shows "Windows protected your
PC" when you start the game, choose **More info**, then **Run anyway**.

You can check the files yourself: upload the zip to [VirusTotal](https://www.virustotal.com/) and
read what its scanners say. The whole program is open source, so you can also read what it does or
build it yourself
([how to build it](https://github.com/nathanialf/ico-pc/blob/main/docs/BUILDING.md)).

## Where are my saves?

In a folder the game makes in your user folder, not beside the program.
[Where that is on each system](PORTABLE_MODE.md#where-your-saves-are).

If you would rather keep everything beside the program, for example on a
USB stick, use [portable mode](PORTABLE_MODE.md#keep-everything-in-one-folder).

## Can I bring my PS2 save over?

Yes, if you have it as a memory card file, the kind PS2 emulators and
save backup tools make. [How to bring it over](PORTABLE_MODE.md#bring-a-ps2-save-over).

## Which devices does it run on?

- Windows and Linux PCs, and the Steam Deck.
- 64-bit ARM Linux handhelds and boards.
- Android phones and tablets.
- Apple-silicon Macs ([how to install](../README.md#mac)).
- iPhones, through LiveContainer ([how to install](../README.md#iphone)).

The maintainer has no Apple device, so the Mac and iPhone versions are
not tested here; problem reports are welcome. The
[supported platforms table](../README.md#supported-platforms) says what to
download for each and how well it is tested.

## Does it work on my phone?

On most 64-bit Android phones and tablets from 2022 or newer, with
Android 10 or later. [The Android guide](ANDROID.md) has the details.

On an iPhone with iOS 15 or newer, it runs inside LiveContainer.
[How to install it](../README.md#iphone).

## Can I use ReShade?

Yes, on Windows. On Linux, vkBasalt does a similar job.
[How to set it up](RESHADE.md).

## Do I need the disc image every time?

No. The first start copies the game's files out of the disc image. After
that the game never opens the image again, so you can move it or delete
it.

## Which disc image works?

Only the PAL version of ICO, numbered SCES-50760, as a `.iso`, `.chd` or `.bin` file (a `.cue` file
that points to the `.bin` works too). The US, Japanese and later editions do not work. If the image
is not the right one, the game says so in a message when you start it, and
the log says so too.

## How do I take a photo?

Press Start during play and choose **Photo mode**. The game pauses, and you
move the camera around and see the scene as the game would draw it from
there, with all its effects. Press Cross to save a picture.
[The photo mode controls](CONTROLS.md#take-a-photo).

## How do I close the game?

Choose **Quit to desktop** on the title screen (**Quit game** on a phone
or tablet). While you play, open the pause menu (Start, or Escape on a
computer), choose **End Game** to reach the title screen, and quit from
there. On a computer you can also close the window.

## Something went wrong. What now?

Look at [When something goes wrong](TROUBLESHOOTING.md). It explains the
error messages and how to send a useful report.
