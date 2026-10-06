# Extras

Settings > Extras is the page for what is not part of playing the game: a
music gallery, a model viewer and the credits. It sits in the Settings menu
because the title menu has no room for more rows (docs/port/SETTINGS.md,
"Extras"), and it is there only when Settings was opened from the title: the
galleries take over the stage and the pause menu has no stage to give.

| entry | what it will be | status |
| --- | --- | --- |
| Music | a list of the game's music tracks to listen to | coming in package MUS |
| Models | a viewer for the game's character and object models | coming in package MV |
| Credits | the staff roll, locked until the ending has been reached ("Finish the game to unlock") | coming in package CRED |

Until the packages land, selecting an entry does nothing and writes
`extras: <entry> not available yet` to the log. Credits already has its
locked look: greyed, with the value "Locked" and the note.

The page is built in `port/ui/settings.c` (`UI_PAGE_EXTRAS`, the `extras*`
hooks) on the shared list code in `port/ui/ui_list.c` for the galleries'
lists; docs/port/UI.md, "Settings menu", describes both.
