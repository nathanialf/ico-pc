/*
 * port/data/sif_host.h
 *
 * The host's SIF: the EE<->IOP calls (port/compat/sifrpc.h, sifdev.h) with no
 * IOP behind them.  There is no IOP reboot and no IRX module; the IOP heap
 * and SIF DMA work on ico_iop_ram (iop_ram.h); an RPC server is a host
 * function registered here under the server id the EE binds to.
 */
#ifndef ICO_PORT_SIF_HOST_H
#define ICO_PORT_SIF_HOST_H

/* A host stand-in for an IOP RPC server: handed the request number and the
   send buffer, it returns the reply (at least the caller's receive size;
   NULL replies with zeros). */
typedef void *(*IcoSifServerFn)(unsigned int rpc_number, void *send, int ssize, int rsize);

/* Register (or replace) the server for `sid`.  0, or -1 if the table is
   full. */
int ico_sif_register_server(unsigned int sid, IcoSifServerFn fn);

/* Forget every server and the IOP heap (tests). */
void ico_sif_host_reset(void);

/* The last sceSifCallRpc's server id and RPC number, and how many calls
   there were (the diagnostics heartbeat). */
void ico_sif_host_last_rpc(unsigned int *sid, unsigned int *rpc, unsigned int *count);

#endif /* ICO_PORT_SIF_HOST_H */
