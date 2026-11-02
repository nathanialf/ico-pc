/*
 * port/game/subtitles_ru_bobttz.c
 *
 * The subtitles in Russian, translated by bob-ttz (GitHub issue 132): one
 * entry per record of the English subtitle files (text/data_EG01.jim,
 * data_EG02.jim after a cleared game) the translation covers, its words
 * with the lines split at '\n' (two at most), drawn as text in place of
 * the record's picture (subtitles.h).  A second-play entry is Yorda's
 * words, which the first play shows in her own script: the record's
 * picture then stays.
 */
#include "subtitles.h"

const IcoSubtitle ico_subtitles_ru_bobttz[] = {
    /* 000 Opening: Get the sword. */
    {0, 0, "Принеси меч."},
    /* 001 Opening (Villager): Do not be angry with us. / This is for the good of the village. */
    {1, 0, "Не держи на нас зла.\nМы делаем это на благо деревни."},
    /* 002 Meeting Yorda (the cage) (Ico): Is anybody there? Who are you? */
    {2, 0, "Кто здесь? Здесь есть кто-нибудь?"},
    /* 004 Meeting Yorda (the cage) (Ico): What are you doing in there? */
    {4, 0, "Что ты там делаешь?"},
    /* 005 Meeting Yorda (the cage) (Ico): Hold on. I will get you down. */
    {5, 0, "Подожди минутку.\nСейчас я тебя спущу."},
    /* 006 Meeting Yorda (the cage) (Ico): They... They tried to sacrifice me / because I have horns. */
    {6, 0, "Я... Меня принесли в жертву.\nПотому что у меня выросли рога."},
    /* 007 Meeting Yorda (the cage) (Ico): Kids with horns are brought here. */
    {7, 0, "Детей, у которых вырастают рога, приводят сюда."},
    /* 008 Meeting Yorda (the cage) (Ico): Were they trying to / sacrifice you too? */
    {8, 0, "Тебя тоже принесли в жертву?"},
    /* 009 Meeting Yorda (the cage) (Yorda): Who are you? / How did you get in here? */
    {9, 1, "Кто ты?\nКак ты здесь оказался?"},
    /* 011 Meeting Yorda (the cage) (Ico): What was that creature / that came after you? */
    {11, 0, "Что это сейчас было?\nОно пришло за тобой?"},
    /* 012 Meeting Yorda (the cage) (Ico): It's too dangerous for us / to be here! */
    {12, 0, "Тебе тоже опасно здесь оставаться."},
    /* 013 Meeting Yorda (the cage) (Ico): We need to get out of here. */
    {13, 0, "В любом случае, давай выбираться отсюда!"},
    /* 019 Meeting Yorda (the cage) (Ico): How did you do that? */
    {19, 0, "Как ты это сделала?"},
    /* 025 The castle gate (Ico): Look, the gate is open! / Now we can get out of here! */
    {25, 0, "Смотри! Ворота открыты!\nМы сможем выйти наружу!"},
    /* 026 The castle gate (Ico): Let's go! */
    {26, 0, "Бежим!"},
    /* 027 The castle gate (Ico): Are you okay? */
    {27, 0, "Ты в порядке?"},
    /* 029 The castle gate (Yorda): I have angered her ... */
    {29, 1, "Я рассердила её\nсвоим уходом…"},
    /* 031 The castle gate (Queen): Come back, Yorda. */
    {31, 1, "Тебе пора возвращаться домой, Йорда."},
    /* 034 The castle gate (Queen): So, you're the one aimlessly / leading my Yorda around. */
    {34, 0, "Так значит это ты?\nТот, кто таскает мою маленькую Йорду повсюду."},
    /* 035 The castle gate (Queen): Do you know who this girl is? */
    {35, 0, "Ты хоть представляешь кто она?"},
    /* 036 The castle gate (Queen): That girl you're with is my one / and only beloved daughter. */
    {36, 0, "Девочка, которая стоит рядом с тобой,\n— моя любимая и единственная дочь."},
    /* 037 The castle gate (Queen): Stop wasting your time with her. */
    {37, 0, "Однажды она унаследует этот замок."},
    /* 038 The castle gate (Queen): She lives in a different world / than some boy with horns! */
    {38, 0, "Ты и моя дочь принадлежите\nразным мирам, мальчик с рогами."},
    /* 043 The castle gate (Queen): Now, know your place / and leave here. */
    {43, 0, "Так что знай своё место\nи уходи отсюда"},
    /* 048 The castle gate (Queen): Yorda, why can't you understand? */
    {48, 1, "Как до тебя не дойдёт?"},
    /* 049 The castle gate (Queen): You cannot survive in the / outside world. */
    {49, 1, "Ты не выживешь\nво внешнем мире."},
    /* 086 The castle gate (Yorda): A little more. */
    {86, 1, "Ещё немного…"},
    /* 088 The castle gate (Ico): Are you okay? */
    {88, 0, "Ты в порядке?"},
    /* 091 The castle gate (Yorda): Thank you ... */
    {91, 1, "Спасибо…"},
    /* 093 The Queen's throne room: Wait. */
    {93, 0, "Остановись!"},
    /* 096 The Queen's throne room (Ico): What did you do to her? */
    {96, 0, "Что ты собираешься делать с Йордой?"},
    /* 097 The Queen's throne room (Queen): Silence boy. You're too late. */
    {97, 0, "Сдавайся! Уже слишком поздно…"},
    /* 098 The Queen's throne room (Queen): My body has become too old / and won't last much longer. */
    {98, 0, "Моему постаревшему телу\nтоже осталось недолго…"},
    /* 099 The Queen's throne room (Queen): But Yorda is going to grant me / the power to be resurrected. */
    {99, 0,
     "Я свяжу свою волю с Йордой и использую её\nдля перерождения в качестве новой владычицы замка."},
    /* 100 The Queen's throne room (Queen): To be my spiritual vessel is the / fulfillment of her destiny! */
    {100, 0, "Такова её судьба. Проще говоря,\nЙорда — лишь сосуд для моей души."},
    /* 104 The Queen's throne room (Queen): The next time her body wakes, / Yorda will be no more. */
    {104, 0, "Когда она очнётся в следующий раз,\nто даже не вспомнит о тебе."},
    /* 105 The Queen's throne room (Queen): Now put down the sword / and leave. */
    {105, 0, "А теперь…\nбросай меч и уходи отсюда."},
    /* 106 The Queen's throne room (Queen): That is what she would / want you to do. */
    {106, 0, "Йорда и сама тоже этого хотела от тебя."},
    /* 109 The Queen's throne room (Queen): You're a nuisance, boy. / Do you want to die that much? */
    {109, 0, "Глупый ребёнок.\nТы так сильно жаждешь смерти?"},
    /* 110 The Queen's throne room (Queen): Yorda will never be able to / escape this castle ... */
    {110, 0, "Девочка никогда не сможет\nпокинуть этот замок."},
    /* 111 The Queen's throne room (Queen): Even if you take ... my life ... */
    {111, 0, "Даже если ты…\nоборвёшь мою... жизнь..."},
    /* 112 Ending (Yorda): Good-bye. */
    {112, 1, "Прощай."},
};
const int ico_subtitles_ru_bobttz_count =
    (int)(sizeof(ico_subtitles_ru_bobttz) / sizeof(ico_subtitles_ru_bobttz[0]));
