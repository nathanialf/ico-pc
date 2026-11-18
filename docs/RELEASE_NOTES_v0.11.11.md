## Fixed

- An attempted fix for stretched and wrongly coloured triangles across the picture on some Mali phones (moto g24) since v0.11.8. A blending shortcut added in v0.11.8 is taken out again, so those phones draw as they did in v0.11.7, where the picture was right. Cutscenes on those phones run as they did in v0.11.7.
- An attempted fix for the Retroid Pocket 6 restarting or the game closing during start. On Qualcomm's driver the game now prepares less graphics work up front once the driver's own memory for the game has grown, and prepares the rest as it is first needed.
- The game's log files now survive the device restarting while the game runs, and the next start says when the device restarted.
- An attempted fix for the Retroid Pocket 6 restarting by itself when the graphics driver stops answering in play: the game now closes with a message within about a second in that case, before the device goes down.
