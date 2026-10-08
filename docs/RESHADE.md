# ReShade and other effects programs

[Back to the front page](../README.md)

ReShade is a free program that adds picture effects to games: sharper
detail, different colours, ambient shadows and much more. It works with
this game on Windows. On Linux, a program called vkBasalt does a similar
job.

You do not need any of this to play. If you only want to turn the game's
own blur, glow or fog off, use **Options > Effects** instead
([more](OPTIONS.md#effects)).

## Put ReShade in (Windows)

ReShade can hook into the game in two ways. The first one is simpler.

**The simple way (Vulkan)**

1. Download ReShade from its website and run the setup.
2. When it asks for the game, choose `ico_pc_x64.exe` in the `x64` folder.
3. When it asks how the game draws its picture, choose **Vulkan**.
4. Pick the effects you want, then finish the setup.

**The other way (Direct3D 12)**

Use this if the simple way does not work on your PC.

1. Open `ico-pc.ini` in the `x64` folder with Notepad and add this line at
   the end:

   ```
   backend=d3d12
   ```

2. Run the ReShade setup and choose `ico_pc_x64.exe`.
3. When it asks how the game draws its picture, choose **Direct3D
   10/11/12**. ReShade puts a file named `dxgi.dll` in the `x64` folder,
   beside the program.
4. Pick the effects you want, then finish the setup.

To go back, delete the `backend=d3d12` line.

**Check that it worked.** Start the game. ReShade shows a short message at
the top of the screen. The game's log file, `x64\logs\ico-pc.log`, also has
a line like this:

```
window: an effects program is loaded (ReShade); see docs/RESHADE.md
```

Presets you download for ReShade work as in any other game: put the preset
file beside the program and choose it in ReShade's menu.

## Make depth effects work

Some effects need to know how far away each thing on screen is. Ambient
shadows and depth blur are examples. The game gives ReShade that
information, but ReShade needs one setting changed to read it the right
way round:

1. In the game, open ReShade's menu (the Home key, unless you changed it).
2. Find **Edit global preprocessor definitions**.
3. Set `RESHADE_DEPTH_INPUT_IS_REVERSED` to `1`. (The game stores near
   things as 1 and far things as 0, which ReShade calls "reversed".)

To check it, turn on the **DisplayDepth** effect. Near things should look
dark and far things light. If everything looks flat, adjust
`RESHADE_DEPTH_LINEARIZATION_FAR_PLANE` in the same place until the scene
shows clear steps from near to far. Turn DisplayDepth off again when you
are done.

Some things to know:

- With the 4:3 picture, the black bars at the sides count as "very far
  away". Effects may tint them a little.
- The CRT filter (Options > Display) switches the depth information off.
  Depth effects do nothing while it is on.
- The game's own menus and messages are already part of the picture when
  ReShade adds its effects, so the effects show on them too.

If you do not want the game to give ReShade the depth information at all,
close the game, open `config.toml` ([where it is](OPTIONS.md#settings-you-can-only-change-in-a-file)),
and change this line under `[video]`:

```
effects_depth = false
```

## The first start with ReShade is slow

With a lot of effects, the first start can take a while, because ReShade
prepares every effect before the game shows anything. The game knows when
ReShade is there and waits longer before it decides something is wrong.
Later starts are quicker.

## Linux and Steam Deck: vkBasalt

vkBasalt adds effects much like ReShade does. Install it from your
system's software store or package manager. Then start the game with
vkBasalt switched on:

- From a terminal: `ENABLE_VKBASALT=1 ./ico_pc`
- From Steam: right-click the game, choose **Properties**, and put this in
  **Launch options**: `ENABLE_VKBASALT=1 %command%`

vkBasalt's own support for depth effects is still experimental, so those
may not work.

## The Steam overlay

The Steam overlay (Shift+Tab) works with the game, with or without
ReShade.
