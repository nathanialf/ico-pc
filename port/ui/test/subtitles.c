/*
 * port/ui/test/subtitles.c
 *
 * The subtitle texts (subtitles.h; docs/port/UI.md, "Subtitles"), UTF-8:
 * test data.  The game draws its subtitles as the pictures they are; these
 * words are part of the font coverage corpus (font_coverage_test.c), the
 * characters the five languages need.
 *
 * Transcribed by eye from the pictures of DATA.DF's ten data_<LL><SS>.jim
 * files, decoded by tools/tm2_sheets.py from the user's disc (kept outside
 * the repository), and re-read against them block by block.  The wording,
 * capitalisation, punctuation, spacing (the English and French space before
 * "?" and "!", a doubled space where the sheet has one) and line breaks are
 * the sheets'; "..." is three full stops.  Italian block 6 keeps the
 * sheet's "hahanno" (the picture overprints "ha" and "hanno").  A line's
 * centre (x) is the middle of its ink on the strip; the faces' capital
 * heights and slot middles are the means over every line that starts with
 * a flat-topped capital.  Nothing else of the disc is here.
 */
#include "subtitles.h"

#include <stddef.h>

/* {block, {x line 1, x line 2}, text} */
/* clang-format off */
/* EG01: data_EG01.jim */
static const UiSubtitle s_en01[] = {
    {0, {255.5f, 0.0f}, "Get the sword."},
    {1, {256.5f, 256.0f}, "Do not be angry with us.\nThis is for the good of the village."},
    {2, {256.0f, 0.0f}, "Is anybody there ?  Who are you ?"},
    {4, {256.5f, 0.0f}, "What are you doing in there ?"},
    {5, {255.5f, 0.0f}, "Hold on. I will get you down."},
    {6, {255.0f, 255.0f}, "They... They tried to sacrifice me\nbecause I have horns."},
    {7, {255.5f, 0.0f}, "Kids with horns are brought here."},
    {8, {256.0f, 255.5f}, "Were they trying to\nsacrifice you too ?"},
    {10, {259.0f, 0.0f}, "? ?"},
    {11, {256.0f, 255.0f}, "What was that creature\nthat came after you ?"},
    {12, {255.0f, 254.0f}, "It's too dangerous for us\nto be here !"},
    {13, {255.5f, 0.0f}, "We need to get out of here."},
    {19, {255.5f, 0.0f}, "How did you do that ?"},
    {25, {254.5f, 255.0f}, "Look, the gate is open !\nNow we can get out of here !"},
    {26, {255.5f, 0.0f}, "Let's go !"},
    {27, {256.0f, 0.0f}, "Are you okay ?"},
    {34, {256.0f, 254.5f}, "So, you're the one aimlessly\nleading my Yorda around."},
    {35, {253.5f, 0.0f}, "Do you know who this girl is ?"},
    {36, {256.5f, 255.5f}, "That girl you're with is my one\nand only beloved daughter."},
    {37, {256.5f, 0.0f}, "Stop wasting your time with her."},
    {38, {255.0f, 253.5f}, "She lives in a different world\nthan some boy with horns !"},
    {43, {255.5f, 254.0f}, "Now, know your place\nand leave here."},
    {88, {256.0f, 0.0f}, "Are you okay ?"},
    {93, {255.5f, 0.0f}, "Wait."},
    {96, {256.5f, 0.0f}, "What did you do to her ?"},
    {97, {255.5f, 0.0f}, "Silence boy.  You're too late."},
    {98, {255.5f, 254.5f}, "My body has become too old\nand won't last much longer."},
    {99, {256.0f, 255.0f}, "But Yorda is going to grant me\nthe power to be resurrected."},
    {100, {255.5f, 255.5f}, "To be my spiritual vessel is the\nfulfillment of her destiny !"},
    {104, {255.5f, 255.5f}, "The next time her body wakes,\nYorda will be no more."},
    {105, {255.0f, 254.5f}, "Now put down the sword\nand leave."},
    {106, {255.5f, 257.0f}, "That is what she would\nwant you to do."},
    {109, {254.0f, 256.0f}, "You're a nuisance, boy.\nDo you want to die that much ?"},
    {110, {255.0f, 255.5f}, "Yorda will never be able to\nescape this castle ..."},
    {111, {255.5f, 0.0f}, "Even if you take ...  my life ..."},
};

