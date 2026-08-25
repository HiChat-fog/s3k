#include "syscall.h"

#include "csr.h"
#include "current.h"
#include "exception.h"
#include "ipc.h"
#include "lock.h"
#include "macro.h"
#include "mem.h"
#include "mon.h"
#include "preempt.h"
#include "proc.h"
#include "rtc.h"
#include "tsl.h"
#include "ttas.h"

/**
 * Get the current process's PID.
 */
static proc_t *syscall_pid_get(pid_t pid, word_t args[8])
{
	(void)args;
	args[0] = pid;
	return current;
}

/**
 * Get a virtual register of the current process.
 */
static proc_t *syscall_vreg_get(pid_t pid, word_t args[8])
{
	(void)pid;
	switch (args[1]) {
	case VREG_TPC:
		args[0] = current->trap.tpc;
		break;
	case VREG_TSP:
		args[0] = current->trap.tsp;
		break;
	case VREG_ECAUSE:
		args[0] = current->trap.ecause;
		break;
	case VREG_EVAL:
		args[0] = current->trap.eval;
		break;
	case VREG_EPC:
		args[0] = current->trap.epc;
		break;
	case VREG_ESP:
		args[0] = current->trap.esp;
		break;
	default:
		args[0] = 0;
		break;
	}
	return current;
}

/**
 * Set a virtual register of the current process.
 */
static proc_t *syscall_vreg_set(pid_t pid, word_t args[8])
{
	(void)pid;
	switch (args[1]) {
	case VREG_TPC:
		current->trap.tpc = args[2];
		break;
	case VREG_TSP:
		current->trap.tsp = args[2];
		break;
	case VREG_ECAUSE:
		current->trap.ecause = args[2];
		break;
	case VREG_EVAL:
		current->trap.eval = args[2];
		break;
	case VREG_EPC:
		current->trap.epc = args[2];
		break;
	case VREG_ESP:
		current->trap.esp = args[2];
		break;
	default:
		break;
	}
	return current;
}

/**
 * Release the process, then call the scheduler.
 */
static proc_t *syscall_sync(pid_t pid, word_t args[8])
{
	(void)pid;
	(void)args;
	current->timeout = 0;
	return NULL;
}

/**
 * Sleep until the specified timeout, then call the scheduler.
 */
static proc_t *syscall_sleep_until(pid_t pid, word_t args[8])
{
	(void)pid;
	if (args[1] != 0) {
		current->timeout = args[1];
	}
	return NULL;
}

/**
 * Get a memory capability.
 */
static proc_t *syscall_mem_introspect(pid_t pid, word_t args[8])
{
	args[0] = mem_introspect(pid, args[1], args[2], (mem_t *)&args[1]);
	return current;
}

/**
 * Get a time slice capability.
 */
static proc_t *syscall_tsl_introspect(pid_t pid, word_t args[8])
{
	args[0] = tsl_introspect(pid, args[1], args[2], (tsl_t *)&args[1]);
	return current;
}

/**
 * Get a monitor capability.
 */
static proc_t *syscall_mon_introspect(pid_t pid, word_t args[8])
{
	args[0] = mon_introspect(pid, args[1], args[2], (mon_t *)&args[1]);
	return current;
}

/**
 * Get an IPC capability.
 */
static proc_t *syscall_ipc_introspect(pid_t pid, word_t args[8])
{
	args[0] = ipc_introspect(pid, args[1], args[2], (ipc_t *)&args[1]);
	return current;
}

/**
 * Derive a memory capability.
 */
static proc_t *syscall_mem_derive(pid_t pid, word_t args[8])
{
	args[0] = mem_derive(pid, args[1], pid, args[2], args[3], args[4], args[5]);
	return current;
}

/**
 * Derive a time slice capability.
 */
static proc_t *syscall_tsl_derive(pid_t pid, word_t args[8])
{
	args[0] = tsl_derive(pid, args[1], pid, args[2], args[3], args[4]);
	return current;
}

/**
 * Derive a monitor capability.
 */
static proc_t *syscall_mon_derive(pid_t pid, word_t args[8])
{
	args[0] = mon_derive(pid, args[1], pid, args[2]);
	return current;
}

/**
 * Derive an IPC capability.
 */
