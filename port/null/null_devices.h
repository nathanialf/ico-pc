/*
 * port/null/null_devices.h
 *
 * Controls of the null devices the headless build links in place of the
 * pad, memory card, sound driver and system configuration (pad_null.c,
 * mc_null.c, snd_null.c, scf_null.c).  gfx_null.c (package 1B) has its own.
 */
#ifndef ICO_PORT_NULL_DEVICES_H
#define ICO_PORT_NULL_DEVICES_H

/* sceScfGetLanguage's answer, in libscf's numbering (0 Japanese, 1 English,
   2 French, 3 Spanish, 4 German, 5 Italian, 6 Dutch, 7 Portuguese).  The
   config layer sets it later from the system locale (plan, "Boot"). */
#define ICO_SCF_LANGUAGE_JAPANESE 0
#define ICO_SCF_LANGUAGE_ENGLISH 1
#define ICO_SCF_LANGUAGE_FRENCH 2
#define ICO_SCF_LANGUAGE_SPANISH 3
#define ICO_SCF_LANGUAGE_GERMAN 4
#define ICO_SCF_LANGUAGE_ITALIAN 5

extern int ico_scf_language;

/* The memory card result the null card reports for every request: "no card
   in the slot" (sceMcGetInfo's -10 and below). */
#define ICO_MC_NULL_RESULT (-10)

/* The sound driver's RPC server id (SNDN2DRV.IRX, sound.c's bind) and the
   size of the reply page the tick call returns. */
#define ICO_SND_SERVER_ID 0x736E646Eu
#define ICO_SND_REPLY_SIZE 0x200

/* Register the null sound driver as the IOP server for ICO_SND_SERVER_ID
   (port/data/sif_host.h).  SgSndn2RemoteInit calls it; it is idempotent. */
void ico_snd_null_register(void);

/* The reply page the null driver last returned (tests). */
const unsigned char *ico_snd_null_last_reply(void);

#endif /* ICO_PORT_NULL_DEVICES_H */