/* EG02: data_EG02.jim */
static const UiSubtitle s_en02[] = {
    {0, {255.5f, 0.0f}, "Get the sword."},
    {1, {256.5f, 256.0f}, "Do not be angry with us.\nThis is for the good of the village."},
    {2, {256.0f, 0.0f}, "Is anybody there ?  Who are you ?"},
    {4, {256.5f, 0.0f}, "What are you doing in there ?"},
    {5, {255.5f, 0.0f}, "Hold on. I will get you down."},
    {6, {255.0f, 255.0f}, "They... They tried to sacrifice me\nbecause I have horns."},
    {7, {255.5f, 0.0f}, "Kids with horns are brought here."},
    {8, {256.0f, 255.5f}, "Were they trying to\nsacrifice you too ?"},
    {9, {255.5f, 256.0f}, "Who are you ?\nHow did you get in here ?"},
    {10, {259.0f, 0.0f}, "? ?"},
    {11, {256.0f, 255.0f}, "What was that creature\nthat came after you ?"},
    {12, {255.0f, 254.0f}, "It's too dangerous for us\nto be here !"},
    {13, {255.5f, 0.0f}, "We need to get out of here."},
    {19, {255.5f, 0.0f}, "How did you do that ?"},
    {25, {254.5f, 255.0f}, "Look, the gate is open !\nNow we can get out of here !"},
    {26, {255.5f, 0.0f}, "Let's go !"},
    {27, {256.0f, 0.0f}, "Are you okay ?"},
    {29, {255.0f, 0.0f}, "I have angered her ..."},
    {31, {255.5f, 0.0f}, "Come back, Yorda."},
    {34, {256.0f, 254.5f}, "So, you're the one aimlessly\nleading my Yorda around."},
    {35, {253.5f, 0.0f}, "Do you know who this girl is ?"},
    {36, {256.5f, 255.5f}, "That girl you're with is my one\nand only beloved daughter."},
    {37, {256.5f, 0.0f}, "Stop wasting your time with her."},
    {38, {255.0f, 253.5f}, "She lives in a different world\nthan some boy with horns !"},
    {43, {255.5f, 254.0f}, "Now, know your place\nand leave here."},
    {48, {255.5f, 0.0f}, "Yorda, why can't you understand ?"},
    {49, {255.5f, 255.5f}, "You cannot survive in the\noutside world."},
    {86, {255.5f, 0.0f}, "A little more."},
    {88, {256.0f, 0.0f}, "Are you okay ?"},
    {91, {255.5f, 0.0f}, "Thank you ..."},
    {93, {255.5f, 0.0f}, "Wait."},
    {96, {256.5f, 0.0f}, "What did you do to her ?"},
    {97, {255.5f, 0.0f}, "Silence boy.  You're too late."},
    {98, {255.5f, 254.5f}, "My body has become too old\nand won't last much longer."},
    {99, {256.0f, 255.0f}, "But Yorda is going to grant me\nthe power to be resurrected."},
    {100, {255.5f, 255.5f}, "To be my spiritual vessel is the\nfulfillment of her destiny !"},
    {104, {255.5f, 255.5f}, "The next time her body wakes,\nYorda will be no more."},
    {105, {255.0f, 254.5f}, "Now put down the sword\nand leave."},
    {106, {255.5f, 257.0f}, "That is what she would\nwant you to do."},
    {109, {254.0f, 256.0f}, "You're a nuisance, boy.\nDo you want to die that much ?"},
    {110, {255.0f, 255.5f}, "Yorda will never be able to\nescape this castle ..."},
    {111, {255.5f, 0.0f}, "Even if you take ...  my life ..."},
    {112, {255.5f, 0.0f}, "Good-bye."},
};

/* FR01: data_FR01.jim */
static const UiSubtitle s_fr01[] = {
    {0, {256.0f, 0.0f}, "Prenez l'épée."},
    {1, {256.5f, 256.0f}, "Ne nous en veuillez pas.\nC'est pour le bien du village."},
    {2, {257.0f, 0.0f}, "Il y a quelqu'un ? Qui êtes-vous ?"},
    {4, {257.0f, 0.0f}, "Que faites-vous là ?"},
    {5, {256.0f, 0.0f}, "Tenez bon. Je vais vous descendre."},
    {6, {257.0f, 255.5f}, "Ils... Ils ont essayé de me sacrifier\nparce que j'ai des cornes."},
    {7, {256.0f, 0.0f}, "Les enfants à cornes sont emmenés ici."},
    {8, {257.0f, 257.0f}, "Voulaient-ils vous\nsacrifier, vous aussi ?"},
    {10, {259.0f, 0.0f}, "? ?"},
    {11, {256.0f, 257.0f}, "Quelle était cette créature\nqui vous suivait ?"},
    {12, {256.5f, 272.5f}, "Cet endroit est trop dangereux\npour nous !"},
    {13, {256.0f, 0.0f}, "Il faut sortir d'ici."},
    {19, {257.0f, 0.0f}, "Comment avez-vous fait ça ?"},
    {25, {256.5f, 256.0f}, "Regardez, la porte est ouverte !\nMaintenant, on peut sortir d'ici !"},
    {26, {256.0f, 0.0f}, "Allons-y !"},
    {27, {257.0f, 0.0f}, "Ça va ?"},
    {34, {256.0f, 256.0f}, "Alors, c'est  vous qui avez\nleurré ma Yorda."},
    {35, {257.5f, 0.0f}, "Vous connaissez cette fille ?"},
    {36, {256.5f, 256.0f}, "La fille qui vous accompagne est\nma seule et unique fille adorée."},
    {37, {256.0f, 0.0f}, "Vous perdez votre temps avec elle."},
    {38, {256.5f, 256.5f}, "Elle vit dans un autre monde\nque celui des garçons à cornes !"},
    {43, {256.5f, 245.5f}, "Bon, gardez vos distances\net partez."},
    {88, {257.0f, 0.0f}, "Ça va ?"},
    {93, {255.5f, 0.0f}, "Attendez."},
    {96, {257.5f, 0.0f}, "Que lui avez-vous fait ?"},
    {97, {256.5f, 0.0f}, "Taisez-vous. Vous arrivez trop tard."},
    {98, {255.5f, 256.0f}, "Mon corps est trop vieux,\nil ne résistera plus très longtemps."},
    {99, {256.5f, 256.0f}, "Mais Yorda va me conférer\nle pouvoir de la résurrection."},
    {100, {256.5f, 256.0f}, "Être mon vaisseau spirituel, voilà\nsa destinée !"},
    {104, {256.5f, 250.5f}, "Lorsque son corps se réveillera,\nYorda ne sera plus."},
    {105, {256.0f, 287.0f}, "Maintenant, rangez cette épée\net partez."},
    {106, {256.5f, 255.5f}, "C'est ce qu'elle attendrait\nde vous."},
    {109, {256.5f, 257.0f}, "Quelle plaie ce garçon !\nVous tenez vraiment à mourir ?"},
    {110, {257.0f, 255.5f}, "Yorda ne pourra jamais\ns'enfuir de ce château..."},
    {111, {255.0f, 0.0f}, "Même si vous m'ôtez... la vie..."},
};

