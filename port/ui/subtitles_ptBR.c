/*
 * port/ui/subtitles_ptBR.c
 *
 * Brazilian Portuguese text subtitles translated from https://github.com/nathanialf/ico-pc/issues/157#issuecomment-6586745670
 */
#include "subtitles.h"

static const UiSubtitleLine kLines[] = {
    {0, "Pegue a espada."},
    {1, "Não fique bravo conosco.\nIsso é pelo bem da vila."},
    {2, "Tem alguém aí? Quem é você?"},
    {4, "O que você está fazendo aí?"},
    {5, "Espera. Eu vou te tirar daí."},
    {6, "Eu… Eu sou um sacrifício.\nPorque tenho chifres. Tá vendo?"},
    {7, "Crianças que tem chifres\nsão trazidas pra cá."},
    {8, "Você também é\num sacrifício?"},
    {9, "Quem é você?\nComo entrou aqui?"},
    {11, "Que criatura era aquela\nque veio atrás de você?"},
    {12, "É muito perigoso pra você\nficar aqui também!"},
    {13, "Precisamos sair daqui."},
    {19, "Como você fez aquilo?"},
    {25, "Olha, o portão está aberto!\nPodemos sair!"},
    {26, "Vamos!"},
    {27, "Você está bem?"},
    {29, "Eu a deixei zangada…"},
    {31, "Venha para casa, Yorda."},
    {34, "Então é você quem anda arrastando\nminha querida Yorda por aí."},
    {35, "Faz ideia de quem ela é?"},
    {36, "Ela é minha única e amada filha."},
    {37, "Aquela que um dia herdará este castelo."},
    {38, "Vocês pertencem a dois mundos\ncompletamente diferentes."},
    {43, "Agora se coloque no seu lugar\ne vá embora daqui."},
    {48, "Yorda, por que não\nconsegue entender?"},
    {49, "Você não sobreviveria\nno mundo lá fora."},
    {86, "Só mais um pouco…"},
    {88, "Você está bem?"},
    {91, "Obrigada…"},
    {93, "Espere."},
    {96, "O que você fez com ela?"},
    {97, "Desista. Já é tarde demais."},
    {98, "Meu corpo não resistirá\npor muito mais tempo."},
    {99, "Yorda herdará minha vontade e renascerá\ncomo a nova senhora deste castelo."},
    {100, "Este é o destino dela.\nSer o receptáculo da minha alma."},
    {104, "Quando ela acordar,\njá não se lembrará de você."},
    {105, "Agora largue a espada e saia daqui."},
    {106, "É o que ela também desejaria…"},
    {109, "Que menino mau…\nQuer tanto assim morrer?"},
    {110, "Ela jamais poderá\nsair deste castelo…"},
    {111, "Mesmo que você tire…\na minha vida…"},
    {112, "Adeus."},
};

const UiSubtitlePack subtitles_ptBR = {
    "ptBR",
    "Português (Brasil)",
    kLines,
    sizeof(kLines) / sizeof(kLines[0]),
};