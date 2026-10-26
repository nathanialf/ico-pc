## New

## Fixed

- In Mirror mode the D-pad's left and right now walk the way you press.
- Options > Audio > Sound output now steps through Auto, Stereo and Mono in the same order in both directions, and Auto always shows the memory card's own setting. Before, picking Stereo or Mono changed what Auto showed, so the two directions looked different. Stereo and Mono no longer change what the game saves to the card.
- On the North American disc, the August 2001 preview and the June 2001 demo, New Game now starts after the Mirror mode / New Game+ screen. Before, the screen took the button press and then nothing happened.
- On Windows, a message some graphics drivers and libraries send to debuggers when they name a thread was taken for a crash on some computers; the game then closed about a minute later. It is ignored now and the log says who sent it.
- Direct3D 11 on Windows now draws the game. Before, the screen stayed black because the graphics driver's shader compiler refused the game's shaders.
- With the OpenGL renderer, OBS's game capture of the game window is upright now. Before, the capture was upside down while the window itself looked right.
- The black bars of the load screen no longer flicker when it changes at Frame rate 60.
- On the North American disc, the August 2001 preview and the June 2001 demo, Options no longer shows Hold type, Film effect or Players. Those discs' own game has none of them: holding Yorda's hand there always works as B (press R1 once to take her hand, again to let go), and a Film effect change started an unrelated animation instead.
- On the June 2001 demo, Extras no longer shows Credits. The demo disc does not hold the credits' scenes, so the row could never play.
- With MSAA anti-aliasing on, the thin dotted lines of mist along seams above the original resolution are now filled in, as without it.