/* FR02: data_FR02.jim */
static const UiSubtitle s_fr02[] = {
    {0, {256.0f, 0.0f}, "Prenez l'épée."},
    {1, {256.5f, 256.0f}, "Ne nous en veuillez pas.\nC'est pour le bien du village."},
    {2, {257.0f, 0.0f}, "Il y a quelqu'un ? Qui êtes-vous ?"},
    {4, {257.0f, 0.0f}, "Que faites-vous là ?"},
    {5, {256.0f, 0.0f}, "Tenez bon. Je vais vous descendre."},
    {6, {257.0f, 255.5f}, "Ils... Ils ont essayé de me sacrifier\nparce que j'ai des cornes."},
    {7, {256.0f, 0.0f}, "Les enfants à cornes sont emmenés ici."},
    {8, {257.0f, 257.0f}, "Voulaient-ils vous\nsacrifier, vous aussi ?"},
    {9, {257.0f, 257.0f}, "Qui êtes-vous ?\nComment êtes-vous entré ici ?"},
    {10, {259.0f, 0.0f}, "? ?"},
    {11, {256.0f, 257.0f}, "Quelle était cette créature\nqui vous suivait ?"},
    {12, {256.5f, 272.5f}, "Cet endroit est trop dangereux\npour nous !"},
    {13, {256.0f, 0.0f}, "Il faut sortir d'ici."},
    {19, {257.0f, 0.0f}, "Comment avez-vous fait ça ?"},
    {25, {256.5f, 256.0f}, "Regardez, la porte est ouverte !\nMaintenant, on peut sortir d'ici !"},
    {26, {256.0f, 0.0f}, "Allons-y !"},
    {27, {257.0f, 0.0f}, "Ça va ?"},
    {29, {254.5f, 0.0f}, "Je l'ai mise en colère..."},
    {31, {255.5f, 0.0f}, "Revenez, Yorda."},
    {34, {256.0f, 256.0f}, "Alors, c'est  vous qui avez\nleurré ma Yorda."},
    {35, {257.5f, 0.0f}, "Vous connaissez cette fille ?"},
    {36, {256.5f, 256.0f}, "La fille qui vous accompagne est\nma seule et unique fille adorée."},
    {37, {256.0f, 0.0f}, "Vous perdez votre temps avec elle."},
    {38, {256.5f, 256.5f}, "Elle vit dans un autre monde\nque celui des garçons à cornes !"},
    {43, {256.5f, 245.5f}, "Bon, gardez vos distances\net partez."},
    {48, {257.5f, 0.0f}, "Yorda, vous ne comprenez donc pas ?"},
    {49, {256.5f, 252.0f}, "Vous ne pouvez pas survivre dans le\nmonde extérieur."},
    {86, {256.0f, 0.0f}, "Un peu mieux."},
    {88, {257.0f, 0.0f}, "Ça va ?"},
    {93, {255.5f, 0.0f}, "Attendez."},
    {96, {257.5f, 0.0f}, "Que lui avez-vous fait ?"},
    {97, {256.5f, 0.0f}, "Taisez-vous. Vous arrivez trop tard."},
    {98, {255.5f, 256.0f}, "Mon corps est trop vieux,\nil ne résistera plus très longtemps."},
    {99, {256.5f, 256.0f}, "Mais Yorda va me conférer\nle pouvoir de la résurrection."},
    {100, {256.5f, 256.0f}, "Être mon vaisseau spirituel, voilà\nsa destinée !"},
    {104, {256.5f, 250.5f}, "Lorsque son corps se réveillera,\nYorda ne sera plus."},
    {105, {256.0f, 287.0f}, "Maintenant, rangez cette épée\net partez."},
    {106, {256.5f, 255.5f}, "C'est ce qu'elle attendrait\nde vous."},
    {109, {256.5f, 257.0f}, "Quelle plaie ce garçon !\nVous tenez vraiment à mourir ?"},
    {110, {257.0f, 255.5f}, "Yorda ne pourra jamais\ns'enfuir de ce château..."},
    {111, {255.0f, 0.0f}, "Même si vous m'ôtez... la vie..."},
    {112, {255.0f, 0.0f}, "Au revoir."},
};

