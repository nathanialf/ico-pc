**Please note:** this release adds the Korean disc, and the Korean disc has had only limited testing: one computer, one phone and automated checks. It may look or play wrong in places the other discs do not. If it does, keep the previous version and send the game's report file (the newest file in the `logs` folder that starts with `report-`, or `ico-pc.log`) with a short description of what you saw. Every report must say which disc it was made with, the European disc (SCES-50760), the Japanese disc (SCPS-11003) or the Korean disc (SCPS-56001).

## New

- The Korean disc of ICO (SCPS-56001) plays too, from the same download on Windows, Linux, the Steam Deck, Android, iPhone and Mac. At the first start, choose your disc image of the Korean disc as a .iso, .chd, .bin or .cue file, the same way as for the other discs ([the three discs](https://github.com/nathanialf/ico-pc#the-three-discs)).
- The Korean disc's menus, signs, memory card screens, stage names and subtitles are the disc's own, in Korean.
- On the Korean disc, Circle confirms and Cross goes back in every menu, the game's own and the ones this port adds, as on the Japanese disc. The Korean disc always runs at 60 Hz, and its Options have no Video mode, Language or Circle goes back rows.
- The Menu language row also offers Korean (한국어). On Auto it shows Korean with the Korean disc.
- The Korean disc keeps its own game data, saves, texture packs and model packs, like the other discs. Packs for the Korean disc go in `textures/SCPS-56001/replacements` and `models/SCPS-56001/replacements`; a pack in `textures/replacements` or `models/replacements` shows on every disc. `disc` in the `[game]` part of `config.toml` now accepts `kor` as well as `auto`, `pal` and `jpn`.
- Achievements, the music and model galleries, photo mode and the model viewer work with the Korean disc.