static proc_t *syscall_ipc_derive(pid_t pid, word_t args[8])
{
	args[0] = ipc_derive(pid, args[1], pid, args[2], args[3], args[4]);
	return current;
}

/**
 * Revoke the children of a memory capability.
 */
static proc_t *syscall_mem_revoke(pid_t pid, word_t args[8])
{
	args[0] = mem_revoke(pid, args[1]);
	return current;
}

/**
 * Revoke the children of a time slice capability.
 */
static proc_t *syscall_tsl_revoke(pid_t pid, word_t args[8])
{
	args[0] = tsl_revoke(pid, args[1]);
	return current;
}

/**
 * Revoke the children of a monitor capability.
 */
static proc_t *syscall_mon_revoke(pid_t pid, word_t args[8])
{
	args[0] = mon_revoke(pid, args[1]);
	return current;
}

/**
 * Revoke the children of an IPC capability.
 */
static proc_t *syscall_ipc_revoke(pid_t pid, word_t args[8])
{
	args[0] = ipc_revoke(pid, args[1]);
	return current;
}

/**
 * Delete a memory capability.
 */
static proc_t *syscall_mem_delete(pid_t pid, word_t args[8])
{
	args[0] = mem_delete(pid, args[1]);
	return current;
}

/**
 * Delete a time slice capability.
 */
static proc_t *syscall_tsl_delete(pid_t pid, word_t args[8])
{
	args[0] = tsl_delete(pid, args[1]);
	return current;
}

/**
 * Delete a monitor capability.
 */
static proc_t *syscall_mon_delete(pid_t pid, word_t args[8])
{
	args[0] = mon_delete(pid, args[1]);
	return current;
}

/**
 * Delete an IPC capability
 */
static proc_t *syscall_ipc_delete(pid_t pid, word_t args[8])
{
	args[0] = ipc_delete(pid, args[1]);
	return current;
}

/**
 * Get a memory capability's PMP configuration.
 */
static proc_t *syscall_mem_pmp_get(pid_t pid, word_t args[8])
{
	pmp_slot_t slot;
	mem_perm_t rwx;
	pmp_addr_t addr;
	args[0] = mem_pmp_get(pid, args[1], &slot, &rwx, &addr);
	args[1] = slot;
	args[2] = rwx;
	args[3] = addr;
	return current;
}

/**
 * Set a memory capability's PMP configuration.
 */
static proc_t *syscall_mem_pmp_set(pid_t pid, word_t args[8])
{
	args[0] = mem_pmp_set(pid, args[1], args[2], args[3], args[4]);
#ifdef PLATFORM_PREEMPT_STK
	platform_proc_pmp_deny(pid);
#endif
	return current;
}

/**
 * Clear a memory capability's PMP configuration.
 */
static proc_t *syscall_mem_pmp_clear(pid_t pid, word_t args[8])
{
	args[0] = mem_pmp_clear(pid, args[1]);
	return current;
}

/**
 * Enable or disable a time slice capability's minor frame.
 */
static proc_t *syscall_tsl_set(pid_t pid, word_t args[8])
{
	args[0] = tsl_set(pid, args[1], args[2]);
	return current;
}

/**
 * Suspend the process that is being monitored by the specified monitor capability.
 */
static proc_t *syscall_mon_suspend(pid_t pid, word_t args[8])
{
	args[0] = mon_suspend(pid, args[1]);
	return current;
}

/**
 * Resume the process that is being monitored by the specified monitor capability.
 */
static proc_t *syscall_mon_resume(pid_t pid, word_t args[8])
{
	args[0] = mon_resume(pid, args[1]);
	return current;
}

/**
 * Yield execution time to the process being monitored by the specified monitor capability.
 */
static proc_t *syscall_mon_yield(pid_t pid, word_t args[8])
{
	proc_t *next = current;
	args[0] = mon_yield(pid, args[1], &next);
	return next;
}

/**
 * Get a register value of the process being monitored by the specified monitor capability.
 */
static proc_t *syscall_mon_reg_get(pid_t pid, word_t args[8])
{
	word_t value;
	args[0] = mon_reg_get(pid, args[1], args[2], &value);
	args[1] = value;
	return current;
}

/**
 * Set a register of the process being monitored by the specified monitor capability.
 */