/* GR01: data_GR01.jim */
static const UiSubtitle s_de01[] = {
    {0, {255.5f, 0.0f}, "Hol das Schwert!"},
    {1, {256.5f, 257.0f}, "Sei nicht sauer auf uns.\nDas ist nur zum Wohle des Dorfes."},
    {2, {257.5f, 0.0f}, "Ist hier jemand? Wer bist du?"},
    {4, {257.0f, 0.0f}, "Was machst du da drinnen?"},
    {5, {256.5f, 0.0f}, "Warte. Ich bringe dich nach unten."},
    {6, {256.5f, 255.5f}, "Sie ... sie wollten mich opfern,\nweil ich Hörner habe."},
    {7, {256.0f, 256.0f}, "Kinder mit Hörnern\nbringt man  hierher."},
    {8, {256.0f, 0.0f}, "Wollten sie dich auch opfern?"},
    {10, {259.0f, 0.0f}, "? ?"},
    {11, {256.5f, 257.5f}, "Was war das für eine Kreatur,\ndie dich verfolgte?"},
    {12, {257.5f, 257.0f}, "Hier ist es für uns\nzu gefährlich!"},
    {13, {255.5f, 0.0f}, "Wir müssen hier heraus!"},
    {19, {256.5f, 0.0f}, "Wie hast du das gemacht?"},
    {25, {257.0f, 256.0f}, "Sieh, das Tor ist offen!\nJetzt kommen wir hier heraus!"},
    {26, {257.0f, 0.0f}, "Lass uns gehen!"},
    {27, {256.5f, 0.0f}, "Bist du in Ordnung?"},
    {34, {256.5f, 255.0f}, "So, du bist der, der meine\nYorda ziellos herumführt."},
    {35, {257.0f, 0.0f}, "Weißt du, wer das Mädchen ist?"},
    {36, {256.0f, 255.5f}, "Deine kleine Freundin ist meine\neinzige, geliebte Tochter."},
    {37, {257.0f, 0.0f}, "Verschwende keine Zeit mit ihr."},
    {38, {256.5f, 255.5f}, "Sie lebt in einer anderen Welt\nals Jungen mit Hörnern!"},
    {43, {257.0f, 256.5f}, "Nun. Sieh das ein und\nverschwinde von hier."},
    {88, {257.5f, 0.0f}, "Bist du in Ordnung?"},
    {93, {255.0f, 0.0f}, "Warte."},
    {96, {256.0f, 0.0f}, "Was hast du ihr angetan?"},
    {97, {256.5f, 0.0f}, "Sei still, Junge. Du bist zu spät."},
    {98, {256.0f, 255.0f}, "Mein Körper ist zu alt und\nwird nicht mehr lange leben."},
    {99, {256.0f, 256.0f}, "Aber Yorda verleiht mir die Macht,\nwieder aufzuerstehen."},
    {100, {257.0f, 257.0f}, "Ein Körper für meinen Geist zu\nsein, ist ihre Bestimmung!"},
    {104, {256.5f, 256.5f}, "Wenn sie das nächste Mal erwacht,\nwird Yorda nicht mehr sein."},
    {105, {257.0f, 256.0f}, "Nun leg das Schwert nieder\nund geh."},
    {106, {257.0f, 257.5f}, "Das wäre auch\nihr Wunsch."},
    {109, {256.5f, 257.0f}, "Du nervst, Junge.\nWillst du unbedingt sterben?"},
    {110, {256.0f, 256.0f}, "Yorda kann niemals aus diesem\nSchloss fliehen ..."},
    {111, {256.5f, 0.0f}, "Selbst, wenn du mich tötest."},
};

