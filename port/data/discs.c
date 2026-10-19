/*
 * port/data/discs.c
 *
 * The discs this program plays (archive.h ico_discs, in config/regions.txt
 * order): their ids, boot files,
 * verification values and DATA.DF manifests, which the extractor
 * (extract.c) accepts an image by and the libcdvd host (cdvd_host.c)
 * reports the disc type from. Plain data and look-ups, no other part of
 * the port.
 */
#include "archive.h"

#include <stddef.h>
#include <string.h>

/* --- the DATA.DF manifests -----------------------------------------------------
 * Each disc's DATA.DF: its size, its directory's member count and the
 * CRC-32 (zlib) of the directory ({count, {name[32], offset, size} x count},
 * 4 + 40 * count bytes), then each member's offset, size and CRC-32 in
 * directory order. Hashes and structure only: no names and no bytes of the
 * file (docs/LEGAL.md). Regenerate with `archive_test manifest --region
 * <slug> <iso>`, whose output replaces the block between that region's
 * BEGIN and END lines (the ctest archive_manifest_<slug> compares them). */

/* BEGIN DATA.DF MANIFEST pal */
#define DF_PAL_SIZE 867184640u
#define DF_PAL_COUNT 193u
#define DF_PAL_DIR_CRC 0xfb82eea7u
static const IcoDfManifestEntry dfMembersPal[] = {
    {8192u, 2365389u, 0xa7442fa6u},        {2373632u, 1256365u, 0xaaf763a3u},
    {3631104u, 4001634u, 0xe7b52d42u},     {7632896u, 51919u, 0x5f42c211u},
    {7686144u, 1928900u, 0x5cbd5400u},     {9615360u, 7487547u, 0x0280831au},
    {17104896u, 3010021u, 0x188be14cu},    {20115456u, 3403790u, 0x0691a3b8u},
    {23521280u, 4527299u, 0xfd10d6f2u},    {28049408u, 3539794u, 0x68f3d3e4u},
    {31590400u, 3786639u, 0x4d9de7b6u},    {35377152u, 3351457u, 0x6d4681b9u},
    {38729728u, 2385789u, 0xba823356u},    {41115648u, 4604019u, 0x915e5a24u},
    {45721600u, 2300650u, 0x06a090b3u},    {48023552u, 1861159u, 0xfc9f2615u},
    {49885184u, 1716793u, 0x95a7af30u},    {51603456u, 2363442u, 0x2dbf667cu},
    {53968896u, 4343953u, 0x58fe4e5du},    {58314752u, 2728365u, 0x26fbdb14u},
    {61044736u, 2789793u, 0xfacf318fu},    {63836160u, 3005291u, 0x4e457741u},
    {66842624u, 4908028u, 0xd081d8f3u},    {71751680u, 4146492u, 0x482524ccu},
    {75898880u, 3554485u, 0xbb873037u},    {79454208u, 770114u, 0x0c258f86u},
    {80226304u, 36699u, 0xaf8823bdu},      {80263168u, 3664632u, 0xea524198u},
    {83929088u, 3229179u, 0x61c42de7u},    {87158784u, 4074014u, 0xd6027944u},
    {91234304u, 4338131u, 0xddff44ecu},    {95574016u, 4908985u, 0x838e669bu},
    {100483072u, 3965297u, 0x71f1fd1cu},   {104450048u, 4342154u, 0x200842c0u},
    {108793856u, 4313146u, 0x893950b0u},   {113108992u, 4089014u, 0xf93f4330u},
    {117198848u, 3963119u, 0xf58f38d5u},   {121163776u, 4407795u, 0x289f146fu},
    {125573120u, 4003169u, 0x84e3572cu},   {129576960u, 4103202u, 0x73e43f03u},
    {133681152u, 4531007u, 0x8015ac33u},   {138213376u, 4597403u, 0xca9e701au},
    {142811136u, 5008097u, 0xa5f1d5b7u},   {147820544u, 4926587u, 0x873f1abau},
    {152748032u, 4610944u, 0x9184f9dfu},   {157360128u, 4686228u, 0x26102a8fu},
    {162048000u, 4591591u, 0x88055934u},   {166639616u, 3309224u, 0xc77f5ea9u},
    {169949184u, 3192736u, 0x7f5df611u},   {173142016u, 5037639u, 0xbd27a677u},
    {178180096u, 2855484u, 0xb20b56c7u},   {181037056u, 4384838u, 0x8aa36005u},
    {185423872u, 2818732u, 0xc3702263u},   {188243968u, 3796942u, 0xa472c950u},
    {192040960u, 4946719u, 0x97f86af3u},   {196988928u, 4074165u, 0xe73bbe82u},
    {201064448u, 4728586u, 0x26c09827u},   {205793280u, 4015760u, 0x0527ab61u},
    {209809408u, 3576896u, 0x7feb7869u},   {213387264u, 4291451u, 0x6acfc99cu},
    {217679872u, 4375432u, 0xbdf95004u},   {222056448u, 3408528u, 0x866a2a4eu},
    {225466368u, 3356859u, 0xf7f631c2u},   {228825088u, 4234068u, 0x67c873f8u},
    {233060352u, 3078221u, 0xafb0e023u},   {236140544u, 2786295u, 0x9bd81444u},
    {238927872u, 4528952u, 0x377874ceu},   {243458048u, 3177701u, 0x3eea6b4cu},
    {246636544u, 95624u, 0xcdb0a477u},     {246732800u, 1277952u, 0x0710d8feu},
    {248010752u, 4337664u, 0xec562dfcu},   {252348416u, 2375680u, 0xdb8bcedbu},
    {254724096u, 4538368u, 0x6e6c79b0u},   {259262464u, 1028096u, 0xa4020997u},
    {260290560u, 901120u, 0xa4223d4bu},    {261191680u, 901120u, 0x08660208u},
    {262092800u, 483328u, 0x9b3142f9u},    {262576128u, 1208320u, 0xe434981du},
    {263784448u, 3125248u, 0xfd8319cdu},   {266909696u, 921600u, 0x0ca4288au},
    {267831296u, 983040u, 0x52dbb45du},    {268814336u, 987136u, 0x82ffa512u},
    {269801472u, 1036288u, 0x24df6266u},   {270837760u, 1175552u, 0x51c61723u},
    {272013312u, 1724416u, 0x28241fd3u},   {273737728u, 1728512u, 0xcb802602u},
    {275466240u, 1269760u, 0x1a63ee1du},   {276736000u, 974848u, 0x104aa807u},
    {277710848u, 1007616u, 0x67b15bfdu},   {278718464u, 4206592u, 0x8de6edacu},
    {282925056u, 1437696u, 0x2beb372bu},   {284362752u, 905216u, 0xcfd8aee6u},
    {285267968u, 5005312u, 0x06a51956u},   {290273280u, 2277376u, 0x9260deb0u},
    {292550656u, 3289088u, 0x96839a46u},   {295839744u, 6283264u, 0x738fa681u},
    {302123008u, 3088384u, 0xd1eaa2f2u},   {305211392u, 1150976u, 0xadabf5d6u},
    {306362368u, 2539520u, 0xf3cce748u},   {308901888u, 13762560u, 0x68ab2d00u},
    {322664448u, 598016u, 0x10f5ecbeu},    {323262464u, 10067968u, 0xce38883fu},
    {333330432u, 1642496u, 0x23a7fef5u},   {334972928u, 847872u, 0x0e23ccdfu},
    {335820800u, 950272u, 0xfae89fd8u},    {336771072u, 3026944u, 0x8eee62adu},
    {339798016u, 1335296u, 0x401d91e6u},   {341133312u, 1355776u, 0xa4fef056u},
    {342489088u, 1200128u, 0xa2747422u},   {343689216u, 1040384u, 0xcc242a09u},
    {344729600u, 1310720u, 0x59256064u},   {346040320u, 757760u, 0xcc1d0d17u},
    {346798080u, 630784u, 0x32381d70u},    {347428864u, 802816u, 0xb0558cadu},
    {348231680u, 755712u, 0x8f8c0382u},    {348987392u, 831488u, 0x5c12a114u},
    {349818880u, 827392u, 0x18c61163u},    {350646272u, 847872u, 0xb0e04b4du},
    {351494144u, 1400832u, 0x4836d5c0u},   {352894976u, 1392640u, 0x2bede058u},
    {354287616u, 765952u, 0x42235eddu},    {355053568u, 1060864u, 0xabdcfc3au},
    {356114432u, 708608u, 0x34988651u},    {356823040u, 720896u, 0xa4791c6eu},
    {357543936u, 716800u, 0xaa4a7b6cu},    {358260736u, 872448u, 0x93c8dfdcu},
    {359133184u, 1171456u, 0xd8c6ce32u},   {360304640u, 913408u, 0xc721cf89u},
    {361218048u, 888832u, 0x33f47f19u},    {362106880u, 1232896u, 0x55da9176u},
    {363339776u, 942080u, 0xc38709c2u},    {364281856u, 655360u, 0x41704791u},
    {364937216u, 696320u, 0x3e551bdbu},    {365633536u, 712704u, 0x176e9058u},
    {366346240u, 630784u, 0xeb013728u},    {366977024u, 700416u, 0xb6a4a501u},
    {367677440u, 774144u, 0x3e071182u},    {368451584u, 798720u, 0x04c79688u},
    {369250304u, 786432u, 0xafbfa914u},    {370036736u, 827392u, 0x30cd6303u},
    {370864128u, 626688u, 0xc0ff345bu},    {371490816u, 634880u, 0xfd657b3au},
    {372125696u, 679936u, 0x1099d110u},    {372805632u, 2478080u, 0x70245be9u},
    {375283712u, 5013504u, 0xe8981923u},   {380297216u, 561152u, 0xad570c36u},
    {380858368u, 835584u, 0x04ed0debu},    {381693952u, 827392u, 0xd2cbdf1fu},
    {382521344u, 671744u, 0xc0b23345u},    {383193088u, 651264u, 0x6069ae9au},
    {383844352u, 4538368u, 0x071cc158u},   {388382720u, 454656u, 0xaa0f4915u},
    {388837376u, 503808u, 0xb852dda8u},    {389341184u, 471040u, 0x9080b7b4u},
    {389812224u, 458752u, 0x4f75e902u},    {390270976u, 130301956u, 0xd532ee7au},
    {520574976u, 130334724u, 0xc48f9011u}, {650911744u, 2815232u, 0x3aaa45e2u},
    {653727744u, 687232u, 0xa2e600cau},    {654415872u, 964u, 0xe5fa0c6bu},
    {654417920u, 2662400u, 0x34c81911u},   {657080320u, 3022848u, 0xfe2b76d4u},
    {660103168u, 3616768u, 0xacf3f715u},   {663719936u, 6213632u, 0x474ac58fu},
    {669933568u, 1789952u, 0x437d572au},   {671723520u, 4202496u, 0x17b056d2u},
    {675926016u, 1089536u, 0xe457c5a6u},   {677015552u, 876544u, 0x575cf3e7u},
    {677892096u, 1811680u, 0xf5e5792eu},   {679704576u, 3567616u, 0x848d1aefu},
    {683272192u, 2075320u, 0xe8882aa8u},   {685348864u, 2887680u, 0xc0396b7eu},
    {688236544u, 2688000u, 0x73e284f7u},   {690925568u, 4440064u, 0x00e7c349u},
    {695365632u, 725760u, 0x25bf4f45u},    {696092672u, 3567616u, 0x848d1aefu},
    {699660288u, 2961408u, 0x62da9193u},   {702621696u, 1467648u, 0x5ab03c76u},
    {704090112u, 4521984u, 0xc7f92f55u},   {708612096u, 2129792u, 0xb63d558eu},
    {710742016u, 4038656u, 0x5a030ffau},   {714780672u, 4038656u, 0x11a4d5d7u},
    {718819328u, 4038656u, 0xf94a9efau},   {722857984u, 4038656u, 0x19c7ed41u},
    {726896640u, 4038656u, 0x84d6f662u},   {730935296u, 4038656u, 0x0dad1b41u},
    {734973952u, 4038656u, 0xea152a5du},   {739012608u, 4038656u, 0x87c4355du},
    {743051264u, 4038656u, 0x480b6c1eu},   {747089920u, 4038656u, 0xed475180u},
    {751128576u, 6746112u, 0xa41b7661u},   {757874688u, 4907760u, 0xefaeee71u},
    {762783744u, 104398852u, 0x0b670315u},
};
/* END DATA.DF MANIFEST pal */

