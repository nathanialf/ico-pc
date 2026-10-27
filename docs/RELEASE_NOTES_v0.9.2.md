**Please note:** Several things in this version have not been tried on real hardware, because nothing was run on a real machine while it was made. These are the whole reorganised and scrolling Options menu, Downsampling, the time to rescue Yorda, the change to how the game holds back extra pictures (including OpenGL), and Direct3D 11 on a real graphics card. If something looks wrong, please send `logs/ico-pc.log` and say what you chose in Options.

Direct3D 11 may still show a black screen on some graphics cards. Running the game's drawing tests on Microsoft's software renderer found a cause of the black screen: Direct3D 11 threw away the settings every drawing step needs, so nothing was drawn. This version fixes that and two smaller faults the same tests found. The fixed version then passed the drawing tests on Microsoft's software renderer, but Direct3D 11 has still not been seen drawing the game on a real graphics card. If it stays black for you, choose Vulkan or Direct3D 12 in Options > Graphics > Graphics API and send `logs/ico-pc.log` from a start with Direct3D 11.

## New

- Options > Gameplay > Time to rescue Yorda sets how long a shadow takes to pull Yorda into a pit: Half, Original, Longer, Double or Triple. It starts at Original, the game's own time.
- Options > Picture > Downsampling, To original size: the picture is drawn at the Resolution you chose and then shrunk evenly to the game's own size, for a clean picture at the original size with smooth edges. It needs Resolution 2x or more and changes nothing with the CRT filter or at Resolution Window.
- Options is reorganised: a Picture page (anti-aliasing, filtering, CRT, texture and model packs) and a Developer page (developer mode, dumps) take rows from Display, Effects and the first page.
- The longer Options pages (the first page, Controls and Characters) now show ten rows at a time with more room between them, and scroll as you move down or up. Back stays at the bottom, and a small arrow at the right edge shows there are more rows above or below. Click or tap an arrow, or turn the mouse wheel, to move a row.
- When Direct3D 11 draws nothing, the game's log now says exactly how its start-up drawing checks drew, so one log shows what goes wrong.
- The Anti-aliasing note in Options now says what supersampling costs (2x draws four times the pixels, 4x sixteen times) and to lower it if the frame rate counter drops to 30 at Frame rate 60.

## Fixed

- At an uncapped or high frame rate, one slow moment no longer holds the game at 60 pictures a second for the rest of the session. When the game does hold back the extra pictures because the graphics card cannot keep up, it now goes back to the full rate within about two seconds once the card has the time again.
- With OpenGL, waiting for the screen no longer counts as a slow graphics card. Before, at Frame rate 60 with Vsync on, scenes the card draws easily could still run at 30 pictures a second.
- With anti-aliasing off and a Resolution that draws more detail than the window can show, the picture is now shrunk evenly to the window instead of skipping some of that detail, so fine lines and edges shimmer less.
- With Direct3D 11, the screen could stay black because the settings every drawing step needs never reached the graphics card. This is fixed, along with some of the game's models never reaching the graphics card. The fixed version passed the drawing tests on Microsoft's software renderer; it has not yet been seen working on a real graphics card.
- On the Asian disc and the August 2001 preview, the game no longer crashes right after you climb the chains and jump out of the window at the start. The same fault could also stop the game on entering a few later areas on the Asian disc, and as the ending credits begin on the Korean disc; those are fixed too.
