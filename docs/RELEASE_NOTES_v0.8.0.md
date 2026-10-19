**Please note:** this release adds the Japanese disc, and the Japanese disc has had only limited testing: one computer, one phone and automated checks. It may look or play wrong in places the European disc does not. If it does, keep the previous version and send the game's report file (the newest file in the `logs` folder that starts with `report-`, or `ico-pc.log`) with a short description of what you saw. Every report must say which disc it was made with, the European disc (SCES-50760) or the Japanese disc (SCPS-11003).

## New

- The Japanese disc of ICO (SCPS-11003) plays too, from the same download on Windows, Linux, the Steam Deck, Android, iPhone and Mac. At the first start, choose your disc image of the European disc (SCES-50760) or the Japanese disc, as a .iso, .chd, .bin or .cue file ([the two discs](https://github.com/nathanialf/ico-pc#the-two-discs)).
- On the Japanese disc, Circle confirms and Cross goes back in every menu, the game's own and the ones this port adds, as the Japanese disc did on the PlayStation 2. The Japanese disc always runs at 60 Hz, and its Options have no Video mode, Language or Circle goes back rows.
- A Menu language row (Options > Language) shows the port's own menus in English, French, German, Italian, Spanish or Japanese. It starts on Auto: Japanese with the Japanese disc, and with the European disc the game's language. The game's own signs, menus and subtitles are the disc's.
- Each disc keeps its own game data, saves, texture packs and model packs, so you can set up both on the same device. Packs for the Japanese disc go in `textures/SCPS-11003/replacements` and `models/SCPS-11003/replacements`. If both discs are set up, `disc` in the `[game]` part of `config.toml` (`auto`, `pal` or `jpn`) says which one starts.
- Achievements, the music and model galleries, photo mode and the model viewer work with the Japanese disc.
- The first lines of the log now name the disc the game is playing.