/* BEGIN DATA.DF MANIFEST jpn */
#define DF_JPN_SIZE 553443328u
#define DF_JPN_COUNT 183u
#define DF_JPN_DIR_CRC 0xfc60bbafu
static const IcoDfManifestEntry dfMembersJpn[] = {
    {8192u, 2374030u, 0x6abbdfcfu},        {2383872u, 1256365u, 0xaaf763a3u},
    {3641344u, 3763677u, 0xd64117dcu},     {7405568u, 51919u, 0x5f42c211u},
    {7458816u, 1928900u, 0x5cbd5400u},     {9388032u, 1638752u, 0x01084ffeu},
    {11028480u, 2769953u, 0x750d6f69u},    {13799424u, 3161213u, 0xf9cee47cu},
    {16961536u, 4283974u, 0x37058c1fu},    {21245952u, 3297914u, 0xd265287au},
    {24545280u, 3543743u, 0x36f0c276u},    {28090368u, 3109687u, 0xa70f4513u},
    {31201280u, 2144364u, 0x146f3c52u},    {33347584u, 4394394u, 0xc74a8775u},
    {37742592u, 2056540u, 0xb6f4966au},    {39800832u, 1618190u, 0x6d953521u},
    {41420800u, 1473751u, 0x71888ff2u},    {42895360u, 2122133u, 0xcf3a6251u},
    {45019136u, 4102265u, 0x06c46bbau},    {49123328u, 2485489u, 0x6809a21bu},
    {51609600u, 2547266u, 0x2ce2a59fu},    {54157312u, 2763562u, 0x43b1dc3fu},
    {56922112u, 4667197u, 0x4dadd848u},    {61589504u, 3903072u, 0x272561beu},
    {65492992u, 3313098u, 0xb2d64859u},    {68806656u, 527059u, 0xe25a8dd2u},
    {69335040u, 36699u, 0x943cf0ebu},      {69371904u, 3423057u, 0x6124b21bu},
    {72796160u, 2988597u, 0xaf8b54a4u},    {75786240u, 3832796u, 0x3c6a0db0u},
    {79620096u, 4094823u, 0x6678867eu},    {83716096u, 4667744u, 0x49be34aau},
    {88385536u, 3724176u, 0x36267903u},    {92110848u, 4099768u, 0x6ea6653du},
    {96210944u, 4071888u, 0xb86ed433u},    {100284416u, 3846739u, 0x0117cc5du},
    {104132608u, 3722519u, 0xe2630f2du},   {107855872u, 4168060u, 0x39a94f92u},
    {112025600u, 3761408u, 0xf7410ba3u},   {115787776u, 3860881u, 0x8465c8b0u},
    {119650304u, 4289951u, 0xd309cf2cu},   {123940864u, 4357496u, 0xca5004c9u},
    {128299008u, 4764745u, 0x85ce478du},   {133064704u, 4682736u, 0xbe6cef5bu},
    {137748480u, 4369026u, 0x772c49d9u},   {142118912u, 4442823u, 0xc7447ff0u},
    {146563072u, 4349727u, 0xa57814d4u},   {150913024u, 3068971u, 0x9e86ad0fu},
    {153982976u, 2949912u, 0x2d212ca7u},   {156934144u, 4797246u, 0xe2583a1eu},
    {161732608u, 2613739u, 0xdc441ed0u},   {164347904u, 4142882u, 0x0073df47u},
    {168491008u, 2579249u, 0x4103ff45u},   {171071488u, 3553887u, 0xc665b6fbu},
    {174626816u, 4706484u, 0x099c248cu},   {179335168u, 3833140u, 0x7ce6163bu},
    {183169024u, 4489433u, 0x3832eedfu},   {187660288u, 3775547u, 0x7c158bd9u},
    {191436800u, 3333007u, 0xe4d35de6u},   {194770944u, 4051638u, 0x1f387ae1u},
    {198823936u, 4135365u, 0x98e9973bu},   {202960896u, 3166284u, 0xd61b9f19u},
    {206129152u, 3116065u, 0xdc063450u},   {209246208u, 3992639u, 0x94bc9b45u},
    {213239808u, 2836411u, 0xdeab4d45u},   {216076288u, 2544575u, 0x772c8e72u},
    {218621952u, 4286763u, 0x717d7052u},   {222910464u, 2934441u, 0x03c57102u},
    {225845248u, 95624u, 0xcdb0a477u},     {225941504u, 1277952u, 0x0710d8feu},
    {227219456u, 4337664u, 0xec562dfcu},   {231557120u, 2375680u, 0xdb8bcedbu},
    {233932800u, 4538368u, 0x6e6c79b0u},   {238471168u, 1028096u, 0xa4020997u},
    {239499264u, 901120u, 0xa4223d4bu},    {240400384u, 901120u, 0x08660208u},
    {241301504u, 483328u, 0x9b3142f9u},    {241784832u, 1208320u, 0xe434981du},
    {242993152u, 3125248u, 0xfd8319cdu},   {246118400u, 921600u, 0x0ca4288au},
    {247040000u, 983040u, 0x52dbb45du},    {248023040u, 987136u, 0x82ffa512u},
    {249010176u, 1036288u, 0x24df6266u},   {250046464u, 1175552u, 0x51c61723u},
    {251222016u, 1724416u, 0x28241fd3u},   {252946432u, 1728512u, 0xcb802602u},
    {254674944u, 1269760u, 0x1a63ee1du},   {255944704u, 974848u, 0x104aa807u},
    {256919552u, 1007616u, 0x67b15bfdu},   {257927168u, 4206592u, 0x8de6edacu},
    {262133760u, 1437696u, 0x2beb372bu},   {263571456u, 905216u, 0xcfd8aee6u},
    {264476672u, 5005312u, 0x06a51956u},   {269481984u, 2277376u, 0x9260deb0u},
    {271759360u, 3289088u, 0x96839a46u},   {275048448u, 6283264u, 0x738fa681u},
    {281331712u, 3088384u, 0xd1eaa2f2u},   {284420096u, 1150976u, 0xadabf5d6u},
    {285571072u, 2539520u, 0xf3cce748u},   {288110592u, 13762560u, 0x68ab2d00u},
    {301873152u, 598016u, 0x10f5ecbeu},    {302471168u, 10067968u, 0xce38883fu},
    {312539136u, 1642496u, 0x23a7fef5u},   {314181632u, 847872u, 0x0e23ccdfu},
    {315029504u, 950272u, 0xfae89fd8u},    {315979776u, 3026944u, 0x8eee62adu},
    {319006720u, 1335296u, 0x401d91e6u},   {320342016u, 1355776u, 0xa4fef056u},
    {321697792u, 1200128u, 0xa2747422u},   {322897920u, 1040384u, 0xcc242a09u},
    {323938304u, 1310720u, 0x59256064u},   {325249024u, 757760u, 0xcc1d0d17u},
    {326006784u, 630784u, 0x32381d70u},    {326637568u, 802816u, 0xb0558cadu},
    {327440384u, 755712u, 0x8f8c0382u},    {328196096u, 831488u, 0x5c12a114u},
    {329027584u, 827392u, 0x18c61163u},    {329854976u, 847872u, 0xb0e04b4du},
    {330702848u, 1400832u, 0x4836d5c0u},   {332103680u, 1392640u, 0x2bede058u},
    {333496320u, 765952u, 0x42235eddu},    {334262272u, 1060864u, 0xabdcfc3au},
    {335323136u, 708608u, 0x34988651u},    {336031744u, 720896u, 0xa4791c6eu},
    {336752640u, 716800u, 0xaa4a7b6cu},    {337469440u, 872448u, 0x93c8dfdcu},
    {338341888u, 1171456u, 0xd8c6ce32u},   {339513344u, 913408u, 0xc721cf89u},
    {340426752u, 888832u, 0x33f47f19u},    {341315584u, 1232896u, 0x55da9176u},
    {342548480u, 942080u, 0xc38709c2u},    {343490560u, 655360u, 0x41704791u},
    {344145920u, 696320u, 0x3e551bdbu},    {344842240u, 712704u, 0x176e9058u},
    {345554944u, 630784u, 0xeb013728u},    {346185728u, 700416u, 0xb6a4a501u},
    {346886144u, 774144u, 0x3e071182u},    {347660288u, 798720u, 0x04c79688u},
    {348459008u, 786432u, 0xafbfa914u},    {349245440u, 827392u, 0x30cd6303u},
    {350072832u, 626688u, 0xc0ff345bu},    {350699520u, 634880u, 0xfd657b3au},
    {351334400u, 679936u, 0x1099d110u},    {352014336u, 2478080u, 0x70245be9u},
    {354492416u, 5013504u, 0xe8981923u},   {359505920u, 561152u, 0xad570c36u},
    {360067072u, 835584u, 0x04ed0debu},    {360902656u, 827392u, 0xd2cbdf1fu},
    {361730048u, 671744u, 0xc0b23345u},    {362401792u, 651264u, 0x6069ae9au},
    {363053056u, 4538368u, 0x071cc158u},   {367591424u, 454656u, 0xaa0f4915u},
    {368046080u, 503808u, 0xb852dda8u},    {368549888u, 471040u, 0x9080b7b4u},
    {369020928u, 458752u, 0x4f75e902u},    {369479680u, 2815232u, 0x3aaa45e2u},
    {372295680u, 687232u, 0x62b935eau},    {372983808u, 964u, 0xe5fa0c6bu},
    {372985856u, 2662400u, 0x34c81911u},   {375648256u, 3022848u, 0xfe2b76d4u},
    {378671104u, 3616768u, 0xacf3f715u},   {382287872u, 6213632u, 0x474ac58fu},
    {388501504u, 1789952u, 0x437d572au},   {390291456u, 4202496u, 0x17b056d2u},
    {394493952u, 1089536u, 0xe457c5a6u},   {395583488u, 876544u, 0x575cf3e7u},
    {396460032u, 1811680u, 0xf5e5792eu},   {398272512u, 3567616u, 0x848d1aefu},
    {401840128u, 2075320u, 0xe8882aa8u},   {403916800u, 2887680u, 0xc0396b7eu},
    {406804480u, 2688000u, 0x73e284f7u},   {409493504u, 4440064u, 0x00e7c349u},
    {413933568u, 725760u, 0x25bf4f45u},    {414660608u, 3567616u, 0x848d1aefu},
    {418228224u, 2961408u, 0x62da9193u},   {421189632u, 1467648u, 0x4d5b5ad1u},
    {422658048u, 4521984u, 0xc7f92f55u},   {427180032u, 2129792u, 0xc689ace2u},
    {429309952u, 4038656u, 0x5db423b5u},   {433348608u, 4038656u, 0xf7919258u},
    {437387264u, 6746112u, 0xa41b7661u},   {444133376u, 4907760u, 0xefaeee71u},
    {449042432u, 104398852u, 0x0b670315u},
};
/* END DATA.DF MANIFEST jpn */

