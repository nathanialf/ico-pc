## Fixed

- An attempted fix for the frame rate dropping during the opening and in some scenes on Android phones (moto g24) with Frame rate Original. In the first moments of play the game now checks whether the phone's graphics driver holds each picture until it is drawn; where it does, the next picture is prepared while the one before is still being drawn instead of leaving that frame out. Other phones present as before, and the pictures look the same.
- An attempted fix for slow scenes on some phones (moto g24), such as parts of the opening: the game now spends less graphics work on the scenery it leaves out of the picture. The first start after the update takes a little longer while the game prepares its graphics again.
- The game closing the moment it is opened on a Pixel 10 Pro Fold with an Android 17 preview. A part used only for Options > Graphics driver ran at every start and no longer works with that system; it now runs only when a driver of your own is opened.