static proc_t *syscall_mon_reg_set(pid_t pid, word_t args[8])
{
	args[0] = mon_reg_set(pid, args[1], args[2], args[3]);
	return current;
}

/**
 * Get a virtual register value of the process being monitored by the specified monitor capability.
 */
static proc_t *syscall_mon_vreg_get(pid_t pid, word_t args[8])
{
	word_t value;
	args[0] = mon_vreg_get(pid, args[1], args[2], &value);
	args[1] = value;
	return current;
}

/**
 * Set a virtual register of the process being monitored by the specified monitor capability.
 */
static proc_t *syscall_mon_vreg_set(pid_t pid, word_t args[8])
{
	args[0] = mon_vreg_set(pid, args[1], args[2], args[3]);
	return current;
}

/**
 * Get the state of the process being monitored by the specified monitor capability.
 */
static proc_t *syscall_mon_state_get(pid_t pid, word_t args[8])
{
	word_t value;
	args[0] = mon_state_get(pid, args[1], &value);
	args[1] = value;
	return current;
}

/**
 * Get a time slice capability configuration of the process being monitored by the specified monitor capability.
 */
static proc_t *syscall_mon_tsl_introspect(pid_t pid, word_t args[8])
{
	pid_t target = mon_get_pid(pid, args[1]);
	args[0] = ERR_INVALID_ACCESS;
	if (target != INVALID_PID) {
		args[0] = tsl_introspect(target, args[2], args[3], (tsl_t *)&args[1]);
	}
	return current;
}

/**
 * Get a memory capability configuration of the process being monitored by the specified monitor capability.
 */
static proc_t *syscall_mon_mem_introspect(pid_t pid, word_t args[8])
{
	pid_t target = mon_get_pid(pid, args[1]);
	args[0] = ERR_INVALID_ACCESS;
	if (target != INVALID_PID) {
		args[0] = mem_introspect(target, args[2], args[3], (mem_t *)&args[1]);
	}
	return current;
}

/**
 * Get a monitor capability configuration of the process being monitored by the specified monitor capability.
 */
static proc_t *syscall_mon_mon_introspect(pid_t pid, word_t args[8])
{
	pid_t target = mon_get_pid(pid, args[1]);
	args[0] = ERR_INVALID_ACCESS;
	if (target != INVALID_PID) {
		args[0] = mon_introspect(target, args[2], args[3], (mon_t *)&args[1]);
	}
	return current;
}

/**
 * Get an IPC capability configuration of the process being monitored by the specified monitor capability.
 */
static proc_t *syscall_mon_ipc_introspect(pid_t pid, word_t args[8])
{
	pid_t target = mon_get_pid(pid, args[1]);
	args[0] = ERR_INVALID_ACCESS;
	if (target != INVALID_PID) {
		args[0] = ipc_introspect(target, args[2], args[3], (ipc_t *)&args[1]);
	}
	return current;
}

/**
 * Grant a memory capability to the process being monitored by the specified monitor capability.
 */
static proc_t *syscall_mon_mem_grant(pid_t pid, word_t args[8])
{
	pid_t target = mon_get_pid(pid, args[1]);
	args[0] = ERR_INVALID_ACCESS;
	if (target != INVALID_PID) {
		args[0] = mem_transfer(pid, args[2], target);
	}
	return current;
}

/**
 * Grant a time slice capability to the process being monitored by the specified monitor capability.
 */
static proc_t *syscall_mon_tsl_grant(pid_t pid, word_t args[8])
{
	pid_t target = mon_get_pid(pid, args[1]);
	args[0] = ERR_INVALID_ACCESS;
	if (target != INVALID_PID) {
		args[0] = tsl_transfer(pid, args[2], target);
	}
	return current;
}

/**
 * Grant a monitor capability to the process being monitored by the specified monitor capability.
 */
static proc_t *syscall_mon_mon_grant(pid_t pid, word_t args[8])
{
	pid_t target = mon_get_pid(pid, args[1]);
	args[0] = ERR_INVALID_ACCESS;
	if (target != INVALID_PID) {
		args[0] = mon_transfer(pid, args[2], target);
	}
	return current;
}

/**
 * Grant an IPC capability to the process being monitored by the specified monitor capability.
 */