/* BEGIN DATA.DF MANIFEST kor */
#define DF_KOR_SIZE 552286208u
#define DF_KOR_COUNT 183u
#define DF_KOR_DIR_CRC 0xa8ab2e74u
static const IcoDfManifestEntry dfMembersKor[] = {
    {8192u, 2370996u, 0x3fa91ebeu},        {2379776u, 1256365u, 0xaaf763a3u},
    {3637248u, 3750373u, 0x9a851676u},     {7389184u, 51919u, 0x5f42c211u},
    {7442432u, 1928900u, 0x5cbd5400u},     {9371648u, 1219845u, 0xb01de094u},
    {10592256u, 2755294u, 0x20aae68bu},    {13348864u, 3148848u, 0xe79390e1u},
    {16498688u, 4271451u, 0x33ec9746u},    {20770816u, 3287430u, 0xa22453a4u},
    {24059904u, 3535108u, 0x8fa255ddu},    {27596800u, 3098162u, 0x78293c20u},
    {30695424u, 2132688u, 0x4767657bu},    {32829440u, 4381059u, 0x078bbfbeu},
    {37212160u, 2046034u, 0x7627f10fu},    {39260160u, 1606834u, 0x50e9f545u},
    {40867840u, 1463173u, 0x2e40f224u},    {42332160u, 2109331u, 0xed886a93u},
    {44441600u, 4090988u, 0x89fddc87u},    {48533504u, 2475394u, 0x5fff599cu},
    {51009536u, 2532767u, 0x60704d98u},    {53542912u, 2750770u, 0xf5bed928u},
    {56295424u, 4655288u, 0x78aa8025u},    {60952576u, 3893041u, 0x90fc9f1bu},
    {64845824u, 3300213u, 0x4937f712u},    {68147200u, 512530u, 0x9634374du},
    {68661248u, 36699u, 0x943cf0ebu},      {68698112u, 3412299u, 0xd945f064u},
    {72112128u, 2976787u, 0x947db4efu},    {75089920u, 3821638u, 0x1347991fu},
    {78913536u, 4082880u, 0xfc872cb0u},    {82997248u, 4657440u, 0x6be8ac05u},
    {87656448u, 3713264u, 0x6acb791fu},    {91371520u, 4086826u, 0x1a4e77a3u},
    {95459328u, 4059305u, 0x6fae1424u},    {99520512u, 3834239u, 0x8e03dedcu},
    {103356416u, 3710614u, 0x4758f3a2u},   {107067392u, 4155468u, 0xce1ff6f7u},
    {111224832u, 3749511u, 0x61c30fe4u},   {114974720u, 3848062u, 0x960c9906u},
    {118822912u, 4276601u, 0x96e2bc73u},   {123101184u, 4346092u, 0xbf473a6bu},
    {127449088u, 4751995u, 0xbe6121a7u},   {132202496u, 4672799u, 0x9857f275u},
    {136876032u, 4356155u, 0xf8af3911u},   {141234176u, 4431532u, 0x629cd895u},
    {145666048u, 4337773u, 0xa7603b13u},   {150005760u, 3058318u, 0xa429dcd0u},
    {153065472u, 2939546u, 0x26213c5au},   {156006400u, 4784954u, 0xd555232fu},
    {160792576u, 2601801u, 0x3e4dfbb0u},   {163395584u, 4130351u, 0x80beccfeu},
    {167526400u, 2566116u, 0xe243ff3bu},   {170092544u, 3542021u, 0x8dc4a35au},
    {173635584u, 4695418u, 0x5491713eu},   {178331648u, 3822027u, 0x10211ad5u},
    {182155264u, 4476913u, 0x1ccec8beu},   {186632192u, 3763335u, 0xd24422f7u},
    {190396416u, 3322595u, 0x3172a3e6u},   {193720320u, 4039959u, 0x0abccb7du},
    {197761024u, 4123845u, 0xca8e97f8u},   {201885696u, 3153924u, 0x0380ebebu},
    {205041664u, 3103859u, 0xe2bcef88u},   {208146432u, 3980908u, 0x17966aceu},
    {212127744u, 2822248u, 0x0924bdddu},   {214951936u, 2532596u, 0x8870bcbeu},
    {217485312u, 4275137u, 0x221b4adbu},   {221761536u, 2924973u, 0x7ffe0c33u},
    {224688128u, 95624u, 0xcdb0a477u},     {224784384u, 1277952u, 0x0710d8feu},
    {226062336u, 4337664u, 0xec562dfcu},   {230400000u, 2375680u, 0xdb8bcedbu},
    {232775680u, 4538368u, 0x6e6c79b0u},   {237314048u, 1028096u, 0xa4020997u},
    {238342144u, 901120u, 0xa4223d4bu},    {239243264u, 901120u, 0x08660208u},
    {240144384u, 483328u, 0x9b3142f9u},    {240627712u, 1208320u, 0xe434981du},
    {241836032u, 3125248u, 0xfd8319cdu},   {244961280u, 921600u, 0x0ca4288au},
    {245882880u, 983040u, 0x52dbb45du},    {246865920u, 987136u, 0x82ffa512u},
    {247853056u, 1036288u, 0x24df6266u},   {248889344u, 1175552u, 0x51c61723u},
    {250064896u, 1724416u, 0x28241fd3u},   {251789312u, 1728512u, 0xcb802602u},
    {253517824u, 1269760u, 0x1a63ee1du},   {254787584u, 974848u, 0x104aa807u},
    {255762432u, 1007616u, 0x67b15bfdu},   {256770048u, 4206592u, 0x8de6edacu},
    {260976640u, 1437696u, 0x2beb372bu},   {262414336u, 905216u, 0xcfd8aee6u},
    {263319552u, 5005312u, 0x06a51956u},   {268324864u, 2277376u, 0x9260deb0u},
    {270602240u, 3289088u, 0x96839a46u},   {273891328u, 6283264u, 0x738fa681u},
    {280174592u, 3088384u, 0xd1eaa2f2u},   {283262976u, 1150976u, 0xadabf5d6u},
    {284413952u, 2539520u, 0xf3cce748u},   {286953472u, 13762560u, 0x68ab2d00u},
    {300716032u, 598016u, 0x10f5ecbeu},    {301314048u, 10067968u, 0xce38883fu},
    {311382016u, 1642496u, 0x23a7fef5u},   {313024512u, 847872u, 0x0e23ccdfu},
    {313872384u, 950272u, 0xfae89fd8u},    {314822656u, 3026944u, 0x8eee62adu},
    {317849600u, 1335296u, 0x401d91e6u},   {319184896u, 1355776u, 0xa4fef056u},
    {320540672u, 1200128u, 0xa2747422u},   {321740800u, 1040384u, 0xcc242a09u},
    {322781184u, 1310720u, 0x59256064u},   {324091904u, 757760u, 0xcc1d0d17u},
    {324849664u, 630784u, 0x32381d70u},    {325480448u, 802816u, 0xb0558cadu},
    {326283264u, 755712u, 0x8f8c0382u},    {327038976u, 831488u, 0x5c12a114u},
    {327870464u, 827392u, 0x18c61163u},    {328697856u, 847872u, 0xb0e04b4du},
    {329545728u, 1400832u, 0x4836d5c0u},   {330946560u, 1392640u, 0x2bede058u},
    {332339200u, 765952u, 0x42235eddu},    {333105152u, 1060864u, 0xabdcfc3au},
    {334166016u, 708608u, 0x34988651u},    {334874624u, 720896u, 0xa4791c6eu},
    {335595520u, 716800u, 0xaa4a7b6cu},    {336312320u, 872448u, 0x93c8dfdcu},
    {337184768u, 1171456u, 0xd8c6ce32u},   {338356224u, 913408u, 0xc721cf89u},
    {339269632u, 888832u, 0x33f47f19u},    {340158464u, 1232896u, 0x55da9176u},
    {341391360u, 942080u, 0xc38709c2u},    {342333440u, 655360u, 0x41704791u},
    {342988800u, 696320u, 0x3e551bdbu},    {343685120u, 712704u, 0x176e9058u},
    {344397824u, 630784u, 0xeb013728u},    {345028608u, 700416u, 0xb6a4a501u},
    {345729024u, 774144u, 0x3e071182u},    {346503168u, 798720u, 0x04c79688u},
    {347301888u, 786432u, 0xafbfa914u},    {348088320u, 827392u, 0x30cd6303u},
    {348915712u, 626688u, 0xc0ff345bu},    {349542400u, 634880u, 0xfd657b3au},
    {350177280u, 679936u, 0x1099d110u},    {350857216u, 2478080u, 0x70245be9u},
    {353335296u, 5013504u, 0xe8981923u},   {358348800u, 561152u, 0xad570c36u},
    {358909952u, 835584u, 0x04ed0debu},    {359745536u, 827392u, 0xd2cbdf1fu},
    {360572928u, 671744u, 0xc0b23345u},    {361244672u, 651264u, 0x6069ae9au},
    {361895936u, 4538368u, 0x071cc158u},   {366434304u, 454656u, 0xaa0f4915u},
    {366888960u, 503808u, 0xb852dda8u},    {367392768u, 471040u, 0x9080b7b4u},
    {367863808u, 458752u, 0x4f75e902u},    {368322560u, 2815232u, 0x3aaa45e2u},
    {371138560u, 687232u, 0x62b935eau},    {371826688u, 964u, 0xe5fa0c6bu},
    {371828736u, 2662400u, 0x34c81911u},   {374491136u, 3022848u, 0xfe2b76d4u},
    {377513984u, 3616768u, 0xacf3f715u},   {381130752u, 6213632u, 0x474ac58fu},
    {387344384u, 1789952u, 0x437d572au},   {389134336u, 4202496u, 0x17b056d2u},
    {393336832u, 1089536u, 0xe457c5a6u},   {394426368u, 876544u, 0x575cf3e7u},
    {395302912u, 1811680u, 0xf5e5792eu},   {397115392u, 3567616u, 0x848d1aefu},
    {400683008u, 2075320u, 0xe8882aa8u},   {402759680u, 2887680u, 0xc0396b7eu},
    {405647360u, 2688000u, 0x73e284f7u},   {408336384u, 4440064u, 0x00e7c349u},
    {412776448u, 725760u, 0x25bf4f45u},    {413503488u, 3567616u, 0x848d1aefu},
    {417071104u, 2961408u, 0x62da9193u},   {420032512u, 1467648u, 0x4d5b5ad1u},
    {421500928u, 4521984u, 0xc7f92f55u},   {426022912u, 2129792u, 0xc689ace2u},
    {428152832u, 4038656u, 0xcfb1949bu},   {432191488u, 4038656u, 0x48333c81u},
    {436230144u, 6746112u, 0xa41b7661u},   {442976256u, 4907760u, 0xefaeee71u},
    {447885312u, 104398852u, 0x0b670315u},
};
/* END DATA.DF MANIFEST kor */

