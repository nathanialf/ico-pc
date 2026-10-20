**Please note:** this release has had only automated checks. The Linux files were checked by unpacking them, not by playing. Nobody has played it through, nobody has run it on an Android handheld, nobody has yet heard the sound on a Linux desktop, and the white-line fix below could not be tried, because the lines never appeared on the test machine. If something looks or sounds wrong, keep the previous version and send the game's report file (the newest file in the `logs` folder that starts with `report-`, or `ico-pc.log`) with a short description of what you saw.

## New

- Linux has an AppImage, the game in a single file, for 64-bit x86 PCs (`-linux-x86_64.AppImage`) and 64-bit ARM (`-linux-aarch64.AppImage`). Allow the file to run as a program, then double-click it. Saves go to the same place as with the other Linux downloads. A folder named `userdata` beside the file keeps everything with the file, for example on an external drive.

## Fixed

- Sound on Linux desktops that use PipeWire or PulseAudio. The game used to talk to the sound card directly and could play through the wrong output or none. It now plays through your desktop's sound system, and the list in Options > Audio shows your system's outputs.
- Android: the first start after installing a new version now prepares its graphics from nothing. The graphics the previous version prepared are dropped; saves and settings stay.
- Android handhelds that run low on memory while the game prepares its graphics: the game now stops preparing when the system warns that memory is critically low and goes on, and the next start does not prepare again.
- When the graphics driver stops answering while the game is drawing, the game now says so and names the report file to attach, instead of looking like a frozen game. On Android the next start has the graphics driver check every read, as after any time the graphics stop.
- Thin white lines flickering along the edges of walls and floors while the camera moves. The game draws extra pictures between its own steps, and in those the pieces of a wall were placed by slightly different arithmetic, which left hairline gaps that the room's mist painted white. The game now places every unmoving piece the same way between its steps. This was not caused by the Japanese or Korean disc support. If lines still show, press F12 while they show and send the game's report file, the `.zip` that F12 saves and the picture it saves whose name ends in `-present.png`.
- On phones and handhelds at some screen sizes, parts of the scenery could be missing and the log file grew to many megabytes, because the game prepared more drawing programs than it had room to keep. The room is now sized for all of them, and a program that cannot be made is reported once instead of on every frame.