static proc_t *syscall_mon_ipc_grant(pid_t pid, word_t args[8])
{
	pid_t target = mon_get_pid(pid, args[1]);
	args[0] = ERR_INVALID_ACCESS;
	if (target != INVALID_PID) {
		args[0] = ipc_transfer(pid, args[2], target);
	}
	return current;
}

/**
 * Derive then grant a time slice capability to the process being monitored by the specified monitor capability.
 */
static proc_t *syscall_mon_tsl_derive(pid_t pid, word_t args[8])
{
	pid_t target = mon_get_pid(pid, args[1]);
	args[0] = ERR_INVALID_ACCESS;
	if (target != INVALID_PID) {
		args[0] = tsl_derive(pid, args[2], target, args[3], args[4], args[5]);
	}
	return current;
}

/**
 * Get a memory capability configuration of the process being monitored by the specified monitor capability.
 */
static proc_t *syscall_mon_mem_derive(pid_t pid, word_t args[8])
{
	pid_t target = mon_get_pid(pid, args[1]);
	args[0] = ERR_INVALID_ACCESS;
	if (target != INVALID_PID) {
		args[0] = mem_derive(pid, args[2], target, args[3], args[4], args[5], args[6]);
	}
	return current;
}

/**
 * Get a monitor capability configuration of the process being monitored by the specified monitor capability.
 */
static proc_t *syscall_mon_mon_derive(pid_t pid, word_t args[8])
{
	pid_t target = mon_get_pid(pid, args[1]);
	args[0] = ERR_INVALID_ACCESS;
	if (target != INVALID_PID) {
		args[0] = mon_derive(pid, args[2], target, args[3]);
	}
	return current;
}

/**
 * Get an IPC capability configuration of the process being monitored by the specified monitor capability.
 */
static proc_t *syscall_mon_ipc_derive(pid_t pid, word_t args[8])
{
	pid_t target = mon_get_pid(pid, args[1]);
	args[0] = ERR_INVALID_ACCESS;
	if (target != INVALID_PID) {
		args[0] = ipc_derive(pid, args[2], target, args[3], args[4], args[5]);
	}
	return current;
}

/**
 * Get a memory capability PMP configuration of the process being monitored by the specified monitor capability.
 */
static proc_t *syscall_mon_mem_pmp_get(pid_t pid, word_t args[8])
{
	pid_t target = mon_get_pid(pid, args[1]);
	args[0] = ERR_INVALID_ACCESS;
	if (target != INVALID_PID) {
		pmp_slot_t slot;
		mem_perm_t rwx;
		pmp_addr_t addr;
		args[0] = mem_pmp_get(target, args[2], &slot, &rwx, &addr);
		args[1] = slot;
		args[2] = rwx;
		args[3] = addr;
	}
	return current;
}

/**
 * Set a memory capability PMP configuration of the process being monitored by the specified monitor capability.
 */
static proc_t *syscall_mon_mem_pmp_set(pid_t pid, word_t args[8])
{
	pid_t target = mon_get_pid(pid, args[1]);
	args[0] = ERR_INVALID_ACCESS;
	if (target != INVALID_PID) {
		args[0] = mem_pmp_set(target, args[2], args[3], args[4], args[5]);
#ifdef PLATFORM_PREEMPT_STK
		platform_proc_pmp_deny(target);
#endif
	}
	return current;
}

/**
 * Clear a memory capability PMP configuration of the process being monitored by the specified monitor capability.
 */
static proc_t *syscall_mon_mem_pmp_clear(pid_t pid, word_t args[8])
{
	pid_t target = mon_get_pid(pid, args[1]);
	args[0] = ERR_INVALID_ACCESS;
	if (target != INVALID_PID) {
		args[0] = mem_pmp_clear(target, args[2]);
	}
	return current;
}

/**
 * Grant a time slice capability to the process being monitored by the specified monitor capability.
 */
static proc_t *syscall_mon_tsl_set(pid_t pid, word_t args[8])
{
	pid_t target = mon_get_pid(pid, args[1]);
	args[0] = ERR_INVALID_ACCESS;
	if (target != INVALID_PID) {
		args[0] = tsl_set(target, args[2], args[3]);
	}
	return current;
}

/**
 * Send an synchronous IPC message in a unidirectional IPC channel.
 */