/* --- the discs ---------------------------------------------------------------- */

#define N_MEMBERS(a) ((uint32_t)(sizeof(a) / sizeof((a)[0])))

/* config/regions.txt, row for row (region_select checks the slug, the disc
   id, the boot file and the product name against it) */
const IcoDiscInfo ico_discs[ICO_REGION_COUNT] = {
    [ICO_REGION_PAL] = {"SCES-50760", "pal", "SCES_507.60",
                        "1017b53f6e80f41f823369b0be1d8c69f7e16dc6",
                        "da3644c54c26fe760f3b6a591a5fc2eab396ed2b", 907706368u, DF_PAL_SIZE,
                        DF_PAL_COUNT, DF_PAL_DIR_CRC, 0u, dfMembersPal, N_MEMBERS(dfMembersPal),
                        "ico.o2r", "Ico_PAL", 0x14 /* SCECdPS2DVD */, "the European disc"},
    [ICO_REGION_JPN] = {"SCPS-11003", "jpn", "SCPS_110.03",
                        "95ee72becbc7fba85e9c0f6aa6fc932c705b37b3",
                        "580da1dcc4c239a1608a10300e7e220fe5d35525", 559693824u, DF_JPN_SIZE,
                        DF_JPN_COUNT, DF_JPN_DIR_CRC, 0u, dfMembersJpn, N_MEMBERS(dfMembersJpn),
                        "ico-jpn.o2r", "Ico_JPN", 0x12 /* SCECdPS2CD */, "the Japanese disc"},
    [ICO_REGION_KOR] = {"SCPS-56001", "kor", "SCPS_560.01",
                        "156b63e5824b03e60e0d45c4e86fc424d9e1dd1b",
                        "f9b00596d133c698df20c8ffe4ca2d7107e236bb", 873332736u, DF_KOR_SIZE,
                        DF_KOR_COUNT, DF_KOR_DIR_CRC, 153600u, dfMembersKor,
                        N_MEMBERS(dfMembersKor), "ico-kor.o2r", "Ico_KOR", 0x14 /* SCECdPS2DVD */,
                        "the Korean disc"},
};

