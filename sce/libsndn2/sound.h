/*
 * sce/libsndn2/sound.h  (derived name: named after the member, sound.o)
 *
 * The declarations sound.c needs ahead of their definitions: the Sg reverb
 * entry points the parameter controller calls, the _Sg DMA helper, the
 * fake-body vab opener, the _Sg event handlers and realtime pass the tick
 * calls, the _Sg voice, packet and remote helpers called above their
 * definitions; the library's entry points the game's sound, I/O and debug
 * code calls; and the stream PCM calls the movie player's audio decoder makes.
 * Each prototype is the signature of its definition in sound.c, which
 * includes this header.
 *
 * It is the port's host header too: port/compat/sound.h includes it.  The
 * one EE-sized type is host-correct (the EE's `long` is 64 bits), and
 * SgSetSePitchDirect, which the EE header left out (s_init.c declared it
 * itself), is declared.
 */
#ifndef SCE_LIBSNDN2_SOUND_H
#define SCE_LIBSNDN2_SOUND_H

void SgSetReverbType(int core, int type);
void SgSetReverbDepth(int core, int left, int right);
void SgSetReverbDelaytime(int core, int time);
void SgSetReverbFeedback(int core, int feedback);
void _SgDmaCommon(int cmd, unsigned int iop, unsigned int spu, unsigned int size);
int SgVabOpenFakeBody(int *hd, int spu);
int _SgSetPkAdd(int cmd, int id, int word2, int word3);
int _SgSeMain(int *seq);
int _SgBgmMain(int *seq);
int _SgSetRealtimeVolume(int *seq);
int _SgTableEnvAdd(int *seq);
int _SgSeqKeyOnSlot(int note);
int _SgSeKeyOnSlot(int owner, int pri, int vab);
int _SgSeKeyOff(char *seq);
int _SgSeqKeyOff(int *seq);
int _SgIntoKeyOn(int count, int note, int key);
int _SgPitchTableVag(int slot, int step, int note, int fine, int bend, int range, int pitch);
int _SgSeqSeVolume(int voice, int *seq);
int _SgPan(int tone, int ch);
int _SgfadeParam(int target, int start, int total, int left);
int _SgSndn2Remote(int rpc_number, int mode, void *sendbuf, void *recvbuf, int ssize, int rsize);
void _SgSeqSeRrEnd(int *seq);
void _SgEndSeq(int *seq);
void _SgTempoChange(int *seq);
void _SgProgChange(int *seq);
void _SgContMod(int *seq);
void _SgContModLoop(int *seq);
void _SgContParam(int *seq);
void _SgContVol(int *seq);
void _SgContPan(int *seq);
void _SgContDump(int *seq);
void _SgContPolta(char *seq);
void _SgContSeLoop(int *seq);
void _SgContLoopCount(void *seq);
void _SgContLoop(int *seq);
void _SgBendForm(int *seq);
void _SgSetRealtimeTickProc(void);
void _SgDeltaTime(char *s);
int SgSndn2RemoteInit(void);
int SgSndn2RemoteSync(void);
void SgInit(void);
void SgInitHot(void);
void SgQuit(void);
void SgCalledTickProc(void);
void SgSetDigitalOutputMode(int mode);
int SgDmaWrite(unsigned int iop, unsigned int spu, unsigned int size);
int SgDmaRead(unsigned int spu, unsigned int iop, unsigned int size);
int SgGetDmaTransferStatus(int mode);
int SgVabOpen(int iop, int *hd, int spu);
int SgVabClose(int vab);
int SgBgmOpen(int vab, void *sq);
int SgBgmClose(int id);
void SgSetReverbEndAddr(int core, int addr);
void SgSetOutputMode(int mode);
void SgSetTickMode(int tick);
int SgGetSlotStatus(int kind, int slot);
void SgSetMasterVol(int core, int left, int right);
int SgSetBgmVol(unsigned int id, int vol, int mask);
int SgSetSeMasterVol(int vab, int vol);
void SgBgmPlay(unsigned int id);
void SgBgmStop(unsigned int id, int mode);
void SgSetBgmTempo(unsigned int id, int tempo);
int SgGetBgmTempo(unsigned int id);
int SgGetBgmStatus(int id);
int SgGetBgmChStatus(unsigned int id, int channel, int kind);
int SgSetBgmPanpot(unsigned int id, int pan);
int SgSePlay(int vabflags, int prog, int tone);
void SgSeStop(int id);
void SgSeStopAll(int immediate);
void SgSetSeVolDirect(unsigned int id, int left, int right);
int SgGetSpuSlotMalloc(int mode);
int SgSetSpuSlotFree(unsigned int slot);
void SgStAdpcmInit(void);
void SgStAdpcmQuit(void);
int SgStAdpcmOpen(void *req);
int SgStAdpcmClose(unsigned int ch);
int SgStAdpcmChannelVolume(unsigned long long mask, unsigned int left, int right);
int SgStAdpcmChannelPitch(unsigned long long mask, int pitch);
int SgStAdpcmPlay(unsigned long long mask);
int SgStAdpcmStop(unsigned long long mask);
int SgStAdpcmIopReadAddr(int ch);
void SgStPcmInit(void);
void SgStPcmQuit(void);
int SgStPcmOpen(int *req);
int SgStPcmClose(unsigned int ch);
void SgStPcmSetEffect(int effect);
int SgStPcmPlay(unsigned long long mask);
int SgStPcmStop(unsigned long long mask);
int SgStPcmLseek(unsigned int ch, unsigned int offset);
void SgStPcmVolume(unsigned long long mask, unsigned int left, int right);
int SgStPcmIopReadAddr(unsigned int ch);

void SgSetSePitchDirect(unsigned int id, int pitch);
int SgStPcmBufMode(int mode, long long mask, int addr); /* EE long: 64 bits */

#endif /* SCE_LIBSNDN2_SOUND_H */
