**Please note:** several things in this release have not been tried on real hardware. Direct3D 11 has had its shaders corrected but has not run on a Windows computer since, so it may still not draw; OpenGL has been changed to do less work and has not run on a real graphics card since; the game on Vulkan 1.1 drivers has only run on a test setup that imitates one; Players and Hold type A on the North American disc, the August 2001 preview and the June 2001 demo have not been played through (on those discs, taking Yorda's hand by walking up to her with R1 already held may not work with Hold type A); and the Android changes to the driver file picker have not been tried on a phone whose own file picker misbehaves. If something looks or plays wrong, keep the previous version and send the game's report file (the newest file in the `logs` folder that starts with `report-`, or `ico-pc.log`) with a short description of what you saw, and say which disc you played. For a picture problem on Linux, also send the line of `logs/ico-pc.log` that starts with `rhi_vk: swapchain`. For seams, ripples or mist, press **F12** (or use **Options > Save a picture for a bug report**) while it shows and send the `.zip` file and the picture beside it.

## New

- The game now also runs on graphics drivers that support only Vulkan 1.1, such as the stock driver of some older Adreno phones and tablets on Android. It has not been tried on such a driver yet; if it does not start, the box on Android offers a driver package as before, and a report either way is welcome.
- A second player can take Yorda on the North American disc, the August 2001 preview and the June 2001 demo too, from the pause menu once the game has been finished (Options > Gameplay > Players).
- Hold type (hold R1 to keep Yorda's hand, or press once to take it and once to let go) can now be chosen on the North American disc, the August 2001 preview and the June 2001 demo too; each disc starts with its own way.
- Options > Display > Frame rate counter shows the frames per second in the corner of the screen. It starts Off.
- With the OpenGL renderer, the game repeats less work for each picture, and the log now tells the graphics card's own time apart from the time it spends waiting for the rest of the game. If OpenGL runs slower than Vulkan on your computer, a log of a minute's play shows which of the two holds it back.

## Fixed

- On the North American disc, the August 2001 preview and the June 2001 demo, New Game now starts after the Mirror mode / New Game+ screen. Before, the screen took the button press and then nothing happened.
- On the North American disc, the August 2001 preview and the June 2001 demo, Options no longer shows Film effect. Those discs' own game has no film looks, and a Film effect change started an unrelated animation instead.
- On the June 2001 demo, Extras no longer shows Credits. The demo disc does not hold the credits' scenes, so the row could never play.
- In Mirror mode the D-pad's left and right now walk the way you press.
- Options > Audio > Sound output now steps through Auto, Stereo and Mono in the same order in both directions, and Auto always shows the memory card's own setting. Before, picking Stereo or Mono changed what Auto showed, so the two directions looked different. Stereo and Mono no longer change what the game saves to the card.
- With MSAA anti-aliasing on, the thin dotted lines of mist along seams above the original resolution are now filled in, as without it.
- With Full pixel on, the puddle ripples in the drained room no longer draw as bright white segments.
- The black bars of the load screen no longer flicker when it changes at Frame rate 60.
- On Windows, a message some graphics drivers and libraries send to debuggers when they name a thread was taken for a crash on some computers; the game then closed about a minute later. It is ignored now and the log says who sent it.
- Direct3D 11 on Windows now draws the game. Before, the screen stayed black because the graphics driver's shader compiler refused the game's shaders.
- With the OpenGL renderer, OBS's game capture of the game window is upright now. Before, the capture was upside down while the window itself looked right.
- On Android, when the file picker for Options > Graphics driver > Add a driver closes at once without letting you choose, or never opens, the game no longer waits for ever or stays silent. It says so and names the folder to copy the driver's zip file into (Android/data/com.defnf.icopc/files/drivers). At the start, the box that offers a driver file now uses a zip from that folder in this case too.
- On Android, the box shown when the phone's own graphics driver cannot run the game now names that folder, and says that a driver package is loaded by the game itself, without the phone's own Vulkan loader.
- On Android phones with a Qualcomm chip, after the graphics stopped responding on the phone's own driver, the note under Options > Graphics driver now points to the Turnip driver package route.