const IcoDiscInfo *ico_disc_by_id(const char *id)
{
    int i;

    for (i = 0; id != NULL && i < ICO_REGION_COUNT; i++) {
        if (strcmp(ico_discs[i].id, id) == 0) {
            return &ico_discs[i];
        }
    }
    return NULL;
}

const IcoDiscInfo *ico_disc_by_boot(const char *boot_elf)
{
    int i;

    for (i = 0; boot_elf != NULL && i < ICO_REGION_COUNT; i++) {
        if (strcmp(ico_discs[i].boot_elf, boot_elf) == 0) {
            return &ico_discs[i];
        }
    }
    return NULL;
}

const IcoDiscInfo *ico_disc_by_slug(const char *slug)
{
    int i;

    for (i = 0; slug != NULL && i < ICO_REGION_COUNT; i++) {
        if (strcmp(ico_discs[i].slug, slug) == 0) {
            return &ico_discs[i];
        }
    }
    return NULL;
}

/* Wording over the rows: "a, b or c". */
#define JOIN_MAX 512

const char *ico_disc_list_text(void)
{
    static char text[JOIN_MAX];
    size_t n = 0;
    int i;

    text[0] = '\0';
    for (i = 0; i < ICO_REGION_COUNT; i++) {
        const char *sep = i == 0 ? "" : (i == ICO_REGION_COUNT - 1 ? " or " : ", ");

        n += (size_t)snprintf(text + n, n < sizeof(text) ? sizeof(text) - n : 0, "%s%s (%s)", sep,
                              ico_discs[i].product_name, ico_discs[i].id);
        if (n >= sizeof(text)) {
            text[sizeof(text) - 1] = '\0';
            break;
        }
    }
    return text;
}

