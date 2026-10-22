**Please note:** this release has had only limited testing: one computer and automated checks. It may look or play wrong in places the previous version did not. If it does, keep the previous version and send the game's report file (the newest file in the `logs` folder that starts with `report-`, or `ico-pc.log`) with a short description of what you saw.

## New

- **Options > Effects > Motion blur** has a third choice, **Smooth**. Off and Original are as before, and Original is still what the game starts with. Original moves the trail on once for each of the game's own frames, so above the Original frame rate it stands still for a moment between them while the picture in front keeps moving. Smooth moves the same trail on with every frame you see. It is as long as Original's, and it uses a little more of the graphics card, which matters most on a phone.
- **Edit layout** on a phone with a camera hole or rounded corners now lets you drag a button under the hole or into the corner beside it. The game's own arrangement still keeps clear of them, so a button you have not moved stays where it was.
- **Options > Developer mode > Save a picture for a bug report**, for phones and handhelds that have no F12 key. Choose it and leave the menu: 3 seconds later the game saves what is on the screen, with the log, in the `dumps` folder. If the menu is not reachable while the problem shows, putting `dump_every=300` in the settings file makes the game save a picture every few seconds instead; the Android page says how.

## Fixed

- The ripples on wet floors no longer grow little spikes at their corners. Their edges are now drawn the way the PlayStation 2 draws them.
- A bright line down the left edge of the screen on some wide phones, seen in the room with the hanging chain before you meet Yorda. The two outermost columns of the picture, which the PlayStation 2 never showed, now repeat the picture next to them. If the line is still there, send the game's report file and a picture made with the new row above.