/* GR02: data_GR02.jim */
static const UiSubtitle s_de02[] = {
    {0, {255.5f, 0.0f}, "Hol das Schwert!"},
    {1, {256.5f, 257.0f}, "Sei nicht sauer auf uns.\nDas ist nur zum Wohle des Dorfes."},
    {2, {257.5f, 0.0f}, "Ist hier jemand? Wer bist du?"},
    {4, {257.0f, 0.0f}, "Was machst du da drinnen?"},
    {5, {256.5f, 0.0f}, "Warte. Ich bringe dich nach unten."},
    {6, {256.5f, 255.5f}, "Sie ... sie wollten mich opfern,\nweil ich Hörner habe."},
    {7, {256.0f, 256.0f}, "Kinder mit Hörnern\nbringt man  hierher."},
    {8, {256.0f, 0.0f}, "Wollten sie dich auch opfern?"},
    {9, {257.5f, 257.5f}, "Wer bist du?\nWie bist du hier reingekommen?"},
    {10, {259.0f, 0.0f}, "? ?"},
    {11, {256.5f, 257.5f}, "Was war das für eine Kreatur,\ndie dich verfolgte?"},
    {12, {257.5f, 257.0f}, "Hier ist es für uns\nzu gefährlich!"},
    {13, {255.5f, 0.0f}, "Wir müssen hier heraus!"},
    {19, {256.5f, 0.0f}, "Wie hast du das gemacht?"},
    {25, {257.0f, 256.0f}, "Sieh, das Tor ist offen!\nJetzt kommen wir hier heraus!"},
    {26, {257.0f, 0.0f}, "Lass uns gehen!"},
    {27, {256.5f, 0.0f}, "Bist du in Ordnung?"},
    {29, {257.0f, 0.0f}, "Ich habe sie verärgert ..."},
    {31, {255.5f, 0.0f}, "Komm zurück, Yorda!"},
    {34, {256.5f, 255.0f}, "So, du bist der, der meine\nYorda ziellos herumführt."},
    {35, {257.0f, 0.0f}, "Weißt du, wer das Mädchen ist?"},
    {36, {256.0f, 255.5f}, "Deine kleine Freundin ist meine\neinzige, geliebte Tochter."},
    {37, {257.0f, 0.0f}, "Verschwende keine Zeit mit ihr."},
    {38, {256.5f, 255.5f}, "Sie lebt in einer anderen Welt\nals Jungen mit Hörnern!"},
    {43, {257.0f, 256.5f}, "Nun. Sieh das ein und\nverschwinde von hier."},
    {48, {257.5f, 0.0f}, "Yorda, warum verstehst du nicht?"},
    {49, {256.5f, 256.0f}, "Du kannst in der Außenwelt\nnicht überleben."},
    {86, {257.5f, 0.0f}, "Ein wenig mehr."},
    {88, {257.5f, 0.0f}, "Bist du in Ordnung?"},
    {91, {255.0f, 0.0f}, "Vielen Dank."},
    {93, {255.0f, 0.0f}, "Warte."},
    {96, {256.0f, 0.0f}, "Was hast du ihr angetan?"},
    {97, {256.5f, 0.0f}, "Sei still, Junge. Du bist zu spät."},
    {98, {256.0f, 255.0f}, "Mein Körper ist zu alt und\nwird nicht mehr lange leben."},
    {99, {256.0f, 256.0f}, "Aber Yorda verleiht mir die Macht,\nwieder aufzuerstehen."},
    {100, {257.0f, 257.0f}, "Ein Körper für meinen Geist zu\nsein, ist ihre Bestimmung!"},
    {104, {256.5f, 256.5f}, "Wenn sie das nächste Mal erwacht,\nwird Yorda nicht mehr sein."},
    {105, {257.0f, 256.0f}, "Nun leg das Schwert nieder\nund geh."},
    {106, {257.0f, 257.5f}, "Das wäre auch\nihr Wunsch."},
    {109, {256.5f, 257.0f}, "Du nervst, Junge.\nWillst du unbedingt sterben?"},
    {110, {256.0f, 256.0f}, "Yorda kann niemals aus diesem\nSchloss fliehen ..."},
    {111, {256.5f, 0.0f}, "Selbst, wenn du mich tötest."},
    {112, {255.5f, 0.0f}, "Auf Wiedersehen."},
};

/* IT01: data_IT01.jim */
static const UiSubtitle s_it01[] = {
    {0, {256.0f, 0.0f}, "Prendi la spada"},
    {1, {256.0f, 256.5f}, "Non essere arrabbiato con noi.\nÈ per il bene del villaggio."},
    {2, {257.0f, 0.0f}, "C'è nessuno? Tu chi sei?"},
    {4, {257.0f, 0.0f}, "Cosa stai facendo lì dentro?"},
    {5, {255.0f, 0.0f}, "Aspetta. Ti faccio scendere."},
    {6, {263.5f, 258.5f}, "Loro... Loro hahanno cercato di\nsacrificarmi perché ho le corna."},
    {7, {256.0f, 0.0f}, "I bambini con le corna vengono portati qui."},
    {8, {256.0f, 257.0f}, "Stavano cercando di\nsacrificare anche te?"},
    {10, {259.0f, 0.0f}, "? ?"},
    {11, {256.5f, 257.0f}, "Cos'era quella creatura\nche ti stava cercando?"},
    {12, {256.5f, 256.5f}, "Per noi è troppo pericoloso\nrestare qui!"},
    {13, {255.5f, 0.0f}, "Dobbiamo uscire di qui."},
    {19, {257.0f, 0.0f}, "Come ci sei riuscita?"},
    {25, {256.5f, 256.5f}, "Guarda, il cancello è aperto!\nOra possiamo uscire di qui!"},
    {26, {256.0f, 0.0f}, "Andiamo!"},
    {27, {256.5f, 0.0f}, "Stai bene?"},
    {34, {256.5f, 265.0f}, "E così tu saresti quello che porta in giro\nsenza una meta la mia Yorda."},
    {35, {256.5f, 0.0f}, "Sai chi è questa ragazza?"},
    {36, {256.5f, 256.0f}, "Questa ragazza è la mia unica\ne adorata figlia."},
    {37, {255.5f, 0.0f}, "Smettila di sprecare il tuo tempo con lei."},
    {38, {257.0f, 256.5f}, "Yorda vive in un mondo diverso\nda quello dei ragazzi con le corna!"},
    {43, {256.0f, 255.5f}, "Ora sai come stanno le cose...\npuoi andartene."},
    {88, {256.5f, 0.0f}, "Stai bene?"},
    {93, {255.5f, 0.0f}, "Aspetta."},
    {96, {257.0f, 0.0f}, "Cosa le hai fatto?"},
    {97, {256.0f, 0.0f}, "Silenzio ragazzo. Sei in ritardo."},
    {98, {256.0f, 255.5f}, "Il mio corpo è diventato troppo vecchio.\ne non resisterà ancora a lungo."},
    {99, {256.0f, 256.0f}, "Ma Yorda mi garantirà la\nforza per essere riportato in vita."},
    {100, {256.5f, 256.5f}, "Essere il mio vascello spirituale è\nil compimento del suo destino!"},
    {104, {256.0f, 246.5f}, "La prossima volta che si desterà,\nYorda non ci sarà più."},
    {105, {256.5f, 263.5f}, "Ora abbassa la spada\ne vai via."},
    {106, {257.0f, 256.0f}, "Questo è ciò che lei\nvorrebbe tu facessi."},
    {109, {255.5f, 257.5f}, "Sei noioso, ragazzo.\nHai così tanta voglia di morire?"},
    {110, {257.0f, 255.5f}, "Yorda non riuscirà mai a\nscappare da questo castello..."},
    {111, {255.0f, 0.0f}, "Anche se tu mi uccidessi..."},
};