static proc_t *syscall_ipc_send(pid_t pid, word_t args[8])
{
	proc_t *next = current;
	word_t data[2] = {args[2], args[3]};
	args[0] = ipc_send(pid, args[1], data, args[4], args[5], &next);
	return next;
}

/**
 * Wait to receive an IPC message (synchronous, bidirectional/unidirectional IPC).
 */
static proc_t *syscall_ipc_recv(pid_t pid, word_t args[8])
{
	proc_t *next = current;
	args[0] = ipc_recv(pid, args[1], &next, args[2]);
	return next;
}

/**
 * Make an IPC call (synchronous, bidirectional IPC channel) to an IPC server.
 */
static proc_t *syscall_ipc_call(pid_t pid, word_t args[8])
{
	proc_t *next = current;
	word_t data[2] = {args[2], args[3]};
	args[0] = ipc_call(pid, args[1], data, args[4], args[5], &next);
	return next;
}

/**
 * Send a reply to an IPC call.
 */
static proc_t *syscall_ipc_reply(pid_t pid, word_t args[8])
{
	proc_t *next = current;
	word_t data[2] = {args[2], args[3]};
	args[0] = ipc_reply(pid, args[1], data, args[4], args[5], &next);
	return next;
}

/**
 * Send a reply for an IPC call, then atomically wait to receive an IPC message.
 */
static proc_t *syscall_ipc_replyrecv(pid_t pid, word_t args[8])
{
	proc_t *next = current;
	word_t data[2] = {args[2], args[3]};
	args[0] = ipc_replyrecv(pid, args[1], data, args[4], args[5], &next, args[6]);
	return next;
}

/**
 * Send an asynchronous IPC message in a unidirectional IPC channel.
 */
static proc_t *syscall_ipc_asend(pid_t pid, word_t args[8])
{
	proc_t *next = current;
	args[0] = ipc_asend(pid, args[1], args[2], &next);
	return next;
}

/**
 * Read the message inbox of an asynchronous IPC channel.
 */
static proc_t *syscall_ipc_arecv(pid_t pid, word_t args[8])
{
	proc_t *next = current;
	word_t data;
	args[0] = ipc_arecv(pid, args[1], &data);
	args[1] = data;
	return next;
}

/*
 * DMA engine simulation. The kernel performs an M-mode memcpy(dst, src, len).
 * This models a bus-master DMA initiator: the transfer is executed in M-mode
 * and therefore bypasses the CPU-side PMP that constrains U-mode processes.
 * It exists to expose the PMP-doesn't-block-DMA limitation and to let the
 * monitor demonstrate gating DMA descriptor targets before they reach here.
 *
 * The monitor can register protected ranges (s3k_dma_protect): a transfer
 * whose destination falls inside any registered range is refused before the
 * copy runs. This is the demo-level stand-in for the I/O-side layered
 * defense (IOPMP/IOMMU-style target vetting) that the proposal describes;
 * with no ranges registered the behaviour is the unguarded limitation arm.
 */
#define DMA_PROTECT_MAX 4

static struct {
	word_t base;
	word_t size;
	bool active;
} dma_protect[DMA_PROTECT_MAX];

static bool dma_dst_protected(word_t dst, word_t len)
{
	for (int i = 0; i < DMA_PROTECT_MAX; i++) {
		if (!dma_protect[i].active)
			continue;
		word_t lo = dma_protect[i].base;
		word_t hi = dma_protect[i].base + dma_protect[i].size;
		/* range overlap check: any byte of [dst, dst+len) inside */
		if (dst < hi && dst + len > lo)
			return true;
	}
	return false;
}

static proc_t *syscall_dma_protect(pid_t pid, word_t args[8])
{
	(void)pid;
	word_t idx = args[1];
	word_t base = args[2];
	word_t size = args[3];
	if (idx >= DMA_PROTECT_MAX) {
		args[0] = -1;
		return current;
	}
	dma_protect[idx].base = base;
	dma_protect[idx].size = size;
	dma_protect[idx].active = (size != 0);
	args[0] = 0;
	return current;
}

