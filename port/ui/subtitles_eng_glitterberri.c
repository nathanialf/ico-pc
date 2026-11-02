/*
 * port/ui/subtitles_eng_glitterberri.c
 *
 * English text subtitles from https://glitterberri.com/ico/unused-dialogue-and-retranslation/
 */
#include "subtitles.h"

static const UiSubtitleLine kLines[] = {
    {0, "Bring me the sword."},
    {1, "Do not think ill of us.\nThis is all for the good of the village."},
    {2, "Who is it? Is someone there?"},
    {4, "What are you doing up there?"},
    {5, "Hang on a minute.\nI’ll get you down!"},
    {6, "I, I’m a sacrifice.\nBecause I have horns, see?"},
    {7, "Children who grow horns\nget taken to this place."},
    {8, "Are you a sacrifice, too?"},
    {9, "Who are you?\nHow did you get in here?"},
    {11, "What was that just now?\nWas it after you?"},
    {12, "It’s dangerous for you\nto be here, too."},
    {13, "Anyhow, let’s get out of here!"},
    {19, "How did you do that?"},
    {25, "Look! The gate’s open!\nWe can get out!"},
    {26, "Let’s go!"},
    {27, "Are you alright?"},
    {29, "I’ve gone and made\nher angry…"},
    {31, "It’s time for you to come home, Yorda."},
    {34, "So, you’re the one who’s been\nleading my little Yorda around."},
    {35, "Do you have any idea\nwho she is?"},
    {36, "The girl who stands beside you\nis my beloved daughter and sole heir."},
    {37, "Someday, she will inherit this castle."},
    {38, "You and my daughter inhabit\ndifferent worlds, horned boy."},
    {43, "Now, remember your place,\nand begone from here."},
    {48, "Why do you not understand?"},
    {49, "You cannot survive\nin the outside world."},
    {86, "Just a little further…"},
    {88, "Are you alright?"},
    {91, "Thank you…"},
    {93, "Stop right there."},
    {96, "What do you want with Yorda?"},
    {97, "Give up your foolish ambitions. You are already too late…"},
    {98, "This body of mine\nwill not last much longer…"},
    {99, "I shall bind my will to Yorda,\nand use her to reincarnate as master of this castle."},
    {100, "That is the girl’s destiny.\nShe is the vessel for my soul."},
    {104, "The next time she wakes,\nshe will remember nothing of you or your time together."},
    {105, "Now…\nPut down the sword and leave this place."},
    {106, "That is what Yorda wished you to do, too."},
    {109, "Foolish child.\nDo you truly wish for death so badly?"},
    {110, "The girl will never be able to\nleave the castle…"},
    {111, "Even if…\nyou take my life…"},
    {112, "Goodbye…"},
};

const UiSubtitlePack subtitles_eng_glitterberri = {
    "eng_glitterberri",
    "English (GlitterBerri)",
    kLines,
    sizeof(kLines) / sizeof(kLines[0]),
};
