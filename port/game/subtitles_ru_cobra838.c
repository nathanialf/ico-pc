/*
 * port/game/subtitles_ru_cobra838.c
 *
 * The subtitles in Russian, translated by cobra838 (GitHub PR 180's
 * port/ui/subtitles_rus.c): one entry per record of the English subtitle
 * files (text/data_EG01.jim, data_EG02.jim after a cleared game) the
 * translation covers, its words with the lines split at '\n' (two at most),
 * drawn as text in place of the record's picture (subtitles.h).  A
 * second-play entry is Yorda's words, which the first play shows in her own
 * script: the record's picture then stays (the same eight records as
 * subtitles_ru_bobttz.c, the rule his PR's table keeps too).
 */
#include "subtitles.h"

const IcoSubtitle ico_subtitles_ru_cobra838[] = {
    /* 000 Opening: Get the sword. */
    {0, 0, "Принеси меч."},
    /* 001 Opening (Villager): Do not be angry with us. / This is for the good of the village. */
    {1, 0, "Не держи на нас зла.\nТак нужно ради блага деревни."},
    /* 002 Meeting Yorda (the cage) (Ico): Is anybody there? Who are you? */
    {2, 0, "Кто там?.. Там кто-нибудь есть?"},
    /* 004 Meeting Yorda (the cage) (Ico): What are you doing in there? */
    {4, 0, "Что ты там делаешь?"},
    /* 005 Meeting Yorda (the cage) (Ico): Hold on. I will get you down. */
    {5, 0, "Подожди. Я спущу тебя."},
    /* 006 Meeting Yorda (the cage) (Ico): They... They tried to sacrifice me / because I have horns. */
    {6, 0, "Я… Я жертва.\nВидишь мои рога?"},
    /* 007 Meeting Yorda (the cage) (Ico): Kids with horns are brought here. */
    {7, 0, "Всех детей с рогами приводят сюда."},
    /* 008 Meeting Yorda (the cage) (Ico): Were they trying to / sacrifice you too? */
    {8, 0, "А ты тоже жертва?"},
    /* 009 Meeting Yorda (the cage) (Yorda): Who are you? / How did you get in here? */
    {9, 1, "Кто ты?\nКак ты сюда попал?"},
    /* 011 Meeting Yorda (the cage) (Ico): What was that creature / that came after you? */
    {11, 0, "Что это за существо?\nОно пришло за тобой?"},
    /* 012 Meeting Yorda (the cage) (Ico): It's too dangerous for us / to be here! */
    {12, 0, "Нам опасно здесь оставаться."},
    /* 013 Meeting Yorda (the cage) (Ico): We need to get out of here. */
    {13, 0, "Давай выбираться отсюда!"},
    /* 019 Meeting Yorda (the cage) (Ico): How did you do that? */
    {19, 0, "Как ты это сделала?"},
    /* 025 The castle gate (Ico): Look, the gate is open! / Now we can get out of here! */
    {25, 0, "Смотри! Ворота открыты!\nМы можем уходить!"},
    /* 026 The castle gate (Ico): Let's go! */
    {26, 0, "Бежим!"},
    /* 027 The castle gate (Ico): Are you okay? */
    {27, 0, "Ты в порядке?"},
    /* 029 The castle gate (Yorda): I have angered her ... */
    {29, 1, "Я… рассердила ее…"},
    /* 031 The castle gate (Queen): Come back, Yorda. */
    {31, 1, "Ну возвращайся же, Йорда!"},
    /* 034 The castle gate (Queen): So, you're the one aimlessly / leading my Yorda around. */
    {34, 0, "Ах, так вот кто ходит\nс моей милой Йордой."},
    /* 035 The castle gate (Queen): Do you know who this girl is? */
    {35, 0, "А знаешь ли ты, кто она?"},
    /* 036 The castle gate (Queen): That girl you're with is my one / and only beloved daughter. */
    {36, 0, "Эта девочка — моя единственная и любимая дочь."},
    /* 037 The castle gate (Queen): Stop wasting your time with her. */
    {37, 0, "Скоро она унаследует этот замок."},
    /* 038 The castle gate (Queen): She lives in a different world / than some boy with horns! */
    {38, 0, "А ты, мальчик с рогами, принадлежишь к\nсовершенно иному миру."},
    /* 043 The castle gate (Queen): Now, know your place / and leave here. */
    {43, 0, "Так знай же свое место и уходи."},
    /* 048 The castle gate (Queen): Yorda, why can't you understand? */
    {48, 1, "Ну почему ты не поймешь?"},
    /* 049 The castle gate (Queen): You cannot survive in the / outside world. */
    {49, 1, "Ты не сможешь жить\nво внешнем мире."},
    /* 086 The castle gate (Yorda): A little more. */
    {86, 1, "Еще чуть-чуть…"},
    /* 088 The castle gate (Ico): Are you okay? */
    {88, 0, "Ты в порядке?"},
    /* 091 The castle gate (Yorda): Thank you ... */
    {91, 1, "Спасибо… за все…"},
    /* 093 The Queen's throne room: Wait. */
    {93, 0, "Стой!"},
    /* 096 The Queen's throne room (Ico): What did you do to her? */
    {96, 0, "Что ты с ней сделала?"},
    /* 097 The Queen's throne room (Queen): Silence boy. You're too late. */
    {97, 0, "Поздно, мальчик. Ты опоздал."},
    /* 098 The Queen's throne room (Queen): My body has become too old / and won't last much longer. */
    {98, 0, "Мое тело становится старым.\nОно не протянет долго."},
    /* 099 The Queen's throne room (Queen): But Yorda is going to grant me / the power to be resurrected. */
    {99, 0, "Йорда — дар для меня,\nдля моей силы."},
    /* 100 The Queen's throne room (Queen): To be my spiritual vessel is the / fulfillment of her destiny! */
    {100, 0, "Быть спиритическим сосудом —\nвот ее предназначение."},
    /* 104 The Queen's throne room (Queen): The next time her body wakes, / Yorda will be no more. */
    {104, 0, "Когда ее тело проснется,\nЙорды больше не будет."},
    /* 105 The Queen's throne room (Queen): Now put down the sword / and leave. */
    {105, 0, "А теперь… бросай меч и уходи."},
    /* 106 The Queen's throne room (Queen): That is what she would / want you to do. */
    {106, 0, "Она хотела бы, чтобы ты ушел."},
    /* 109 The Queen's throne room (Queen): You're a nuisance, boy. / Do you want to die that much? */
    {109, 0, "Мальчик, ты что,\nхочешь умереть?"},
    /* 110 The Queen's throne room (Queen): Yorda will never be able to / escape this castle ... */
    {110, 0, "Йорда ни за что не сможет\nпокинуть этот замок."},
    /* 111 The Queen's throne room (Queen): Even if you take ... my life ... */
    {111, 0, "Даже если…\nмоя жизнь оборвется…"},
    /* 112 Ending (Yorda): Good-bye. */
    {112, 1, "Прощай."},
};
const int ico_subtitles_ru_cobra838_count =
    (int)(sizeof(ico_subtitles_ru_cobra838) / sizeof(ico_subtitles_ru_cobra838[0]));
