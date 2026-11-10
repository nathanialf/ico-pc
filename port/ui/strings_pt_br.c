/*
 * port/ui/strings_pt_br.c
 *
 * The port's UI strings and the European disc's menu words in Brazilian
 * Portuguese, translated by Igor Chagas (ichagas7, GitHub issue 157's CSVs),
 * UTF-8: a community translation (strings.h UI_LANG_COMMUNITY_FIRST,
 * docs/TRANSLATING.md).  An id the translation leaves empty, one newer than
 * its CSV, one whose English changed since, or a note longer than its space
 * is NULL and takes English at run time (strings.c ui_str_in).  The rows
 * marked [cobra838] were corrected or filled by cobra838 (GitHub PR 180).
 */
#include <stddef.h>

#include "strings.h"

const char *const ui_strings_pt_br[UI_STR_COUNT] = {
    [UI_STR_NONE] = "",
    [UI_STR_SETTINGS] = "Opções",
    [UI_STR_SECTION_DISPLAY] = "Tela",
    [UI_STR_SECTION_CONTROLS] = "Controles",
    [UI_STR_SECTION_GAMEPLAY] = "Jogo",
    [UI_STR_SECTION_LANGUAGE] = "Idioma",
    [UI_STR_BACK] = "Voltar",
    [UI_STR_ON] = "Ligado",
    [UI_STR_OFF] = "Desligado",
    [UI_STR_OPT_PRESET] = "Opção",
    [UI_STR_VAL_ORIGINAL] = NULL, /* the same as the English */
    [UI_STR_VAL_ENHANCED] = "Melhorado",
    [UI_STR_OPT_FULLSCREEN] = "Tela Cheia",
    [UI_STR_OPT_RESOLUTION] = "Resolução",
    [UI_STR_OPT_ASPECT] = "Proporção de Tela",
    [UI_STR_OPT_VSYNC] = "V-Sync",
    [UI_STR_OPT_FILTERING] = "Filtro de Texturas",
    [UI_STR_OPT_FULL_HEIGHT] = "Preencher Tela",
    [UI_STR_OPT_MIRROR] = "Modo Espelhado",
    [UI_STR_OPT_REMAP] = "Remapear Controles",
    [UI_STR_OPT_MOUSE_CAMERA] = "Câmera pelo Mouse",
    [UI_STR_OPT_VIBRATION] = "Vibração",
    [UI_STR_OPT_STICK_FIX] = "Correção do Analógico",
    [UI_STR_OPT_YORDA] = "Sombras nunca pegam a Yorda",
    [UI_STR_OPT_YORDA_NOTE] =
        "Para uma experiência menos estressante. Algumas cenas roteirizadas ainda mostrarão a captura.",
    [UI_STR_OPT_PIT_TIME] = "Tempo para resgatar a Yorda",
    /* the English now names the row: "The Shadows never take Yorda row stops it." */
    [UI_STR_OPT_PIT_TIME_NOTE] = NULL,
    [UI_STR_OPT_SHADOW_SPEED] = "Velocidade das Sombras",
    [UI_STR_OPT_SHADOW_SPEED_NOTE] =
        "Quão rápido as Sombras se andam, correm e voam. Não altera a frequência dos ataques.",
    [UI_STR_VAL_SPEED_SLOWER] = "Mais lento",
    [UI_STR_VAL_SPEED_FASTER] = "Mais rápido",
    [UI_STR_VAL_SPEED_FASTEST] = "Muito rápido",
    [UI_STR_LANG_EN] = NULL, /* empty in the CSV: English */
    [UI_STR_LANG_FR] = NULL, /* empty in the CSV: English */
    [UI_STR_LANG_DE] = NULL, /* empty in the CSV: English */
    [UI_STR_LANG_IT] = NULL, /* empty in the CSV: English */
    [UI_STR_LANG_ES] = NULL, /* empty in the CSV: English */
    [UI_STR_LANG_JA] = NULL, /* empty in the CSV: English */
    [UI_STR_OPT_DEVELOPER_MODE] = "Modo de Desenvolvedor",
    [UI_STR_DEVELOPER_NOTE] = "Conquistas são suspensas no Modo de Desenvolvedor",
    [UI_STR_POPUP_TEST_TITLE] = NULL, /* empty in the CSV: English */
    [UI_STR_POPUP_TEST_BODY] = NULL,  /* empty in the CSV: English */
    [UI_STR_ACH_OPENING] = "O Sacrifício",
    [UI_STR_ACH_OPENING_DESC] = "Assista a abertura sem pulá-la.",
    [UI_STR_ACH_HAND] = "Mão com mão",
    [UI_STR_ACH_HAND_DESC] = "Pegue na mão de Yorda",
    [UI_STR_ACH_GATE] = "Portão do Castelo",
    [UI_STR_ACH_GATE_DESC] = "Chegue até o Portão Principal do Castelo",
    [UI_STR_ACH_WINDMILL] = "O Moinho",
    [UI_STR_ACH_WINDMILL_DESC] = "Chegue até o Moinho",
    [UI_STR_ACH_GRAVE] = "O Cemitério",
    [UI_STR_ACH_GRAVE_DESC] = "Chegue até o Cemitério",
    [UI_STR_ACH_WATERFALL] = "A Cachoeira",
    [UI_STR_ACH_WATERFALL_DESC] = "Chegue até a Cachoeira",
    [UI_STR_ACH_GONDOLA] = "A Gôndola",
    [UI_STR_ACH_GONDOLA_DESC] = "Chegue até a Gôndola",
    [UI_STR_ACH_WATERTOWER] = "A Caixa D'Água",
    [UI_STR_ACH_WATERTOWER_DESC] = "Chegue até a Caixa D'Água",
    [UI_STR_ACH_CLIFF] = "O Penhasco",
    [UI_STR_ACH_CLIFF_DESC] = "Chegue até o Penhasco",
    [UI_STR_ACH_WINGS] = "Leste e Oeste",
    [UI_STR_ACH_WINGS_DESC] = "Chegue até os salões simétricos da esquerda e da direita",
    [UI_STR_ACH_QUEEN] = "A Rainha",
    [UI_STR_ACH_QUEEN_DESC] = "Entre no salão da Rainha",
    [UI_STR_ACH_QUEEN_DEFEATED] = "A Queda da Rainha",
    [UI_STR_ACH_QUEEN_DEFEATED_DESC] = "Derrote a Rainha",
    [UI_STR_ACH_BEACH] = "A Praia",
    [UI_STR_ACH_BEACH_DESC] = "Chegue até a Praia",
    [UI_STR_ACH_CLEAR] = NULL, /* the same as the English */
    [UI_STR_ACH_CLEAR_DESC] = "Termine o jogo",
    [UI_STR_ACH_CLEAR2] = "Mais uma vez",
    [UI_STR_ACH_CLEAR2_DESC] = "Termine a jornada em um Novo Jogo+",
    [UI_STR_ACH_FIRST_KILL] = "Banidor das Sombras",
    [UI_STR_ACH_FIRST_KILL_DESC] = "Derrote uma criatura das Sombras",
    [UI_STR_ACH_KILLS_25] = "Caçador de Sombras",
    [UI_STR_ACH_KILLS_25_DESC] = "Derrote 25 criaturas das Sombras",
    [UI_STR_ACH_KILLS_100] = "Flagelo das Sombras",
    [UI_STR_ACH_KILLS_100_DESC] = "Derrote 100 criaturas das Sombras",
    [UI_STR_ACH_RESCUE] = "Não sem Ela",
    [UI_STR_ACH_RESCUE_DESC] = "Liberte a Yorda das garras de uma Sombra",
    [UI_STR_ACH_HAND_10] = "Nunca solte",
    [UI_STR_ACH_HAND_10_DESC] = "Segure a mão da Yorda por 10 minutos no total",
    [UI_STR_ACH_HAND_60] = "Inseparáveis",
    [UI_STR_ACH_HAND_60_DESC] = "Segure a mão da Yorda por 1 hora no total",
    [UI_STR_ACH_FIRST_SAVE] = "Um Momento de Descanso",
    [UI_STR_ACH_FIRST_SAVE_DESC] = "Salve o jogo em um Sofá",
    [UI_STR_ACH_SOFAS_5] = "Viajantes Exaustos",
    [UI_STR_ACH_SOFAS_5_DESC] = "Salve o jogo em 5 sofás diferentes",
    [UI_STR_ACH_SWORD] = "A Espada",
    [UI_STR_ACH_SWORD_DESC] = "Pegue a Espada",
    [UI_STR_ACH_QUEEN_SWORD] = "A Espada da Rainha",
    [UI_STR_ACH_QUEEN_SWORD_DESC] = "Empunhe a Espada da Rainha",
    [UI_STR_ACH_LIGHT_BLADE] = "Uma Luz Estranha",
    [UI_STR_ACH_LIGHT_BLADE_DESC] = "Empunhe a Espada da Luz",
    [UI_STR_ACH_NO_CAPTURE] = "Jamais Capturada",
    [UI_STR_ACH_NO_CAPTURE_DESC] =
        "Termine um novo jogo sem que uma Sombra capture a Yorda", /* [cobra838] */
    [UI_STR_ACH_NO_GAMEOVER] = "Intacto",
    [UI_STR_ACH_NO_GAMEOVER_DESC] = "Termine um novo jogo sem nenhuma morte",
    [UI_STR_ACH_FAST] = "Escapada",
    [UI_STR_ACH_FAST_DESC] = "Termine o jogo em até 3 horas",
    [UI_STR_ACH_SECRET] = "Um Presente na Praia",
    [UI_STR_ACH_SECRET_DESC] = "Veja a cena secreta na Praia",
    [UI_STR_SECTION_AUDIO] = "Áudio",
    [UI_STR_SECTION_ACHIEVEMENTS] = "Conquistas",
    [UI_STR_OPT_VOLUME] = NULL, /* the same as the English */
    [UI_STR_OPT_MOUSE_SENS] = "Sensibilidade do Mouse",
    [UI_STR_OPT_VIDEO_MODE] = "Modo de Vídeo",
    [UI_STR_VAL_PAL50] = NULL, /* empty in the CSV: English */
    [UI_STR_VAL_60HZ] = NULL,  /* empty in the CSV: English */
    [UI_STR_OPT_FRAMERATE] = "Taxa de Frames",
    [UI_STR_VAL_WINDOW] = "Janela",
    [UI_STR_VAL_AUTO] = "Automático",
    [UI_STR_VAL_TRILINEAR] = NULL, /* the same as the English */
    [UI_STR_VAL_ANISOTROPIC] = "Anisotrópico",
    [UI_STR_LANGUAGE_NOTE] = "As legendas e o texto do jogo mudam na próxima inicialização",
    [UI_STR_OPT_MENU_LANGUAGE] = "Idioma do Menu",
    [UI_STR_MENU_LANGUAGE_NOTE] =
        "O idioma deste menu, da ajuda no modo foto e demais mensagens. O texto do jogo é sempre o do disco.",
    [UI_STR_OPT_DISC] = "Disco",
    [UI_STR_DISC_NOTE] =
        "Qual disco usar quando mais de um está instalado. Se aplica na próxima inicialização",
    [UI_STR_DISC_PAL] = NULL,   /* empty in the CSV: English */
    [UI_STR_DISC_JPN] = NULL,   /* empty in the CSV: English */
    [UI_STR_DISC_KOR] = NULL,   /* empty in the CSV: English */
    [UI_STR_DISC_NTSC] = NULL,  /* empty in the CSV: English */
    [UI_STR_DISC_AUG6] = NULL,  /* empty in the CSV: English */
    [UI_STR_DISC_JUN26] = NULL, /* empty in the CSV: English */
    [UI_STR_DISC_CN] = NULL,    /* empty in the CSV: English */
    [UI_STR_REMAP_KEYBOARD] = "Teclado",
    [UI_STR_REMAP_GAMEPAD] = "Controle",
    [UI_STR_REMAP_HINT] = "Cruz: alterar    Quadrado: limpar    Triângulo: voltar", /* [cobra838] */
    [UI_STR_REMAP_HINT_CIRCLE] =
        "Círculo: alterar    Quadrado: limpar    Triângulo: voltar", /* [cobra838] */
    [UI_STR_REMAP_MENU_NOTE] =
        "Nos menus, os botões de face e o direcional do controle sempre seguem a posição.", /* [cobra838] */
    [UI_STR_REMAP_PRESS] = "Aperte uma tecla ou botão…",
    [UI_STR_REMAP_RESET] = "Restaurar os padrões",
    [UI_STR_OPT_TOUCH_MODE] = "Controles de Toque",
    [UI_STR_OPT_TOUCH_SIZE] = "Tamanho dos botões de toque",
    [UI_STR_OPT_TOUCH_OPACITY] = "Opacidade dos botões de toque",
    [UI_STR_VAL_ALWAYS] = "Sempre",
    [UI_STR_VAL_SMALL] = "Pequeno",
    [UI_STR_VAL_MEDIUM] = "Médio",
    [UI_STR_VAL_LARGE] = "Grande",
    [UI_STR_TOUCH_NOTE] = "Botões na tela. São ocultados quando um controle é conectado.",
    [UI_STR_BTN_CROSS] = "X",
    [UI_STR_BTN_CIRCLE] = "Círculo",
    [UI_STR_BTN_SQUARE] = "Quadrado",
    [UI_STR_BTN_TRIANGLE] = "Triângulo",
    [UI_STR_BTN_START] = NULL,  /* the same as the English */
    [UI_STR_BTN_SELECT] = NULL, /* the same as the English */
    [UI_STR_BTN_DPAD] = NULL,   /* the same as the English */
    [UI_STR_STICK_LEFT] = "Analógico Esquerdo",
    [UI_STR_STICK_RIGHT] = "Analógico Direito",
    [UI_STR_DIR_UP] = "Cima",
    [UI_STR_DIR_DOWN] = "Baixo",
    [UI_STR_DIR_LEFT] = "Esquerda",
    [UI_STR_DIR_RIGHT] = "Direita",
    [UI_STR_PAD_SOUTH] = "Sul",
    [UI_STR_PAD_EAST] = "Leste",
    [UI_STR_PAD_WEST] = "Oeste",
    [UI_STR_PAD_NORTH] = "Norte",
    [UI_STR_PAD_BACK] = "Voltar",
    [UI_STR_PAD_START] = NULL, /* the same as the English */
    [UI_STR_PAD_L3] = NULL,    /* empty in the CSV: English */
    [UI_STR_PAD_R3] = NULL,    /* empty in the CSV: English */
    [UI_STR_PAD_L1] = NULL,    /* empty in the CSV: English */
    [UI_STR_PAD_R1] = NULL,    /* empty in the CSV: English */
    [UI_STR_PAD_DPAD_UP] = "Seta para Cima",
    [UI_STR_PAD_DPAD_DOWN] = "Seta para Baixo",
    [UI_STR_PAD_DPAD_LEFT] = "Seta para a Esquerda",
    [UI_STR_PAD_DPAD_RIGHT] = "Seta para a Direita",
    [UI_STR_PAD_L2] = NULL, /* empty in the CSV: English */
    [UI_STR_PAD_R2] = NULL, /* empty in the CSV: English */
    [UI_STR_PAD_LSTICK_LEFT] = "Analógico Esquerdo para a Esquerda",
    [UI_STR_PAD_LSTICK_RIGHT] = "Analógico Esquerdo para a Direita",
    [UI_STR_PAD_LSTICK_UP] = "Analógico Esquerdo para Cima",
    [UI_STR_PAD_LSTICK_DOWN] = "Analógico Esquerdo para Baixo",
    [UI_STR_PAD_RSTICK_LEFT] = "Analógico Direito para a Esquerda",
    [UI_STR_PAD_RSTICK_RIGHT] = "Analógico Direito para a Direita",
    [UI_STR_PAD_RSTICK_UP] = "Analógico Direito para Cima",
    [UI_STR_PAD_RSTICK_DOWN] = "Analógico Direito para Baixo",
    [UI_STR_VAL_UNCAPPED] = "Ilimitado",
    [UI_STR_FPS_UNIT] = "Frames por Segundo",
    [UI_STR_VIDEO_MODE_TITLE_ONLY] = NULL, /* empty in the CSV: English */
    [UI_STR_ACH_LOCKED] = "Bloqueado",
    [UI_STR_ACH_STATE_UNLOCKED] = "Desbloqueado",
    [UI_STR_MIRROR_SCREEN] =
        "O jogo é invertido horizontalmente. Mantém a escolha ativada em saves.",
    [UI_STR_OPT_NEWGAME_PLUS] = "Novo Jogo+",
    [UI_STR_NEWGAME_PLUS_SCREEN] =
        NULL, /* longer than its space (170 bytes): English until shortened */
    [UI_STR_NEWGAME_START] = "Iniciar",
    [UI_STR_NEWGAME_START_SCREEN] = "Inicia o novo jogo com as escolhas acima.",
    [UI_STR_OPT_CRT] = "Filtro CRT",
    [UI_STR_OPT_CRT_STRENGTH] = "Intensidade do Filtro CRT",
    [UI_STR_OPT_TONE_GAMMA] = NULL,                     /* newer than the translation: English */
    [UI_STR_OPT_TONE_BRIGHTNESS] = NULL,                /* newer than the translation: English */
    [UI_STR_OPT_TONE_CONTRAST] = NULL,                  /* newer than the translation: English */
    [UI_STR_OPT_TONE_SATURATION] = NULL,                /* newer than the translation: English */
    [UI_STR_OPT_TONE_NOTE] = NULL,                      /* newer than the translation: English */
    [UI_STR_VAL_CRT_SCANLINES] = "Linhas de varredura", /* [cobra838] */
    [UI_STR_VAL_CRT_CONSUMER] = "TV de consumo",        /* [cobra838] */
    [UI_STR_VAL_CRT_TRINITRON] = NULL,                  /* empty in the CSV: English */
    [UI_STR_VAL_CRT_PVM] = NULL,                        /* empty in the CSV: English */
    [UI_STR_VAL_CRT_SHADOW] = "Máscara de sombra",      /* [cobra838] */
    [UI_STR_VAL_CUSTOM] = "Customizado",
    [UI_STR_RESOLUTION_CRT_NOTE] =
        "Filtro CRT: os pixels originais do PS2, 1x enquanto ativado.", /* [cobra838] */
    [UI_STR_QUIT_DESKTOP] = "Voltar à Área de Trabalho",
    [UI_STR_QUIT_CONFIRM] = "Voltar à Área de Trabalho?",
    [UI_STR_OPT_CIRCLE_BACK] = "Use Círculo para Voltar",
    [UI_STR_CIRCLE_BACK_NOTE] = "Círculo (B) também Volta nos Menus do Jogo, assim como Triângulo.",
    [UI_STR_MT_LANG_ENGLISH] = NULL,  /* empty in the CSV: English */
    [UI_STR_MT_LANG_FRANCAIS] = NULL, /* empty in the CSV: English */
    [UI_STR_MT_LANG_DEUTSCH] = NULL,  /* empty in the CSV: English */
    [UI_STR_MT_LANG_ITALIANO] = NULL, /* empty in the CSV: English */
    [UI_STR_MT_LANG_ESPANOL] = NULL,  /* empty in the CSV: English */
    [UI_STR_MT_HZ50] = NULL,          /* empty in the CSV: English */
    [UI_STR_MT_HZ60] = NULL,          /* empty in the CSV: English */
    [UI_STR_MT_NO_CARD] = "Nenhum Cartão de Memória (PS2) inserido",
    [UI_STR_MT_NEED_START] =
        "No mínimo 360KB são necessários\npara salvar os dados do jogo.\nDeseja iniciar?",
    [UI_STR_MT_NO_SPACE_START] =
        "Espaço insuficiente\nno Cartão de Memória (PS2).\nNo mínimo 360KB são necessários\npara salvar os dados do jogo.\nDeseja iniciar?",
    [UI_STR_MT_YES] = "Sim",
    [UI_STR_MT_NO] = "Não",
    [UI_STR_MT_VIBRATION] = "Vibração",
    [UI_STR_MT_ACTIVATE] = "Ativar",
    [UI_STR_MT_DEACTIVATE] = "Desativar",
    [UI_STR_MT_CONTINUE] = "Continuar",
    [UI_STR_MT_NEW_GAME] = "Novo Jogo",
    [UI_STR_MT_DO_NOT_REMOVE] = "Não remova o\nCartão de Memória (PS2)\nou desligue o sistema.",
    [UI_STR_MT_ACCESSING] = "Acessando",
    [UI_STR_MT_LOC_OLD_BRIDGE] = "Ponte Antiga",
    [UI_STR_MT_LOC_TROLLEY_A] = "Carrinho 2",
    [UI_STR_MT_LOC_MAIN_GATE] = "Portão Principal",
    [UI_STR_MT_LOC_GRAVEYARD] = "Cemitério",
    [UI_STR_MT_LOC_WINDMILL] = "Moinho",
    [UI_STR_MT_LOC_STONE_PILLAR] = "Pilar de Pedra",
    [UI_STR_MT_LOC_EAST_ARENA] = "Arena Leste",
    [UI_STR_MT_LOC_EAST_REFLECTOR] = "Refletor Leste",
    [UI_STR_MT_LOC_WATERFALL] = "Cachoeira",
    [UI_STR_MT_LOC_GONDOLA] = "Gôndola",
    [UI_STR_MT_LOC_WATER_TOWER] = "Caixa D'Água",
    [UI_STR_MT_LOC_WEST_IDOL_STAIRS] = "Escadaria do Ídolo Oeste",
    [UI_STR_MT_LOC_TROLLEY_B] = "Carrinho 1",
    [UI_STR_MT_LOC_CRANE] = "Guindaste",
    [UI_STR_MT_LOC_SANDY_BEACH] = "Praia",
    [UI_STR_MT_SLOT_1] = "Entrada do Cartão de memória 1",
    [UI_STR_MT_SLOT_2] = "Entrada do Cartão de memória 2",
    [UI_STR_MT_OK] = NULL, /* the same as the English */
    [UI_STR_MT_BACK] = "Voltar",
    [UI_STR_MT_SELECT_SLOT] = "Escolha a Entrada do Cartão de Memória",
    [UI_STR_MT_SELECT_LOAD] = "Escolha um arquivo para carregar",
    [UI_STR_MT_NO_SAVE_DATA] = "Não há dados salvos.",
    [UI_STR_MT_LOADING] = "Carregando",
    [UI_STR_MT_SAVE_Q] = "Salvar?",
    [UI_STR_MT_NO_SPACE] = "Espaço insuficiente\nno Cartão de Memória (PS2).",
    [UI_STR_MT_NEED_360] = "(São necessários 360KB ou mais)",
    [UI_STR_MT_SELECT_SAVE] = "Selecione o arquivo para salvar",
    [UI_STR_MT_OVERWRITE] = "Salvar irá sobrescrever\nos dados neste arquivo.\nProceder?",
    [UI_STR_MT_NOT_FORMATTED] =
        "O Cartão de Memória (PS2)\nnão está formatado.\nFormatar?", /* [cobra838] */
    [UI_STR_MT_FORMATTING] = "Formatando",
    [UI_STR_MT_SAVING] = "Salvando",
    [UI_STR_MT_FILE_SAVED] = "Arquivo Salvo.",
    [UI_STR_MT_RESUME_GAME] = "Continuar o Jogo",
    [UI_STR_MT_END_GAME] = "Sair do Jogo",
    [UI_STR_MT_GAME_WILL_END] = "O jogo irá fechar.\nProsseguir?",
    [UI_STR_MT_SAVE_FAILED] = "O salvamento falhou.",
    [UI_STR_MT_FORMAT_FAILED] = "O formato falhou.",
    [UI_STR_MT_LOAD_FAILED] = "O carregamento falhou.",
    [UI_STR_MT_NO_ACCESS] = "Não é possível acessar\no Cartão de Memória (PS2)",
    [UI_STR_MT_OPTIONS] = "Opções",
    [UI_STR_MT_PAUSE_BACK] = "Voltar",
    [UI_STR_MT_FILM_EFFECT] = "Efeito de Filme",
    [UI_STR_MT_SOUND] = "Som",
    [UI_STR_MT_STEREO] = "Estéreo",
    [UI_STR_MT_MONO] = NULL,                   /* the same as the English */
    [UI_STR_MT_HOLD_TYPE] = "Tipo de Segurar", /* [cobra838] */
    [UI_STR_MT_BUTTON_CONFIG] = "Configuração dos Botões",
    [UI_STR_MT_BRIGHTNESS] = "Brilho",
    [UI_STR_MT_PLAYERS] = "Jogadores",
    [UI_STR_MT_JUMP] = "Pular",
    [UI_STR_MT_ATTACK] = "Atacar",
    [UI_STR_MT_ACTION] = "Ação",
    [UI_STR_MT_RELEASE] = "Soltar",
    [UI_STR_MT_HOLD_HAND_CALL] = "Segurar a mão/Chamar",
    [UI_STR_MT_ZOOM] = NULL, /* the same as the English */
    [UI_STR_MT_DEFAULT] = "Padrão",
    [UI_STR_MT_ADJUST_HINT] = "Ajustar com botões direcionais",
    [UI_STR_MT_DARK] = "Escuro",
    [UI_STR_MT_LIGHT] = "Claro",
    [UI_STR_MT_CONTINUE_Q] = "Continuar?",
    [UI_STR_MT_DIGIT_0] = NULL,   /* empty in the CSV: English */
    [UI_STR_MT_DIGIT_1] = NULL,   /* empty in the CSV: English */
    [UI_STR_MT_DIGIT_2] = NULL,   /* empty in the CSV: English */
    [UI_STR_MT_DIGIT_3] = NULL,   /* empty in the CSV: English */
    [UI_STR_MT_DIGIT_4] = NULL,   /* empty in the CSV: English */
    [UI_STR_MT_DIGIT_5] = NULL,   /* empty in the CSV: English */
    [UI_STR_MT_DIGIT_6] = NULL,   /* empty in the CSV: English */
    [UI_STR_MT_DIGIT_7] = NULL,   /* empty in the CSV: English */
    [UI_STR_MT_DIGIT_8] = NULL,   /* empty in the CSV: English */
    [UI_STR_MT_DIGIT_9] = NULL,   /* empty in the CSV: English */
    [UI_STR_MT_DIGIT_10] = NULL,  /* empty in the CSV: English */
    [UI_STR_MT_COLON] = NULL,     /* empty in the CSV: English */
    [UI_STR_MT_VAL_A] = NULL,     /* empty in the CSV: English */
    [UI_STR_MT_VAL_B] = NULL,     /* empty in the CSV: English */
    [UI_STR_MT_PLAYERS_1] = NULL, /* empty in the CSV: English */
    [UI_STR_MT_PLAYERS_2] = NULL, /* empty in the CSV: English */
    [UI_STR_EXTRAS] = NULL,       /* empty in the CSV: English */
    [UI_STR_EXTRAS_MUSIC] = "Música",
    [UI_STR_EXTRAS_MODELS] = "Modelos",
    [UI_STR_EXTRAS_CREDITS] = "Créditos",
    [UI_STR_EXTRAS_LOCKED_NOTE] = "Conclua o jogo para desbloquear",
    [UI_STR_OPT_MUSIC_VOL] = "Volume da música",
    [UI_STR_OPT_EFFECTS_VOL] = "Volume dos efeitos",
    [UI_STR_OPT_OUTPUT] = "Volume de saída",
    [UI_STR_VAL_STEREO] = "Estéreo",
    [UI_STR_VAL_MONO] = NULL, /* the same as the English */
    [UI_STR_OPT_DEVICE] = "Dispositivo de saída",
    [UI_STR_VAL_DEFAULT_DEVICE] = "Padrão",
    [UI_STR_VAL_DEFAULT] = "Padrão",
    [UI_STR_GAL_SOUNDTRACK] = "Trilha Sonora",
    [UI_STR_GAL_SCENE] = "Sons da Cena",
    [UI_STR_GAL_AMBIENCE] = "Ambiência",
    [UI_STR_GAL_VOICE] = "Voz",
    [UI_STR_GAL_SE] = "Efeitos",
    [UI_STR_GAL_PLAYING] = "Tocando",
    [UI_STR_GAL_STOPPED] = "Parou",
    [UI_STR_GAL_EMPTY] = "As tabelas de som do jogo não estão carregadas",
    [UI_STR_GAL_PAUSED] = "Pausado",
    [UI_STR_HINT_PLAY] = "Tocar",
    [UI_STR_HINT_PAUSE] = "Pausar",
    [UI_STR_HINT_STOP] = "Parar",
    [UI_STR_HINT_PREV] = "Anterior",
    [UI_STR_HINT_NEXT] = "Próximo",
    [UI_STR_HINT_SECTION] = "Seção",
    [UI_STR_MV_ANIMATION] = "Animação",
    [UI_STR_MV_LOOP] = NULL,  /* the same as the English */
    [UI_STR_MV_FRAME] = NULL, /* the same as the English */
    [UI_STR_MV_NO_ANIMATIONS] = "Sem animações",
    [UI_STR_MV_HINT_VIEW] = "Visualizar",
    [UI_STR_MV_HINT_TITLE] = "Tela de Título",
    [UI_STR_MV_HINT_PLAY] = "Tocar",
    [UI_STR_MV_HINT_TURN] = "Virar",
    [UI_STR_MV_HINT_ZOOM] = NULL, /* the same as the English */
    [UI_STR_MV_HINT_MOVE] = "Mover",
    [UI_STR_MV_HINT_SAVE] = "Salvar arquivos de Modelos",
    [UI_STR_MV_SAVED_FMT] = "%d arquivos salvos em %s", /* [cobra838] */
    [UI_STR_MV_SAVED_NONE] = "Nenhum arquivo Salvo",
    [UI_STR_MV_ICO] = NULL,   /* the same as the English */
    [UI_STR_MV_YORDA] = NULL, /* the same as the English */
    [UI_STR_MV_QUEEN] = "A Rainha",
    [UI_STR_MV_SHADOW] = "Sombra",
    [UI_STR_MV_SHADOW_WINGED] = "Sombra Alada",
    [UI_STR_MV_SHADOW_HORNED] = "Sombra com Chifres",
    [UI_STR_MV_SHADOW_HORNED_WINGED] = "Sombra Alada com Chifres",
    [UI_STR_MV_SHADOW_BULL] = "Sombra-touro",
    [UI_STR_MV_SHADOW_BULL_WINGED] = "Sombra-touro Alada",
    [UI_STR_MV_SHADOW_LARVA] = "Sombra-larva",
    [UI_STR_MV_SHADOW_BUTTERFLY] = "Sombra-borboleta",
    [UI_STR_MV_SHADOW_SLUG] = "sombra-lesma",
    [UI_STR_MV_SHADOW_BONES] = "Sombra-esqueleto",
    [UI_STR_MV_BIRD] = "Pássaro",
    [UI_STR_MV_GUARD] = "Guarda",
    [UI_STR_MV_CAGE] = "Cela",
    [UI_STR_MV_BOMB] = "Bomba",
    [UI_STR_MV_POT] = "Vaso",
    [UI_STR_MV_BARREL] = "Barril",
    [UI_STR_MV_LEVER] = "Alavanca",
    [UI_STR_MV_SWORD] = "Espada",
    [UI_STR_MV_MAGIC_SWORD] = "Espada Mágica (Espada da Rainha)",
    [UI_STR_MV_COUCH] = "Sofá de Pedra",
    [UI_STR_PHOTO_MODE] = "Modo Foto",
    [UI_STR_PHOTO_FOV] = "Campo de visão %d°", /* [cobra838] */
    [UI_STR_PHOTO_SAVED] = "Foto salva",
    [UI_STR_PHOTO_FAILED] = "Foto não foi salva",
    [UI_STR_PHOTO_CAM_FREE] = "Câmera Livre",
    [UI_STR_PHOTO_CAM_ORBIT] = "Câmera Orbital",
    [UI_STR_PHOTO_SPEED] = "velocidade %s", /* [cobra838] */
    [UI_STR_PHOTO_SPEED_SLOW] = "Lento",
    [UI_STR_PHOTO_SPEED_NORMAL] = NULL, /* the same as the English */
    [UI_STR_PHOTO_SPEED_FAST] = "Rápido",
    [UI_STR_OPT_BRIGHTNESS] = "Brilho",
    [UI_STR_OPT_HOLD_TYPE] = "Tipo de Segurar", /* [cobra838] */
    [UI_STR_OPT_BUTTON_CONFIG] = "Configuração dos botões",
    [UI_STR_OPT_FILM_EFFECT] = "Efeito de Filme",
    [UI_STR_OPT_PLAYERS] = "Jogadores",
    [UI_STR_OPT_ACH_POPUPS] = "Pop-up de Conquistas",
    [UI_STR_VAL_HOLD_A] = "A (segurar)",
    [UI_STR_VAL_HOLD_B] = "B (alternar)",
    [UI_STR_VAL_HOLD_DISC_A] = "Original do disco (A)", /* [cobra838] */
    [UI_STR_VAL_HOLD_DISC_B] = "Original do disco (B)", /* [cobra838] */
    [UI_STR_HOLD_TYPE_NOTE] =
        "A: segure R1 para segurar a mão da Yorda. B: aperte R1 uma vez para segurar a mão da Yorda; aperte novamente para soltar.",
    [UI_STR_STICK_FIX_ALSO_NOTE] = "Também presente na aba \"Jogo\"",
    [UI_STR_ACH_POPUPS_ALSO_NOTE] = "Também presente na aba \"Conquistas\"",
    [UI_STR_PLAYERS_NOTE] = "Um segundo controle controla a Yorda",
    [UI_STR_BUTTON_CONFIG_NOTE] =
        "A disposição de botões no seu save. Se aplica sobre \"remapear controles.\"",
    [UI_STR_OPT_TEXTURE_PACK] = "Pacote de Texturas",
    [UI_STR_VAL_NONE_INSTALLED] = "Nenhum instalado",
    [UI_STR_TEXTURE_PACK_NOTE] = "Pacotes PCSX2: copie %s para textures.", /* [cobra838] */
    [UI_STR_OPT_DUMP_TEXTURES] = "Exportar Texturas",
    [UI_STR_DUMP_TEXTURES_NOTE] = NULL, /* its % codes differ from English's */
    [UI_STR_OPT_MODEL_PACK] = "Pacote de Modelos",
    [UI_STR_MODEL_PACK_NOTE] = "Pacotes de modelos: copie %s para models.", /* [cobra838] */
    [UI_STR_OPT_DUMP_MODELS] = "Exportar Modelos",
    [UI_STR_DUMP_MODELS_NOTE] = NULL, /* its % codes differ from English's */
    [UI_STR_OPT_REPORT_PICTURE] = "Salvar uma imagem para um relatório de erro",
    [UI_STR_REPORT_PICTURE_NOTE] =
        "Salva a imagem da tela 3 segundos depois, junto com o log, em um arquivo zip na pasta dumps.",
    [UI_STR_REPORT_PICTURE_ARMED] = "Captura de tela em 3 segundos",
    [UI_STR_REPORT_PICTURE_ARMED_BODY] =
        "Sai do menu agora.\nO arquivo zip vai para a pasta dumps.",
#if ICO_RC_TOOLS
    [UI_STR_OPT_SHARE_REPORT] = "Compartilhar relatório de erro", /* [cobra838] */
    [UI_STR_SHARE_REPORT_NOTE] =
        "Abre o menu de compartilhamento com o log e o zip de imagem mais recente, para enviar por e-mail ou aplicativo de mensagens.", /* [cobra838] */
#endif
    [UI_STR_SECTION_EFFECTS] = "Efeitos",
    [UI_STR_SECTION_PICTURE] = "Imagem",
    [UI_STR_SECTION_DEVELOPER] = "Desenvolvedor",
    [UI_STR_OPT_EFFECT_GLOW] = "Brilho",
    [UI_STR_OPT_EFFECT_DEPTH_OF_FIELD] = "Profundidade de campo",
    [UI_STR_OPT_EFFECT_SOFTENING] = "Suavização de tela",
    [UI_STR_OPT_EFFECT_MOTION_BLUR] = "Desfoque de movimento",
    [UI_STR_OPT_EFFECT_FOG] = "Neblina",
    [UI_STR_EFFECTS_NOTE] =
        "Os próprios efeitos de imagem do jogo. \"Tudo Ativado\" corresponde à imagem do PS2. A predefinição não altera esses efeitos, exceto o brilho entre quadros.",
    [UI_STR_STATS_PLAY_TIME] = "Tempo de Jogo",
    [UI_STR_STATS_DEATHS] = "Número de Mortes",
    [UI_STR_STATS_CAPTURES] = "Yorda Capturada",
    [UI_STR_STATS_SAVES] = "N° de saves nesta jornada",
    [UI_STR_STATS_ENEMIES] = "Inimigos derrotados nesta jornada",
    [UI_STR_STATS_ASSISTS] = "Assistências",
    [UI_STR_STATS_AREA] = "Área",
    [UI_STR_PHOTO_ACT_MOVE] = "Mover",
    [UI_STR_PHOTO_ACT_LOOK] = "Olhar",
    [UI_STR_PHOTO_ACT_RISE] = "subir e descer",
    [UI_STR_PHOTO_ACT_CIRCLE] = "Circular",
    [UI_STR_PHOTO_ACT_NEARFAR] = "mais perto, mais longe e para os lados",
    [UI_STR_PHOTO_ACT_ZOOM] = NULL, /* the same as the English */
    [UI_STR_PHOTO_ACT_ROLL] = "rolar",
    [UI_STR_PHOTO_ACT_SWITCH] = "orbitar ou câmera livre",
    [UI_STR_PHOTO_ACT_SPEED] = "velocidade",
    [UI_STR_PHOTO_ACT_RESET] = "restaurar",
    [UI_STR_PHOTO_ACT_SAVE] = "salvar uma imagem",
    [UI_STR_PHOTO_ACT_HIDE] = "ocultar este painel (lembrar)",
    [UI_STR_PHOTO_ACT_BACK] = "voltar",
    [UI_STR_PHOTO_ACT_FILM] = "efeito de filme",
    [UI_STR_PHOTO_FILM_DEJA] = "Déjà vu",
    [UI_STR_PHOTO_MOUSE_PREFIX] = NULL, /* the same as the English */
    [UI_STR_PHOTO_UP] = "Cima",
    [UI_STR_PHOTO_DOWN] = "Baixo",
    [UI_STR_PHOTO_RIGHT] = "Direita",
    [UI_STR_SECTION_CHARACTERS] = "Customização de Personagem",
    [UI_STR_CHAR_ICO_SKIN] = "Ico: Pele",
    [UI_STR_CHAR_ICO_PONCHO_NAVY] = "Ico: Poncho, azul marinho",
    [UI_STR_CHAR_ICO_PONCHO_PINK] = "Ico: Poncho, rosa",
    [UI_STR_CHAR_ICO_PONCHO_LIGHT] = "Ico: Poncho, claro",
    [UI_STR_CHAR_ICO_PONCHO_DARK] = "Ico: Poncho, escuro",
    [UI_STR_CHAR_ICO_TUNIC] = "Ico: Túnica",
    [UI_STR_CHAR_ICO_SHORTS] = NULL, /* the same as the English */
    [UI_STR_CHAR_YORDA_SKIN] = "Yorda: Pele",
    [UI_STR_CHAR_YORDA_DRESS] = "Yorda: Vestido",
    [UI_STR_CHAR_RANDOMIZE] = "Aleatório",
    [UI_STR_CHAR_RESET] = "Restaurar para Original",
    [UI_STR_CHAR_TONE] = "Matiz %d", /* [cobra838] */
    [UI_STR_CHAR_NOTE_TITLE] = "As cores se aplicam ao jogo de uma vez.",
    [UI_STR_CHAR_NOTE_PAUSE] = "As cores mudam atrás deste menu conforme sua escolha.",
    [UI_STR_CHAR_NOTE_PACK] =
        "Um pacote de textura está ativado. Se houverem texturas para Ico e Yorda, elas substituem essas cores.",
    [UI_STR_COLOUR_RED] = "Vermelho",       /* [cobra838] */
    [UI_STR_COLOUR_CRIMSON] = "Carmesim",   /* [cobra838] */
    [UI_STR_COLOUR_ROSE] = "Rosa",          /* [cobra838] */
    [UI_STR_COLOUR_PINK] = "Rosa choque",   /* [cobra838] */
    [UI_STR_COLOUR_MAGENTA] = NULL,         /* empty in the CSV: English */
    [UI_STR_COLOUR_PLUM] = "Ameixa",        /* [cobra838] */
    [UI_STR_COLOUR_VIOLET] = "Violeta",     /* [cobra838] */
    [UI_STR_COLOUR_INDIGO] = "Índigo",      /* [cobra838] */
    [UI_STR_COLOUR_NAVY] = "Azul marinho",  /* [cobra838] */
    [UI_STR_COLOUR_BLUE] = "Azul",          /* [cobra838] */
    [UI_STR_COLOUR_SKY] = "Azul-céu",       /* [cobra838] */
    [UI_STR_COLOUR_TEAL] = "Azul-petróleo", /* [cobra838] */
    [UI_STR_COLOUR_CYAN] = "Ciano",         /* [cobra838] */
    [UI_STR_COLOUR_GREEN] = "Verde",        /* [cobra838] */
    [UI_STR_COLOUR_MOSS] = "Musgo",         /* [cobra838] */
    [UI_STR_COLOUR_OLIVE] = "Oliva",        /* [cobra838] */
    [UI_STR_COLOUR_GOLD] = "Dourado",       /* [cobra838] */
    [UI_STR_COLOUR_ORANGE] = "Laranja",     /* [cobra838] */
    [UI_STR_COLOUR_RUST] = "Ferrugem",      /* [cobra838] */
    [UI_STR_COLOUR_BROWN] = "Marrom",       /* [cobra838] */
    [UI_STR_COLOUR_SAND] = "Areia",         /* [cobra838] */
    [UI_STR_COLOUR_WHITE] = "Branco",       /* [cobra838] */
    [UI_STR_COLOUR_GREY] = "Cinza",         /* [cobra838] */
    [UI_STR_COLOUR_BLACK] = "Preto",        /* [cobra838] */
    [UI_STR_CHAR_SWITCH_YORDA] = "Mudar para Yorda",
    [UI_STR_CHAR_SWITCH_ICO] = "Mudar para Ico",
    [UI_STR_CHAR_HINT_COLOUR] = "Cor",
    [UI_STR_CHAR_HINT_CHARACTER] = "Personagem",
    [UI_STR_OPT_WINDOW_MODE] = "Modo de Janela",
    [UI_STR_VAL_WINDOWED] = "Em janela",
    [UI_STR_VAL_BORDERLESS] = "Sem bordas",
    [UI_STR_OPT_EFFECT_CINEMATIC_BARS] = "Barras cinematográficas",
    [UI_STR_OPT_FULL_PIXEL] = "Pixel cheio (pixels exatos da tela)", /* [cobra838] */
    [UI_STR_SECTION_GRAPHICS] = "Gráficos",
    [UI_STR_OPT_GRAPHICS_API] = "API Gráfica",
    [UI_STR_VAL_GRAPHICS_AUTOMATIC] = "Automático",
    [UI_STR_GRAPHICS_API_NOTE] =
        "Aplicar alterna agora; sem Aplicar, a escolha é usada na próxima inicialização.", /* [cobra838] */
    [UI_STR_OPT_GRAPHICS_ADAPTER] = "Placa de vídeo", /* [cobra838] */
    [UI_STR_GRAPHICS_ADAPTER_NOTE] =
        "Qual placa de vídeo desenha o jogo. Aplicar alterna agora; sem Aplicar, a escolha é usada na próxima inicialização.", /* [cobra838] */
    [UI_STR_GRAPHICS_ADAPTER_FAILED] =
        "A placa de vídeo %s não pôde iniciar; %s foi mantida", /* [cobra838] */
    [UI_STR_GRAPHICS_APPLY] = "Aplicar",
    [UI_STR_GRAPHICS_APPLY_NOTE] =
        "Aplica agora, sem sair do jogo. Uma pequena pausa é normal.",             /* [cobra838] */
    [UI_STR_GRAPHICS_KEEP_ASK] = "Manter estas configurações? Revertendo em %d s", /* [cobra838] */
    [UI_STR_GRAPHICS_KEEP] = "Manter",
    [UI_STR_GRAPHICS_REVERT] = "Reverter",
    [UI_STR_GRAPHICS_FAILED] =
        "O renderizador %s não pôde iniciar; %s foi mantido", /* [cobra838] */
    [UI_STR_SECTION_GPU_DRIVER] = "Driver Gráfico",
    [UI_STR_OPT_GPU_DRIVER] = NULL, /* the same as the English */
    [UI_STR_VAL_GPU_BUILTIN] = "Incorporado",
    [UI_STR_GPU_DRIVER_ADD] = "Adicionar um Driver",
    [UI_STR_GPU_DRIVER_REMOVE] = "Remover este Driver",
    [UI_STR_GPU_DRIVER_NOTE] =
        "Apenas para placas gráficas Adreno. Será aplicado na próxima inicialização.",
    [UI_STR_GPU_DRIVER_ADDED] = "Driver adicionado.",
    [UI_STR_GPU_DRIVER_BAD] = "Este arquivo não é um pacote de Drivers.",
    [UI_STR_GPU_DRIVER_REMOVED] = "Driver removido",
    [UI_STR_GPU_DRIVER_FAILED] =
        "O driver selecionado não iniciou na última execução. O driver nativo do dispositivo está em uso.",
    [UI_STR_GPU_DRIVER_NOSPACE] = "Não há espaço suficiente para adicionar o driver",
    [UI_STR_GPU_DRIVER_NO_PICKER] =
        "Não foi possível abrir o seletor de arquivos. Você pode copiar o arquivo zip do driver para a pasta drivers do jogo: %s", /* [cobra838] */
    [UI_STR_GPU_DRIVER_NO_PICKER_APP] =
        "Nenhum aplicativo neste dispositivo pode escolher um arquivo. Copie o arquivo zip do driver para a pasta drivers do jogo: %s", /* [cobra838] */
    [UI_STR_GPU_DRIVER_UNREADABLE] =
        "Não foi possível ler o arquivo selecionado. Copie-o primeiro para este dispositivo e tente selecioná-lo novamente.",
    [UI_STR_GPU_DRIVER_ADD_FILE] = "Adicionar da pasta de drivers",
    [UI_STR_GPU_DRIVER_CANCELLED] =
        "Nenhum arquivo foi escolhido. Se o seletor de arquivos não aparecer neste dispositivo, copie o arquivo zip do driver para a pasta drivers do jogo e use Adicionar da pasta de drivers: %s", /* [cobra838] */
    [UI_STR_GPU_DRIVER_NOTE_LOST] = NULL, /* empty in the CSV: English */
    [UI_STR_QUIT_GAME] = "Encerrar o jogo",
    [UI_STR_QUIT_GAME_CONFIRM] = "Encerrar o jogo?",
    [UI_STR_OPT_MOUSE_INVERT] = "Inverter mouse - Cima/Baixo",
    [UI_STR_OPT_MOUSE_SPEED] = "Velocidade da Câmera no Mouse",
    [UI_STR_OPT_MOUSE_RANGE] = "Alcance da câmera pelo mouse",
    [UI_STR_OPT_MOUSE_RETURN] = "Retorno automático da câmera",
    [UI_STR_VAL_RANGE_NORMAL] = NULL,  /* the same as the English */
    [UI_STR_VAL_RANGE_FULL] = "Total", /* [cobra838] */
    [UI_STR_VAL_INSTANT] = "Instantâneo",
    [UI_STR_OPT_DOOR_FIX] = "Correção da Porta do Refletor Leste",
    [UI_STR_OPT_DOOR_FIX_NOTE] =
        "Ao darem as mãos, Ico e Yorda não saem mais automaticamente pela mesma porta por onde entraram.",
    [UI_STR_OPT_YORDA_TRIPS] = NULL,      /* newer than the CSV: English */
    [UI_STR_OPT_YORDA_TRIPS_NOTE] = NULL, /* newer than the CSV: English */
    [UI_STR_OPT_NATURAL_CHAINS] = "Correntes Naturais",
    [UI_STR_OPT_NATURAL_CHAINS_NOTE] =
        "A corrente que desce para Ico mantém seus elos no tamanho correto durante toda a descida.",
    [UI_STR_OPT_SKIP_OPENING] = "Pular a abertura",
    [UI_STR_OPT_SKIP_OPENING_NOTE] =
        "O jogo inicia diretamente na tela de título, sem exibir o logotipo da Sony nem a cena de abertura. A alteração entra em vigor na próxima inicialização.",
    [UI_STR_OPT_SECOND_CARD] = NULL,           /* newer than the CSV: English */
    [UI_STR_OPT_SECOND_CARD_NOTE] = NULL,      /* newer than the CSV: English */
    [UI_STR_OPT_SHADOWS_TOGETHER] = NULL,      /* newer than the CSV: English */
    [UI_STR_OPT_SHADOWS_TOGETHER_NOTE] = NULL, /* newer than the CSV: English */
    [UI_STR_OPT_AA] = "Antisserrilhado",
    [UI_STR_OPT_AA_NOTE] =
        "Suaviza as bordas serrilhadas. A superamostragem renderiza 4 vezes (2×) ou 16 vezes (4×) mais pixels. Reduza se o contador de FPS chegar a 30.", /* [cobra838] */
    [UI_STR_OPT_DOWNSAMPLE] = "Redução de resolução",
    [UI_STR_VAL_DOWNSAMPLE_BOX] = "Para o tamanho original",
    [UI_STR_OPT_DOWNSAMPLE_NOTE] =
        NULL, /* longer than its space (181 bytes): English until shortened */
    [UI_STR_AA_OFF] = "Desligado",
    [UI_STR_AA_SSAA2] = "Superamostragem 2x",
    [UI_STR_AA_SSAA4] = "Superamostragem 4x",
    [UI_STR_AA_MSAA2] = NULL,                                /* the same as the English */
    [UI_STR_AA_MSAA4] = NULL,                                /* the same as the English */
    [UI_STR_AA_MSAA8] = NULL,                                /* the same as the English */
    [UI_STR_AA_UNAVAILABLE] = " (desativado: indisponível)", /* [cobra838] */
    [UI_STR_EXTRAS_STEAM] = "Adicionar à Steam",
    [UI_STR_STEAM_ADDED] = "Adicionado à Steam",
    [UI_STR_STEAM_ADDED_BODY] = "Inicie a Steam e procure por ICO\nna sua biblioteca.",
    [UI_STR_STEAM_EXISTS] = "Já está na sua biblioteca Steam",
    [UI_STR_STEAM_RUNNING] = "Feche a Steam primeiro",
    [UI_STR_STEAM_RUNNING_BODY] = "Tente novamente. No Steam Deck,\nuse o modo Desktop.",
    [UI_STR_STEAM_NOT_FOUND] = "Steam não encontrada",
    [UI_STR_STEAM_NOT_FOUND_BODY] = "Instale a Steam primeiro.",
    [UI_STR_STEAM_NO_USER] = "Nenhuma conta Steam encontrada",
    [UI_STR_STEAM_NO_USER_BODY] = "Entre na sua conta Steam uma vez\ne tente novamente.",
    [UI_STR_STEAM_FAILED] = "Não foi possível adicionar à Steam",
    [UI_STR_STEAM_FAILED_BODY] =
        "Não foi possível alterar a lista de atalhos.\nO README explica como adicioná-lo manualmente.",
    [UI_STR_EXTRAS_EXPORT_TEXTURES] = "Exportar texturas",
    [UI_STR_EXPORT_TEXTURES_STOP] = "Interromper exportação",
    [UI_STR_EXPORT_TEXTURES_NOTE] =
        "Para criadores de pacotes: salva todas as imagens do disco com seus respectivos nomes do PCSX2, na pasta de texturas do jogo.",
    [UI_STR_EXPORT_TEXTURES_PROGRESS] = "Exportando texturas: %d de %d",
    [UI_STR_EXPORT_TEXTURES_DONE] = "Texturas exportadas",
    [UI_STR_EXPORT_TEXTURES_DONE_BODY] = "%d imagens salvas em\n%s",
    [UI_STR_EXPORT_TEXTURES_STOPPED] = "Exportação interrompida",
    [UI_STR_EXPORT_TEXTURES_STOPPED_BODY] = "Exportação interrompida após %d imagens",
    [UI_STR_EXPORT_TEXTURES_FAILED] = "Não foi possível exportar as texturas",
    [UI_STR_EXPORT_TEXTURES_FAILED_BODY] = "Não foi possível gravar na pasta %s",
    [UI_STR_EXTRAS_BACKUP] = NULL,         /* newer than the CSV: English */
    [UI_STR_EXTRAS_RESTORE] = NULL,        /* newer than the CSV: English */
    [UI_STR_BACKUP_NOTE] = NULL,           /* newer than the CSV: English */
    [UI_STR_RESTORE_NOTE] = NULL,          /* newer than the CSV: English */
    [UI_STR_BACKUP_CHOOSING] = NULL,       /* newer than the CSV: English */
    [UI_STR_BACKUP_PROGRESS] = NULL,       /* newer than the CSV: English */
    [UI_STR_RESTORE_AGAIN] = NULL,         /* newer than the CSV: English */
    [UI_STR_RESTORE_CONFIRM_TITLE] = NULL, /* newer than the CSV: English */
    [UI_STR_RESTORE_CONFIRM_BODY] = NULL,  /* newer than the CSV: English */
    [UI_STR_RESTORE_CHOOSING] = NULL,      /* newer than the CSV: English */
    [UI_STR_RESTORE_READING] = NULL,       /* newer than the CSV: English */
    [UI_STR_RESTORE_PROGRESS] = NULL,      /* newer than the CSV: English */
    [UI_STR_RESTORE_THIS] = NULL,          /* newer than the CSV: English */
    [UI_STR_RESTORE_ASK_NOTE] = NULL,      /* newer than the CSV: English */
    [UI_STR_RESTORE_ASK_TITLE] = NULL,     /* newer than the CSV: English */
    [UI_STR_RESTORE_VERSION] = NULL,       /* newer than the CSV: English */
    [UI_STR_RESTORE_SAVED] = NULL,         /* newer than the CSV: English */
    [UI_STR_RESTORE_DISCS] = NULL,         /* newer than the CSV: English */
    [UI_STR_RESTORE_OTHER_DISC] = NULL,    /* newer than the CSV: English */
    [UI_STR_RESTORE_PRESS] = NULL,         /* newer than the CSV: English */
    [UI_STR_BACKUP_DONE] = NULL,           /* newer than the CSV: English */
    [UI_STR_BACKUP_DONE_BODY] = NULL,      /* newer than the CSV: English */
    [UI_STR_BACKUP_FAILED] = NULL,         /* newer than the CSV: English */
    [UI_STR_RESTORE_DONE] = NULL,          /* newer than the CSV: English */
    [UI_STR_RESTORE_DONE_RESTART] = NULL,  /* newer than the CSV: English */
    [UI_STR_RESTORE_DONE_CLOSE] = NULL,    /* newer than the CSV: English */
    [UI_STR_RESTORE_FAILED] = NULL,        /* newer than the CSV: English */
    [UI_STR_BACKUP_WHY_BUSY] = NULL,       /* newer than the CSV: English */
    [UI_STR_BACKUP_WHY_NOTHING] = NULL,    /* newer than the CSV: English */
    [UI_STR_BACKUP_WHY_WRITE] = NULL,      /* newer than the CSV: English */
    [UI_STR_BACKUP_WHY_READ] = NULL,       /* newer than the CSV: English */
    [UI_STR_BACKUP_WHY_NOT_BACKUP] = NULL, /* newer than the CSV: English */
    [UI_STR_BACKUP_WHY_NEWER] = NULL,      /* newer than the CSV: English */
    [UI_STR_BACKUP_WHY_DAMAGED] = NULL,    /* newer than the CSV: English */
    [UI_STR_BACKUP_WHY_UNSAFE] = NULL,     /* newer than the CSV: English */
    [UI_STR_BACKUP_WHY_TOO_BIG] = NULL,    /* newer than the CSV: English */
    [UI_STR_BACKUP_WHY_NO_DIALOG] = NULL,  /* newer than the CSV: English */
    [UI_STR_BACKUP_WHY_NO_SPACE] = NULL,   /* newer than the CSV: English */
    [UI_STR_OPT_EFFECT_WATER] = "Reflexos da água",
    [UI_STR_VAL_SHARP] = "Nítidos",
    [UI_STR_EFFECT_WATER_NOTE] =
        "Nítidos renderiza os reflexos na água na resolução selecionada. \"Original\" mantém os reflexos suaves do PS2.", /* [cobra838] */
    [UI_STR_VAL_SMOOTH] = "Suave",
    [UI_STR_OPT_SHOW_FPS] = "Contador de FPS",
    [UI_STR_EFFECT_MOTION_BLUR_NOTE] =
        NULL,                /* longer than its space (171 bytes): English until shortened */
    [UI_STR_LANG_KO] = NULL, /* the same as the English */
    [UI_STR_LANG_ZH] = NULL, /* the same as the English */
    [UI_STR_SECTION_TOUCH] = "Tela sensível ao toque",
    [UI_STR_OPT_TOUCH_EDIT] = "Editar layout",
    [UI_STR_OPT_TOUCH_SNAP] = "Ajustar à grade",
    [UI_STR_OPT_TOUCH_GRID] = "Tamanho da grade",
    [UI_STR_VAL_MM] = NULL, /* the same as the English */
    [UI_STR_TOUCH_RESET] = "Restaurar layout",
    [UI_STR_TOUCH_RESET_ALL] = "Restaurar todos os layouts",
    [UI_STR_TOUCH_EDIT_NOTE] =
        "Mova e redimensione os botões na tela. Ao concluir, as alterações serão salvas.",
    [UI_STR_TOUCH_EDIT_LOCKED] = "Ative os controles por toque para editar o layout.",
    [UI_STR_TED_DONE] = "Concluir",
    [UI_STR_TED_UNDO] = "Desfazer",
    [UI_STR_TED_RESET] = "Restaurar",
    [UI_STR_TED_SNAP] = "Ajustar à grade",
    [UI_STR_TED_GRID] = "Grade",
    [UI_STR_TED_SIZE] = "Tamanho",
    [UI_STR_TED_OPACITY] = "Opacidade",
    [UI_STR_TED_ALL] = "Todos",
    [UI_STR_TED_THIS_SCREEN] = "Esta tela",
    [UI_STR_TED_ALL_SCREENS] = "Todas as telas",
    [UI_STR_TED_CANCEL] = "Cancelar",
    [UI_STR_TED_HIDDEN] = "Oculto",
    [UI_STR_TED_OFF] = "Desativado",
    [UI_STR_TED_STICK] = "Analógico",
    [UI_STR_TED_LOOK] = "Controle da câmera",
    [UI_STR_TED_HINT] =
        "Toque em um botão para selecioná-lo. Arraste para movê-lo. Faça o movimento de pinça para redimensioná-lo.",
    [UI_STR_TED_OVERLAP] = "Botões sobrepostos: o que for desenhado primeiro terá prioridade.",
    [UI_STR_TED_COVERED] = "Um botão não pode cobrir o centro de outro.",
    [UI_STR_ISO_NEED_SPACE] =
        "Esta imagem de disco precisa de %llu.%llu GB livres em %s; %llu.%llu GB está livre.\nLibere espaço (aplicativos, vídeos ou downloads) e inicie o jogo novamente.", /* [cobra838] */
    [UI_STR_ISO_COPY_FAILED] =
        "Não foi possível copiar a imagem de disco para %s.\nVerifique se há aproximadamente %llu.%llu GB livres e se o arquivo ainda está lá. Em seguida, reinicie o jogo. Consulte o registro para saber o motivo.",
    [UI_STR_ISO_COPYING] = "Copiando a imagem de disco para o aplicativo",
    [UI_STR_ISO_COPYING_LONG] =
        "Copiando a imagem de disco para o aplicativo. O arquivo é grande, então isso levará alguns minutos.",
    [UI_STR_SECTION_YORDA] = NULL,     /* newer than the CSV: English */
    [UI_STR_SECTION_SHADOWS] = NULL,   /* newer than the CSV: English */
    [UI_STR_SECTION_CUTSCENES] = NULL, /* newer than the CSV: English */
    [UI_STR_OPT_VAR_TOUGHNESS] = "Resistência das sombras",
    [UI_STR_OPT_VAR_TOUGHNESS_NOTE] =
        NULL, /* longer than its space (184 bytes): English until shortened */
    [UI_STR_VAL_VAR_AMERICAN] = "Americana",
    [UI_STR_VAL_VAR_NOT_TOUGHER] = "Sem aumento de resistência",
    [UI_STR_OPT_VAR_ONE_HIT] = "Um golpe por ataque",
    [UI_STR_OPT_VAR_ONE_HIT_NOTE] = "Cada golpe da arma de Ico só pode atingir uma sombra uma vez.",
    [UI_STR_OPT_VAR_NEVER_DODGE] = "Sombras não se esquivam",
    [UI_STR_OPT_VAR_NEVER_DODGE_NOTE] = "As sombras nunca desviam dos golpes de Ico.",
    [UI_STR_OPT_VAR_AGGRESSION] = "Agressividade das sombras",
    [UI_STR_VAL_VAR_AGG_OWN] = "Original",
    [UI_STR_VAL_VAR_AGG_BOLD_PATIENT] = "Ousadas e pacientes",
    [UI_STR_VAL_VAR_AGG_BOLD_QUICK] = "Ousadas e rápidas",
    [UI_STR_VAL_VAR_AGG_WARY_PATIENT] = "Cautelosas e pacientes",
    [UI_STR_VAL_VAR_AGG_WARY_QUICK] = "Cautelosas e rápidas",
    [UI_STR_OPT_VAR_AGGRESSION_NOTE] =
        NULL, /* longer than its space (172 bytes): English until shortened */
    [UI_STR_OPT_VAR_NO_HINTS] = "Sem dicas",
    [UI_STR_OPT_VAR_NO_HINTS_NOTE] =
        "Yorda não dará dicas sobre para onde ir, como na versão norte-americana do disco.",
    [UI_STR_OPT_VAR_BARRIER] = "Barreira da Rainha",
    [UI_STR_VAL_VAR_HITS_5] = "5 golpes",
    [UI_STR_VAL_VAR_HITS_7] = "7 golpes",
    [UI_STR_OPT_VAR_BARRIER_NOTE] =
        "Quantos golpes são necessários para destruir a barreira da Rainha.",
    [UI_STR_OPT_VAR_NO_LOOK] = "Sem movimento de câmera nas cenas",
    [UI_STR_OPT_VAR_NO_LOOK_NOTE] =
        "O analógico direito não controlará mais a câmera durante as cenas. As conquistas continuam ativadas.",
    [UI_STR_OPT_VAR_IGNORE_CAMS] = "Ignorar câmeras das cenas",
    [UI_STR_OPT_VAR_IGNORE_CAMS_NOTE] =
        "As cenas podem apresentar problemas visuais; o jogo pode mostrar lugares que nunca deveriam ser vistos. Desativa as conquistas.", /* [cobra838] */
    [UI_STR_OPT_VAR_DEBUG_MENU] = "Menu de depuração",
    [UI_STR_OPT_VAR_DEBUG_MENU_NOTE] =
        "Menu de depuração original do jogo, destinado aos desenvolvedores. Desativa as conquistas.",
    [UI_STR_OPT_VAR_TOUGHNESS_NOTE_DEMO] =
        NULL, /* longer than its space (276 bytes): English until shortened */
    [UI_STR_OPT_VAR_NO_HINTS_NOTE_2001] =
        "Esta versão do jogo não oferece dicas, portanto esta opção não altera nada.",
    [UI_STR_OPT_VAR_NO_LOOK_NOTE_2001] =
        "Esta versão do jogo não permite movimentar a câmera durante as cenas, portanto esta opção não altera nada. As conquistas continuam ativadas.",
    [UI_STR_GPU_HANG_TURNIP] =
        "No driver Qualcomm nativo deste dispositivo, um pacote de driver Turnip pode ajudar: adicione-o em Opções > Driver gráfico > Adicionar driver. Será utilizado na próxima inicialização.", /* [cobra838] */
    [UI_STR_STATES] = "Save States",
    [UI_STR_STATES_SLOT] = NULL, /* empty in the CSV: English */
    [UI_STR_STATES_EMPTY] = "Vazio",
    [UI_STR_STATES_ACT_SAVE] = "Salvar",
    [UI_STR_STATES_ACT_LOAD] = "Carregar",
    [UI_STR_STATES_ACT_DELETE] = "Excluir",
    [UI_STR_STATES_CONFIRM] = "Pressione novamente para %s",
    [UI_STR_STATES_SAVED] = "Estado salvo no espaço %d",
    [UI_STR_STATES_LOADED] = "Estado carregado do espaço %d",
    [UI_STR_STATES_DELETED] = "Espaço %d liberado",
    [UI_STR_STATES_NOTE] = NULL, /* English changed since the CSV */
    [UI_STR_STATES_PAUSE_FIRST] = "Pause o jogo para criar um save state",
    [UI_STR_STATES_REFUSED_BUILD] = "Salvo em outra versão do jogo",
    [UI_STR_STATES_REFUSED_DISC] = "Salvo a partir de outro disco",
    [UI_STR_STATES_REFUSED_SETTINGS] = "Uma configuração de jogo é diferente: %s",
    [UI_STR_STATES_REFUSED_BUSY] = "Aguarde: o jogo está carregando ou salvando",
    [UI_STR_STATES_REFUSED_CHECK] = "Não foi possível restaurar o save state. O jogo continua",
    [UI_STR_STATES_OFF] = "Save states desativados nesta versão",
    [UI_STR_STATES_TITLE_HINT] = "Save states são gerenciados no menu de pausa",
    [UI_STR_STATES_SAVE_FAILED] = "Não foi possível criar o save state",
    [UI_STR_STATES_XB_OLD] = NULL,    /* newer than the CSV: English */
    [UI_STR_STATES_XB_CODE] = NULL,   /* newer than the CSV: English */
    [UI_STR_STATES_XB_DATA] = NULL,   /* newer than the CSV: English */
    [UI_STR_STATES_XB_PINNED] = NULL, /* newer than the CSV: English */
    [UI_STR_OPT_EFFECT_GLOW_SMOOTH] = "Suavização do brilho entre quadros",
    [UI_STR_EFFECT_GLOW_SMOOTH_NOTE] =
        NULL, /* the CSV has only the first half of the note: English */
    [UI_STR_EFFECT_GLOW_SMOOTH_LOCKED] =
        "Só tem efeito acima de Taxa de quadros: Original, com o brilho ativado.",
    [UI_STR_OPT_EFFECT_MOTION_BLUR_STRENGTH] = NULL,  /* newer than the translation: English */
    [UI_STR_EFFECT_MOTION_BLUR_STRENGTH_NOTE] = NULL, /* newer than the translation: English */
    [UI_STR_SECTION_UNUSED] = NULL,                   /* newer than the CSV: English */
    [UI_STR_UNUSED_ARENA] = NULL,                     /* newer than the CSV: English */
    [UI_STR_UNUSED_ARENA_NOTE] = NULL,                /* newer than the CSV: English */
    [UI_STR_UNUSED_CLIMB] = NULL,                     /* newer than the CSV: English */
    [UI_STR_UNUSED_CLIMB_NOTE] = NULL,                /* newer than the CSV: English */
    [UI_STR_UNUSED_MOVIE] = NULL,                     /* newer than the CSV: English */
    [UI_STR_UNUSED_MOVIE_NOTE] = NULL,                /* newer than the CSV: English */
    [UI_STR_UNUSED_ART] = NULL,                       /* newer than the CSV: English */
    [UI_STR_UNUSED_ART_NOTE] = NULL,                  /* newer than the CSV: English */
    [UI_STR_UNUSED_ART_VER] = NULL,                   /* newer than the CSV: English */
    [UI_STR_UNUSED_ART_START] = NULL,                 /* newer than the CSV: English */
    [UI_STR_UNUSED_ART_COPYRIGHT] = NULL,             /* newer than the CSV: English */
    [UI_STR_STATES_REFUSED_UNUSED] = NULL,            /* newer than the CSV: English */
    [UI_STR_LANG_RU_BOBTTZ] = "Русский (bob-ttz)",
    [UI_STR_LANG_RU_COBRA838] = "Русский (cobra838)",
    [UI_STR_LANG_PT_BR] = "Português (Brasil) (ichagas7)",
    /* ---- new since v0.10.2: English until translated ---- */
    /* Written by the maintainer, please check: UI_STR_NEWGAME_START, UI_STR_NEWGAME_START_SCREEN (above). */
    [UI_STR_VAL_ALWAYS_SCENES] = NULL,         /* "Always, Start only in scenes" */
    [UI_STR_OPT_UPSCALE] = NULL,               /* "Upscaling" */
    [UI_STR_VAL_UPSCALE_FSR1] = NULL,          /* "FSR 1.0" */
    [UI_STR_VAL_UPSCALE_FSR3] = NULL,          /* "FSR 3.1" */
    [UI_STR_OPT_UPSCALE_QUALITY] = NULL,       /* "Upscaling quality" */
    [UI_STR_VAL_UPSCALE_Q_NATIVE] = NULL,      /* "Native AA" */
    [UI_STR_VAL_UPSCALE_Q_QUALITY] = NULL,     /* "Quality" */
    [UI_STR_VAL_UPSCALE_Q_BALANCED] = NULL,    /* "Balanced" */
    [UI_STR_VAL_UPSCALE_Q_PERFORMANCE] = NULL, /* "Performance" */
    [UI_STR_OPT_UPSCALE_SHARPNESS] = NULL,     /* "Sharpness" */
    /* "FSR (AMD) sharpens a picture drawn smaller than the screen. Not with the CRT filter or at Window. 3.1: no Downsampling; anti-aliasing off." */
    [UI_STR_OPT_UPSCALE_NOTE] = NULL,
    [UI_STR_AA_FSR3] = NULL,            /* " (off: FSR 3.1)" */
    [UI_STR_EXTRAS_DESKTOP] = NULL,     /* "Add to the applications menu" */
    [UI_STR_DESKTOP_ADDED] = NULL,      /* "Added to the menu" */
    [UI_STR_DESKTOP_ADDED_BODY] = NULL, /* "The game is in your\napplications menu." */
    [UI_STR_DESKTOP_FAILED] = NULL,     /* "Could not add to the menu" */
    /* "The menu entry could not be\nwritten to your user folder." */
    [UI_STR_DESKTOP_FAILED_BODY] = NULL,
    [UI_STR_SECTION_COMBAT] = NULL,     /* "Combat" */
    [UI_STR_OPT_VAR_AUTO_GUARD] = NULL, /* "Automatic guard" */
    /* "Off: Ico no longer blocks a Shadow’s attack from the front; it lands as an attack from behind does." */
    [UI_STR_OPT_VAR_AUTO_GUARD_NOTE] = NULL,
    [UI_STR_OPT_VAR_ATTACK_TURN] = NULL, /* "Turn to face shadows" */
    /* "Off: Ico swings straight ahead instead of turning toward the nearest Shadow; the swing’s reach is unchanged." */
    [UI_STR_OPT_VAR_ATTACK_TURN_NOTE] = NULL,
    [UI_STR_OPT_VAR_SHADOWS_WAIT_DOWN] = NULL, /* "Shadows wait while Ico is down" */
    /* "Off: the Shadows no longer hold back while Ico is knocked down, and their swings land while he is on the ground and getting up." */
    [UI_STR_OPT_VAR_SHADOWS_WAIT_DOWN_NOTE] = NULL,
    [UI_STR_OPT_VAR_SWORD_COLOUR] = NULL, /* "Light sword colour" */
    /* "Red: the light sword leaves a red trail, as its unused twin in the game does; nothing else changes. From the next room." */
    [UI_STR_OPT_VAR_SWORD_COLOUR_NOTE] = NULL,
    [UI_STR_OPT_VAR_WEAPON] = NULL, /* "Stick" */
    /* "Insect net: each stick is the game’s unused insect net; it cannot be swung or lit and never hurts a Shadow. From the next room." */
    [UI_STR_OPT_VAR_WEAPON_NOTE] = NULL,
    [UI_STR_VAL_VAR_INSECT_NET] = NULL,    /* "Insect net" */
    [UI_STR_VAL_VAR_THIRD_JOURNEY] = NULL, /* "Third journey" */
    /* "Shadows as tough as on a third playthrough, which the game never reaches (twice the first journey’s)." */
    [UI_STR_OPT_VAR_TOUGHNESS_NOTE_THIRD] = NULL,
    [UI_STR_OPT_VAR_PEACEFUL] = NULL, /* "Peaceful shadows" */
    /* "On: Shadows stay where they appear and never go after Ico or Yorda." */
    [UI_STR_OPT_VAR_PEACEFUL_NOTE] = NULL,
    [UI_STR_OPT_VAR_CHAIN_SPEED] = NULL, /* "Chain swing speed" */
    /* "How fast a chain swings when Ico hangs on it and swings it. 1 is the slowest, 10 the fastest, 4 the game’s own. Achievements stay on." */
    [UI_STR_OPT_VAR_CHAIN_SPEED_NOTE] = NULL,
    [UI_STR_OPT_VAR_CHAIN_DAMPING] = NULL, /* "Chain swing slowdown" */
    /* "How quickly a swing dies down while Ico is not swinging the chain. 1 is the slowest, 10 the quickest, 4 the game’s own. Achievements stay on." */
    [UI_STR_OPT_VAR_CHAIN_DAMPING_NOTE] = NULL,
    [UI_STR_VAL_VAR_CHAIN_ORIGINAL] = NULL, /* "4 (original)" */
    [UI_STR_OPT_FREE_MOVEMENT] = NULL,      /* "Free movement" */
    /* "Press L2 to fly Ico with the sticks: the left stick moves him, the right stick goes up and down, L3 sets him on the floor. Switches achievements off." */
    [UI_STR_OPT_FREE_MOVEMENT_NOTE] = NULL,
    [UI_STR_REMAP_ASK_HINT] = NULL, /* "Ask Yorda for a hint" */
    /* ---- new since v0.11.2: English until translated ---- */
    [UI_STR_DIFF] = NULL, /* "Difficulty" */
    /* "Sets the Shadows, the rescue time and the rules for this new journey. The save keeps the choice." */
    [UI_STR_DIFF_NOTE] = NULL,
    [UI_STR_DIFF_EASY] = NULL,   /* "Easy" */
    [UI_STR_DIFF_NORMAL] = NULL, /* "Normal" */
    [UI_STR_DIFF_HARD] = NULL,   /* "Hard" */
    [UI_STR_DIFF_CUSTOM] = NULL, /* "Custom" */
    /* "Slower, gentler Shadows that never take Yorda and never dodge, and more time to rescue her." */
    [UI_STR_DIFF_EASY_NOTE] = NULL,
    [UI_STR_DIFF_NORMAL_NOTE] = NULL, /* "The game as it shipped." */
    /* "Faster, tougher Shadows that attack together, no automatic guard, less time to rescue Yorda, game over after 3 knock-downs in a room." */
    [UI_STR_DIFF_HARD_NOTE] = NULL,
    [UI_STR_DIFF_CUSTOM_NOTE] = NULL, /* "Your own rules. Change every row under Customize." */
    [UI_STR_DIFF_CUSTOMIZE] = NULL,   /* "Customize" */
    /* "Change each rule on its own. A mix that matches no preset shows as Custom." */
    [UI_STR_DIFF_CUSTOMIZE_NOTE] = NULL,
    [UI_STR_DIFF_RULES] = NULL,      /* "Rules" */
    [UI_STR_DIFF_NO_SHADOWS] = NULL, /* "No Shadows" */
    /* "On: no Shadows ever appear. The story still plays out as usual." */
    [UI_STR_DIFF_NO_SHADOWS_NOTE] = NULL,
    [UI_STR_DIFF_KNOCK_LIMIT] = NULL, /* "Knock-down limit" */
    /* "A game over once Ico has been knocked down this many times. Off sets no limit." */
    [UI_STR_DIFF_KNOCK_LIMIT_NOTE] = NULL,
    [UI_STR_DIFF_KNOCK_SCOPE] = NULL, /* "Knock-downs count" */
    /* "Whether knock-downs are counted in each room (the count starts again when Ico enters a room) or across the whole journey. Does nothing while the limit is Off." */
    [UI_STR_DIFF_KNOCK_SCOPE_NOTE] = NULL,
    [UI_STR_DIFF_SCOPE_ROOM] = NULL,    /* "Each room" */
    [UI_STR_DIFF_SCOPE_JOURNEY] = NULL, /* "Whole journey" */
    [UI_STR_DIFF_PERMADEATH] = NULL,    /* "Permadeath" */
    /* "On: any game over ends this journey. There is no Continue, and the save can no longer be loaded." */
    [UI_STR_DIFF_PERMADEATH_NOTE] = NULL,
    [UI_STR_DIFF_LOCKED_NOTE] = NULL, /* "Chosen when the journey began" */
    [UI_STR_DIFF_LINE] = NULL,        /* "Difficulty" */
    [UI_STR_DIFF_LINK] = NULL,        /* "Difficulty" */
    [UI_STR_DIFF_TOAST_ENDED] = NULL, /* "This journey has ended." */
    [UI_STR_DIFF_TOAST_KNOCK] = NULL, /* "Knocked down %d of %d" */
    /* "Save states cannot be loaded on a Permadeath journey." */
    [UI_STR_DIFF_STATES_LOCKED] = NULL,
    [UI_STR_DIFF_WORD_PERMADEATH] = NULL, /* "Permadeath" */
};