/* IT02: data_IT02.jim */
static const UiSubtitle s_it02[] = {
    {0, {256.0f, 0.0f}, "Prendi la spada"},
    {1, {256.0f, 256.5f}, "Non essere arrabbiato con noi.\nÈ per il bene del villaggio."},
    {2, {257.0f, 0.0f}, "C'è nessuno? Tu chi sei?"},
    {4, {257.0f, 0.0f}, "Cosa stai facendo lì dentro?"},
    {5, {255.0f, 0.0f}, "Aspetta. Ti faccio scendere."},
    {6, {263.5f, 258.5f}, "Loro... Loro hahanno cercato di\nsacrificarmi perché ho le corna."},
    {7, {256.0f, 0.0f}, "I bambini con le corna vengono portati qui."},
    {8, {256.0f, 257.0f}, "Stavano cercando di\nsacrificare anche te?"},
    {9, {257.0f, 257.0f}, "Chi sei?\nCome sei entrato qui dentro?"},
    {10, {259.0f, 0.0f}, "? ?"},
    {11, {256.5f, 257.0f}, "Cos'era quella creatura\nche ti stava cercando?"},
    {12, {256.5f, 256.5f}, "Per noi è troppo pericoloso\nrestare qui!"},
    {13, {255.5f, 0.0f}, "Dobbiamo uscire di qui."},
    {19, {257.0f, 0.0f}, "Come ci sei riuscita?"},
    {25, {256.5f, 256.5f}, "Guarda, il cancello è aperto!\nOra possiamo uscire di qui!"},
    {26, {256.0f, 0.0f}, "Andiamo!"},
    {27, {256.5f, 0.0f}, "Stai bene?"},
    {29, {255.5f, 0.0f}, "L'ho fatta arrabbiare..."},
    {31, {256.5f, 0.0f}, "Torna indietro, Yorda."},
    {34, {256.5f, 265.0f}, "E così tu saresti quello che porta in giro\nsenza una meta la mia Yorda."},
    {35, {256.5f, 0.0f}, "Sai chi è questa ragazza?"},
    {36, {256.5f, 256.0f}, "Questa ragazza è la mia unica\ne adorata figlia."},
    {37, {255.5f, 0.0f}, "Smettila di sprecare il tuo tempo con lei."},
    {38, {257.0f, 256.5f}, "Yorda vive in un mondo diverso\nda quello dei ragazzi con le corna!"},
    {43, {256.0f, 255.5f}, "Ora sai come stanno le cose...\npuoi andartene."},
    {48, {257.5f, 0.0f}, "Yorda, perché non riesci a capire?"},
    {49, {256.5f, 254.0f}, "Non puoi sopravvivere nel\nmondo esterno."},
    {86, {256.0f, 0.0f}, "Un po' di più."},
    {88, {256.5f, 0.0f}, "Stai bene?"},
    {91, {255.5f, 0.0f}, "Grazie..."},
    {93, {255.5f, 0.0f}, "Aspetta."},
    {96, {257.0f, 0.0f}, "Cosa le hai fatto?"},
    {97, {256.0f, 0.0f}, "Silenzio ragazzo. Sei in ritardo."},
    {98, {256.0f, 255.5f}, "Il mio corpo è diventato troppo vecchio.\ne non resisterà ancora a lungo."},
    {99, {256.0f, 256.0f}, "Ma Yorda mi garantirà la\nforza per essere riportato in vita."},
    {100, {256.5f, 256.5f}, "Essere il mio vascello spirituale è\nil compimento del suo destino!"},
    {104, {256.0f, 246.5f}, "La prossima volta che si desterà,\nYorda non ci sarà più."},
    {105, {256.5f, 263.5f}, "Ora abbassa la spada\ne vai via."},
    {106, {257.0f, 256.0f}, "Questo è ciò che lei\nvorrebbe tu facessi."},
    {109, {255.5f, 257.5f}, "Sei noioso, ragazzo.\nHai così tanta voglia di morire?"},
    {110, {257.0f, 255.5f}, "Yorda non riuscirà mai a\nscappare da questo castello..."},
    {111, {255.0f, 0.0f}, "Anche se tu mi uccidessi..."},
    {112, {255.0f, 0.0f}, "Addio."},
};

