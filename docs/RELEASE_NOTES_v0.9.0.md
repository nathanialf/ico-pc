**Please note:** this release adds three more discs the game can play and has had limited testing on them (automated checks and one start to the first scene of each, plus the rc checks described in the notes). If something looks or plays wrong, keep the previous version and send the game's report file (the newest file in the `logs` folder that starts with `report-`, or `ico-pc.log`) with a short description of what you saw, and say which disc you played: the European disc (SCES-50760), the Japanese disc (SCPS-11003), the Korean disc (SCPS-56001), the North American disc (SCUS-97113), the August 2001 preview disc or the June 2001 magazine demo.

## New

- **The June 2001 magazine demo disc** (`Ico_JUN26.iso`): setting it up keeps only ICO's part of the disc (about 280 MB, none of the other games on it) and checks that part instead of reading the whole 4.4 GB image, so the first start is much quicker. On Android the 4.4 GB image is first copied into the app, which needs about 5.6 GB free and takes a few minutes; the screen says so while it copies. The message shown when there is not enough space, and the one shown when the copy fails, now come in the menu language you chose under Options, when you have chosen one.
## Fixed

- On phones, tablets and Macs, the thin dotted lines of mist along the seams of walls and floors above the original resolution are now filled in, as on other computers.
- With MSAA anti-aliasing on, hairline gaps along walls and floors no longer show as dotted lines of mist.
- With MSAA anti-aliasing on, small openings onto the sky no longer glow brighter or wider than with it off.
- MSAA anti-aliasing finishes each picture once instead of twice in scenes with shadows, so it costs less.
- The puddle ripples no longer slide against their reflection while the camera moves.
