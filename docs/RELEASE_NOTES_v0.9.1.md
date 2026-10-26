## New

- Hold type (hold R1 to keep Yorda's hand, or press once to take it and once to let go) can now be chosen on the North American disc, the August 2001 preview and the June 2001 demo too; each disc starts with its own way.
- Options > Display > Frame rate counter shows the frames per second in the corner of the screen. It starts Off.
- A second player can take Yorda on the North American disc, the August 2001 preview and the June 2001 demo too, from the pause menu once the game has been finished.

## Fixed

- On Android, when the file picker for Options > Graphics driver > Add a driver closes at once without letting you choose, or never opens, the game no longer waits for ever or stays silent. It says so and names the folder to copy the driver's zip file into (Android/data/com.defnf.icopc/files/drivers). At the start, the box that offers a driver file now uses a zip from that folder in this case too.
- On Android, the box shown when the phone's own graphics driver cannot run the game now names that folder, and says that a driver package is loaded by the game itself, without the phone's own Vulkan loader.
- On Android phones with a Qualcomm chip, after the graphics stopped responding on the phone's own driver, the note under Options > Graphics driver now points to the Turnip driver package route.
- In Mirror mode the D-pad's left and right now walk the way you press.
- Options > Audio > Sound output now steps through Auto, Stereo and Mono in the same order in both directions, and Auto always shows the memory card's own setting. Before, picking Stereo or Mono changed what Auto showed, so the two directions looked different. Stereo and Mono no longer change what the game saves to the card.
- On the North American disc, the August 2001 preview and the June 2001 demo, New Game now starts after the Mirror mode / New Game+ screen. Before, the screen took the button press and then nothing happened.
- On Windows, a message some graphics drivers and libraries send to debuggers when they name a thread was taken for a crash on some computers; the game then closed about a minute later. It is ignored now and the log says who sent it.
- Direct3D 11 on Windows now draws the game. Before, the screen stayed black because the graphics driver's shader compiler refused the game's shaders.
- With the OpenGL renderer, OBS's game capture of the game window is upright now. Before, the capture was upside down while the window itself looked right.
- The black bars of the load screen no longer flicker when it changes at Frame rate 60.
- On the North American disc, the August 2001 preview and the June 2001 demo, Options no longer shows Film effect. Those discs' own game has no film looks, and a Film effect change started an unrelated animation instead.
- On the June 2001 demo, Extras no longer shows Credits. The demo disc does not hold the credits' scenes, so the row could never play.
- With MSAA anti-aliasing on, the thin dotted lines of mist along seams above the original resolution are now filled in, as without it.
- With Full pixel on, the puddle ripples in the drained room no longer draw as bright white segments.