/* SP01: data_SP01.jim */
static const UiSubtitle s_es01[] = {
    {0, {256.0f, 0.0f}, "Coge la espada."},
    {1, {256.0f, 256.0f}, "No te enfades con nosotros.\nEs por el bien de la aldea."},
    {2, {256.0f, 0.0f}, "¿Hay alguien ahí? ¿Quién eres?"},
    {4, {256.5f, 0.0f}, "¿Qué estás haciendo ahí?"},
    {5, {255.5f, 0.0f}, "Espera. Te bajaré."},
    {6, {256.0f, 255.5f}, "Ellos... Intentaron sacrificarme\nporque tengo cuernos."},
    {7, {256.0f, 0.0f}, "Traen aquí a los niños con cuernos."},
    {8, {255.5f, 234.5f}, "¿Intentaban sacrificarte\na ti también?"},
    {10, {259.0f, 0.0f}, "? ?"},
    {11, {255.5f, 245.5f}, "¿Qué era esa criatura que\nte perseguía?"},
    {12, {256.0f, 242.5f}, "¡Es muy peligroso\nestar aquí!"},
    {13, {256.0f, 0.0f}, "Tenemos que salir de aquí."},
    {19, {256.5f, 0.0f}, "¿Cómo has hecho eso?"},
    {25, {256.0f, 256.0f}, "¡Mira, la puerta está abierta!\n¡Ahora podemos salir de aquí!"},
    {26, {256.0f, 0.0f}, "¡Vamos!"},
    {27, {256.0f, 0.0f}, "¿Estás bien?"},
    {34, {256.0f, 256.0f}, "Así que tú eres quien guía\nsin rumbo fijo a mi Yorda."},
    {35, {256.5f, 0.0f}, "¿Sabes quién es esta chica?"},
    {36, {256.5f, 255.5f}, "Esta chica con la que estás es\nmi amada y única hija."},
    {37, {256.0f, 0.0f}, "Deja de perder el tiempo con ella."},
    {38, {256.5f, 256.5f}, "¡Ella vive en un mundo distinto\nal de algunos chicos con cuernos!"},
    {43, {255.5f, 256.0f}, "Ahora, sigue tu camino\ny vete de aquí."},
    {88, {256.0f, 0.0f}, "¿Estás bien?"},
    {93, {256.0f, 0.0f}, "Espera."},
    {96, {256.0f, 0.0f}, "¿Qué le has hecho?"},
    {97, {255.5f, 0.0f}, "Silencio, chico. Has llegado tarde."},
    {98, {256.0f, 254.0f}, "Mi cuerpo ha envejecido demasiado\ny no durará mucho."},
    {99, {256.5f, 256.0f}, "Pero Yorda va a concederme\nel poder de resucitar."},
    {100, {256.0f, 244.5f}, "¡La consumación de su destino es\nser mi recipiente espiritual!"},
    {104, {256.5f, 256.0f}, "La próxima vez que su cuerpo\ndespierte, ya no será Yorda."},
    {105, {256.0f, 256.0f}, "Ahora baja la\nespada y vete."},
    {106, {256.5f, 256.0f}, "Eso es lo que ella\nquerría que hicieses."},
    {109, {256.5f, 256.5f}, "Eres un incordio, muchacho.\n¿Tantas ganas tienes de morir?"},
    {110, {257.5f, 265.5f}, "Yorda nunca podrá escapar\nde este castillo..."},
    {111, {256.0f, 0.0f}, "Ni siquiera... si me matas..."},
};