const char *ico_disc_image_names_text(const char *ext)
{
    static char text[JOIN_MAX];
    size_t n = 0;
    int i;

    text[0] = '\0';
    for (i = 0; i < ICO_REGION_COUNT; i++) {
        const char *sep = i == 0 ? "" : (i == ICO_REGION_COUNT - 1 ? " or " : ", ");

        n += (size_t)snprintf(text + n, n < sizeof(text) ? sizeof(text) - n : 0, "%s%s%s", sep,
                              ico_discs[i].image_base, ext != NULL ? ext : "");
        if (n >= sizeof(text)) {
            text[sizeof(text) - 1] = '\0';
            break;
        }
    }
    return text;
}

const char *ico_disc_boots_text(void)
{
    static char text[JOIN_MAX];
    size_t n = 0;
    int i;

    text[0] = '\0';
    for (i = 0; i < ICO_REGION_COUNT; i++) {
        const char *sep = i == 0 ? "" : (i == ICO_REGION_COUNT - 1 ? " or " : ", ");

        n += (size_t)snprintf(text + n, n < sizeof(text) ? sizeof(text) - n : 0, "%s%s", sep,
                              ico_discs[i].boot_elf);
        if (n >= sizeof(text)) {
            text[sizeof(text) - 1] = '\0';
            break;
        }
    }
    return text;
}

const char *ico_disc_archive_names_text(void)
{
    static char text[JOIN_MAX];
    size_t n = 0;
    int i;

    text[0] = '\0';
    for (i = 0; i < ICO_REGION_COUNT; i++) {
        const char *sep = i == 0 ? "" : (i == ICO_REGION_COUNT - 1 ? " or " : ", ");

        n += (size_t)snprintf(text + n, n < sizeof(text) ? sizeof(text) - n : 0, "%s%s", sep,
                              ico_discs[i].archive_name);
        if (n >= sizeof(text)) {
            text[sizeof(text) - 1] = '\0';
            break;
        }
    }
    return text;
}
