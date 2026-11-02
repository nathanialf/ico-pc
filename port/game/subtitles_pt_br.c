/*
 * port/game/subtitles_pt_br.c
 *
 * The subtitles in Brazilian Portuguese, translated by Igor Chagas
 * (ichagas7, GitHub issue 157): one entry per record of the English
 * subtitle files (text/data_EG01.jim, data_EG02.jim after a cleared game)
 * the translation covers, its words with the lines split at '\n' (two at
 * most), drawn as text in place of the record's picture (subtitles.h).  A
 * second-play entry is Yorda's words, which the first play shows in her own
 * script: the record's picture then stays.
 */
#include "subtitles.h"

const IcoSubtitle ico_subtitles_pt_br[] = {
    /* 000 Opening: Get the sword. */
    {0, 0, "Pegue a espada."},
    /* 001 Opening (Villager): Do not be angry with us. / This is for the good of the village. */
    {1, 0, "Não fique bravo conosco.\nIsso é pelo bem da vila."},
    /* 002 Meeting Yorda (the cage) (Ico): Is anybody there? Who are you? */
    {2, 0, "Tem alguém aí? Quem é você?"},
    /* 004 Meeting Yorda (the cage) (Ico): What are you doing in there? */
    {4, 0, "O que você está fazendo aí?"},
    /* 005 Meeting Yorda (the cage) (Ico): Hold on. I will get you down. */
    {5, 0, "Espera. Eu vou te tirar daí."},
    /* 006 Meeting Yorda (the cage) (Ico): They... They tried to sacrifice me / because I have horns. */
    {6, 0, "Eu… Eu sou um sacrifício.\nPorque tenho chifres. Tá vendo?"},
    /* 007 Meeting Yorda (the cage) (Ico): Kids with horns are brought here. */
    {7, 0, "Crianças que tem chifres\nsão trazidas pra cá."},
    /* 008 Meeting Yorda (the cage) (Ico): Were they trying to / sacrifice you too? */
    {8, 0, "Você também é\num sacrifício?"},
    /* 009 Meeting Yorda (the cage) (Yorda): Who are you? / How did you get in here? */
    {9, 1, "Quem é você?\nComo entrou aqui?"},
    /* 011 Meeting Yorda (the cage) (Ico): What was that creature / that came after you? */
    {11, 0, "Que criatura era aquela\nque veio atrás de você?"},
    /* 012 Meeting Yorda (the cage) (Ico): It's too dangerous for us / to be here! */
    {12, 0, "É muito perigoso pra você\nficar aqui também!"},
    /* 013 Meeting Yorda (the cage) (Ico): We need to get out of here. */
    {13, 0, "Precisamos sair daqui."},
    /* 019 Meeting Yorda (the cage) (Ico): How did you do that? */
    {19, 0, "Como você fez aquilo?"},
    /* 025 The castle gate (Ico): Look, the gate is open! / Now we can get out of here! */
    {25, 0, "Olha, o portão está aberto!\nPodemos sair!"},
    /* 026 The castle gate (Ico): Let's go! */
    {26, 0, "Vamos!"},
    /* 027 The castle gate (Ico): Are you okay? */
    {27, 0, "Você está bem?"},
    /* 029 The castle gate (Yorda): I have angered her ... */
    {29, 1, "Eu a deixei zangada..."},
    /* 031 The castle gate (Queen): Come back, Yorda. */
    {31, 1, "Venha para casa, Yorda."},
    /* 034 The castle gate (Queen): So, you're the one aimlessly / leading my Yorda around. */
    {34, 0, "Então é você quem anda arrastando\nminha querida Yorda por aí."},
    /* 035 The castle gate (Queen): Do you know who this girl is? */
    {35, 0, "Faz ideia de quem ela é?"},
    /* 036 The castle gate (Queen): That girl you're with is my one / and only beloved daughter. */
    {36, 0, "Ela é minha única e amada filha."},
    /* 037 The castle gate (Queen): Stop wasting your time with her. */
    {37, 0, "Aquela que um dia herdará este castelo."},
    /* 038 The castle gate (Queen): She lives in a different world / than some boy with horns! */
    {38, 0, "Vocês pertencem a dois mundos\ncompletamente diferentes."},
    /* 043 The castle gate (Queen): Now, know your place / and leave here. */
    {43, 0, "Agora se coloque no seu lugar\ne vá embora daqui."},
    /* 048 The castle gate (Queen): Yorda, why can't you understand? */
    {48, 1, "Yorda, por que não\nconsegue entender?"},
    /* 049 The castle gate (Queen): You cannot survive in the / outside world. */
    {49, 1, "Você não sobreviveria\nno mundo lá fora."},
    /* 086 The castle gate (Yorda): A little more. */
    {86, 1, "Só mais um pouco…"},
    /* 088 The castle gate (Ico): Are you okay? */
    {88, 0, "Você está bem?"},
    /* 091 The castle gate (Yorda): Thank you ... */
    {91, 1, "Obrigada…"},
    /* 093 The Queen's throne room: Wait. */
    {93, 0, "Espere."},
    /* 096 The Queen's throne room (Ico): What did you do to her? */
    {96, 0, "O que você fez com ela?"},
    /* 097 The Queen's throne room (Queen): Silence boy. You're too late. */
    {97, 0, "Desista. Já é tarde demais."},
    /* 098 The Queen's throne room (Queen): My body has become too old / and won't last much longer. */
    {98, 0, "Meu corpo não resistirá\npor muito mais tempo."},
    /* 099 The Queen's throne room (Queen): But Yorda is going to grant me / the power to be resurrected. */
    {99, 0, "Yorda herdará minha vontade e renascerá\ncomo a nova senhora deste castelo."},
    /* 100 The Queen's throne room (Queen): To be my spiritual vessel is the / fulfillment of her destiny! */
    {100, 0, "Este é o destino dela.\nSer o receptáculo da minha alma."},
    /* 104 The Queen's throne room (Queen): The next time her body wakes, / Yorda will be no more. */
    {104, 0, "Quando ela acordar,\njá não se lembrará de você."},
    /* 105 The Queen's throne room (Queen): Now put down the sword / and leave. */
    {105, 0, "Agora largue a espada e saia daqui."},
    /* 106 The Queen's throne room (Queen): That is what she would / want you to do. */
    {106, 0, "É o que ela também desejaria…"},
    /* 109 The Queen's throne room (Queen): You're a nuisance, boy. / Do you want to die that much? */
    {109, 0, "Que menino mau…\nQuer tanto assim morrer?"},
    /* 110 The Queen's throne room (Queen): Yorda will never be able to / escape this castle ... */
    {110, 0, "Ela jamais poderá\nsair deste castelo…"},
    /* 111 The Queen's throne room (Queen): Even if you take ... my life ... */
    {111, 0, "Mesmo que você tire... a minha vida..."},
    /* 112 Ending (Yorda): Good-bye. */
    {112, 1, "Adeus."},
};
const int ico_subtitles_pt_br_count =
    (int)(sizeof(ico_subtitles_pt_br) / sizeof(ico_subtitles_pt_br[0]));