/* SP02: data_SP02.jim */
static const UiSubtitle s_es02[] = {
    {0, {256.0f, 0.0f}, "Coge la espada."},
    {1, {256.0f, 256.0f}, "No te enfades con nosotros.\nEs por el bien de la aldea."},
    {2, {256.0f, 0.0f}, "¿Hay alguien ahí? ¿Quién eres?"},
    {4, {256.5f, 0.0f}, "¿Qué estás haciendo ahí?"},
    {5, {255.5f, 0.0f}, "Espera. Te bajaré."},
    {6, {256.0f, 255.5f}, "Ellos... Intentaron sacrificarme\nporque tengo cuernos."},
    {7, {256.0f, 0.0f}, "Traen aquí a los niños con cuernos."},
    {8, {255.5f, 234.5f}, "¿Intentaban sacrificarte\na ti también?"},
    {9, {256.0f, 256.5f}, "¿Quién eres?\n¿Cómo has entrado aquí?"},
    {10, {259.0f, 0.0f}, "? ?"},
    {11, {255.5f, 245.5f}, "¿Qué era esa criatura que\nte perseguía?"},
    {12, {256.0f, 242.5f}, "¡Es muy peligroso\nestar aquí!"},
    {13, {256.0f, 0.0f}, "Tenemos que salir de aquí."},
    {19, {256.5f, 0.0f}, "¿Cómo has hecho eso?"},
    {25, {256.0f, 256.0f}, "¡Mira, la puerta está abierta!\n¡Ahora podemos salir de aquí!"},
    {26, {256.0f, 0.0f}, "¡Vamos!"},
    {27, {256.0f, 0.0f}, "¿Estás bien?"},
    {29, {256.0f, 0.0f}, "La he enojado..."},
    {31, {256.0f, 0.0f}, "Vuelve, Yorda."},
    {34, {256.0f, 256.0f}, "Así que tú eres quien guía\nsin rumbo fijo a mi Yorda."},
    {35, {256.5f, 0.0f}, "¿Sabes quién es esta chica?"},
    {36, {256.5f, 255.5f}, "Esta chica con la que estás es\nmi amada y única hija."},
    {37, {256.0f, 0.0f}, "Deja de perder el tiempo con ella."},
    {38, {256.5f, 256.5f}, "¡Ella vive en un mundo distinto\nal de algunos chicos con cuernos!"},
    {43, {255.5f, 256.0f}, "Ahora, sigue tu camino\ny vete de aquí."},
    {48, {257.5f, 0.0f}, "Yorda, ¿por qué no lo entiendes?"},
    {49, {257.0f, 249.5f}, "No puedes sobrevivir en el\nmundo exterior."},
    {86, {256.0f, 0.0f}, "Un poco más."},
    {88, {256.0f, 0.0f}, "¿Estás bien?"},
    {91, {256.0f, 0.0f}, "Gracias..."},
    {93, {256.0f, 0.0f}, "Espera."},
    {96, {256.0f, 0.0f}, "¿Qué le has hecho?"},
    {97, {255.5f, 0.0f}, "Silencio, chico. Has llegado tarde."},
    {98, {256.0f, 254.0f}, "Mi cuerpo ha envejecido demasiado\ny no durará mucho."},
    {99, {256.5f, 256.0f}, "Pero Yorda va a concederme\nel poder de resucitar."},
    {100, {256.0f, 244.5f}, "¡La consumación de su destino es\nser mi recipiente espiritual!"},
    {104, {256.5f, 256.0f}, "La próxima vez que su cuerpo\ndespierte, ya no será Yorda."},
    {105, {256.0f, 256.0f}, "Ahora baja la\nespada y vete."},
    {106, {256.5f, 256.0f}, "Eso es lo que ella\nquerría que hicieses."},
    {109, {256.5f, 256.5f}, "Eres un incordio, muchacho.\n¿Tantas ganas tienes de morir?"},
    {110, {257.5f, 265.5f}, "Yorda nunca podrá escapar\nde este castillo..."},
    {111, {256.0f, 0.0f}, "Ni siquiera... si me matas..."},
    {112, {255.0f, 0.0f}, "Adiós."},
};
/* clang-format on */

#define N(a) ((int)(sizeof(a) / sizeof((a)[0])))

static const struct {
    const UiSubtitle *items;
    int count;
} s_sets[UI_LANG_COUNT][2] = {
    {{s_en01, N(s_en01)}, {s_en02, N(s_en02)}}, {{s_fr01, N(s_fr01)}, {s_fr02, N(s_fr02)}},
    {{s_de01, N(s_de01)}, {s_de02, N(s_de02)}}, {{s_it01, N(s_it01)}, {s_it02, N(s_it02)}},
    {{s_es01, N(s_es01)}, {s_es02, N(s_es02)}},
};

/* capital height (texels, at half coverage) and slot middles, measured:
   English 11.73 over 40 lines, upper 12.88 and lower 34.01; German 12.65,
   12.88 and 34.16; French, Italian and Spanish (one face) 12.21, 13.92 and
   33.9 */
static const UiSubtitleFace s_faces[UI_LANG_COUNT] = {
    {16.0f, {12.9f, 34.0f}}, /* EN */
    {16.7f, {13.9f, 33.9f}}, /* FR */
    {17.4f, {12.9f, 34.2f}}, /* DE */
    {16.7f, {13.9f, 33.9f}}, /* IT */
    {16.7f, {13.9f, 33.9f}}, /* ES */
};

const UiSubtitle *ui_SubtitleTable(UiLang lang, int set, int *count)
{
    if ((int)lang < 0 || lang >= UI_LANG_COUNT || set < 0 || set > 1) {
        if (count) {
            *count = 0;
        }
        return NULL;
    }
    if (count) {
        *count = s_sets[lang][set].count;
    }
    return s_sets[lang][set].items;
}

const UiSubtitle *ui_SubtitleFind(UiLang lang, int set, int block)
{
    int n = 0;
    const UiSubtitle *t = ui_SubtitleTable(lang, set, &n);
    int lo = 0, hi = n - 1;
    while (t && lo <= hi) {
        const int mid = (lo + hi) / 2;
        if (t[mid].block == block) {
            return &t[mid];
        }
        if (t[mid].block < block) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return NULL;
}

const UiSubtitleFace *ui_SubtitleFace(UiLang lang)
{
    if ((int)lang < 0 || lang >= UI_LANG_COUNT) {
        lang = UI_LANG_EN;
    }
    return &s_faces[lang];
}