static proc_t *syscall_dma_sim(pid_t pid, word_t args[8])
{
	(void)pid;
	word_t dst = args[1];
	word_t src = args[2];
	word_t len = args[3];
	/* Monitor-vetted DMA gate: refuse transfers into protected ranges. */
	if (dma_dst_protected(dst, len)) {
		args[0] = -1; /* refused: destination is a protected range */
		return current;
	}
	/* M-mode memcpy: not gated by PMP (the limitation being demonstrated). */
	volatile uint8_t *d = (volatile uint8_t *)dst;
	const uint8_t *s = (const uint8_t *)src;
	for (word_t i = 0; i < len; i++)
		d[i] = s[i];
	args[0] = 0; /* success: the DMA transfer landed */
	return current;
}

/* Mirror app output into the USBFS CDC console (kern/platform/usbfs_cdc.c).
 * Weak so platforms without the driver link unchanged. */
extern void usbfs_cdc_putc(char c) __attribute__((weak));

static proc_t *syscall_dbg_putc(pid_t pid, word_t args[8])
{
	(void)pid;
	if (usbfs_cdc_putc)
		usbfs_cdc_putc((char)(args[1] & 0xFF));
	args[0] = 0;
	return current;
}

/**
 * Kernel-verified elapsed time, low 32 bits.
 */
static proc_t *syscall_now(pid_t pid, word_t args[8])
{
	(void)pid;
	args[0] = (word_t)rtc_get_time();
	return current;
}

/**
 * Handler type for system calls.
 */
typedef proc_t *(*handler_t)(pid_t pid, word_t args[8]);

/**
 * Handlers for individual system calls.
 */
handler_t handlers[] = {
	syscall_pid_get,
	syscall_vreg_get,
	syscall_vreg_set,
	syscall_sync,
	syscall_sleep_until,
	syscall_mem_introspect,
	syscall_tsl_introspect,
	syscall_mon_introspect,
	syscall_ipc_introspect,
	syscall_mem_derive,
	syscall_tsl_derive,
	syscall_mon_derive,
	syscall_ipc_derive,
	syscall_mem_revoke,
	syscall_tsl_revoke,
	syscall_mon_revoke,
	syscall_ipc_revoke,
	syscall_mem_delete,
	syscall_tsl_delete,
	syscall_mon_delete,
	syscall_ipc_delete,
	syscall_mem_pmp_get,
	syscall_mem_pmp_set,
	syscall_mem_pmp_clear,
	syscall_tsl_set,
	syscall_mon_suspend,
	syscall_mon_resume,
	syscall_mon_yield,
	syscall_mon_reg_get,
	syscall_mon_reg_set,
	syscall_mon_vreg_get,
	syscall_mon_vreg_set,
	syscall_mon_state_get,
	syscall_mon_mem_introspect,
	syscall_mon_tsl_introspect,
	syscall_mon_mon_introspect,
	syscall_mon_ipc_introspect,
	syscall_mon_mem_grant,
	syscall_mon_tsl_grant,
	syscall_mon_mon_grant,
	syscall_mon_ipc_grant,
	syscall_mon_mem_derive,
	syscall_mon_tsl_derive,
	syscall_mon_mon_derive,
	syscall_mon_ipc_derive,
	syscall_mon_mem_pmp_get,
	syscall_mon_mem_pmp_set,
	syscall_mon_mem_pmp_clear,
	syscall_mon_tsl_set,
	syscall_ipc_send,
	syscall_ipc_recv,
	syscall_ipc_call,
	syscall_ipc_reply,
	syscall_ipc_replyrecv,
	syscall_ipc_asend,
	syscall_ipc_arecv,
	syscall_dma_protect,
	syscall_dma_sim,
	syscall_dbg_putc,
	syscall_now,
};

/**
 * System call handler.
 */
proc_t *syscall_handler(void)
{
	// The system call number.
	word_t syscall_nr = current->regs.a0;

	// If system call number is invalid, make an exception.
	if (syscall_nr >= ARRAY_SIZE(handlers)) {
		return exception_handler(0x8, syscall_nr);
	}

	// Try to acquire a lock. Also checks for preemption.
	if (!lock_acquire(true)) {
		return NULL;
	}

	// Advance the program counter.
	current->regs.pc += 4;

	// Call the system call handler
	proc_t *next = handlers[syscall_nr](current->pid, &current->regs.a0);

	// Releases the lock.
	lock_release();

	return next;
}
