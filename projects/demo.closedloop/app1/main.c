#include "s3k.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "sha256.h"

extern void app2_init(void); // Spawn PID2 (attitude, key) on monitor 8
extern void app3_init(void); // Spawn PID3 (comm, attacker) on monitor 16
extern char __uart_base[];
extern s3k_index_t pid3_gain_cap; /* dynamic arm: cap to revoke */

/*
 * Trusted-boot negative test.
 *
 * PID1 is the boot-time monitor. Before resuming PID2 (the key partition) it
 * measures the partition image and compares against a golden digest. Two
 * scenarios selected at build time:
 *   - golden matches the real image  -> boot proceeds (resume PID2)
 *   - a wrong golden (S3K_DEMO_BAD_BOOT) -> digest mismatch, boot refused
 */
#ifdef PLATFORM_CH32V307
#define PID2_IMAGE_BASE 0x20008000UL /* ch32: PID2 32K NAPOT region */
/* Measure only the actual image length: the tail of the 32K NAPOT region
 * (0x2000F000..0x20010000) overlaps the kernel's SRAM data window, which
 * holds live kernel state at boot-verify time, so hashing the full 0x8000
 * can never match a golden computed from image+zero-padding. */
#define PID2_IMAGE_SIZE 0x6288UL /* app2.bin length in bytes */
#else
#define PID2_IMAGE_BASE 0x80020000UL
#define PID2_IMAGE_SIZE 0x10000
#endif

#ifdef S3K_DEMO_BAD_BOOT
/* wrong golden: simulates a tampered image whose digest differs */
static const uint8_t golden[32] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
	0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
};
#else
/* correct golden: SHA-256 of the real PID2 image, computed offline. */
extern const uint8_t golden_ref[32];
static const uint8_t *golden = golden_ref;

/* Recovery-chain timestamps parked in globals so a debugger SRAM dump can
 * read the latency even with no console attached (the USBFS CDC console
 * carries the same values live once the board's USB cable is plugged in).
 * Units: rdtime() ticks @18 MHz (/18 -> microseconds). On QingKe V4F the
 * rdtime 64-bit result has an unspecified garbage HIGH word; the LOW 32
 * bits are the real tick counter, so every capture masks to 32 bits and
 * the globals are uint32_t. */
volatile uint32_t g_t_fault, g_t_recover, g_latency;
volatile uint32_t g_pmp_state;        /* PMP integrity check: 1=baseline PASS,
                                       * 2=divergence DETECTED, 3=restore PASS,
                                       * 0xF0-0xF3 = failure codes */
volatile uint32_t g_pmp_rogue_slot, g_pmp_rogue_perm, g_pmp_rogue_addr;

/* Same epoch, kernel-side clock: S3K_SYSCALL_NOW returns rtc_get_time()
 * from the kernel's STK-accumulated 18 MHz counter. Latching both here
 * resolves the rdtime timebase: rdtime_rate = (g_t_recover - g_t_fault)
 * / (g_s_recover - g_s_fault) * 18 MHz, and the latency in real
 * microseconds = (g_s_recover - g_s_fault) / 18. */
volatile uint32_t g_s_fault, g_s_recover, g_s_lat;
#endif

static int boot_verify_pid2(void)
{
	sha256_ctx c;
	uint8_t digest[32];
	int i, match;

	sha256_init(&c);
	sha256_update(&c, (const void *)PID2_IMAGE_BASE, PID2_IMAGE_SIZE);
	sha256_final(&c, digest);

	printf("PID1 >>> [boot-verify] measure PID2 image @0x%lx: ",
	       (unsigned long)PID2_IMAGE_BASE);
	for (i = 0; i < 32; i++)
		printf("%02x", digest[i]);
	printf("\n");

	match = (memcmp(digest, golden, 32) == 0);
	printf("PID1 >>> [boot-verify] %s\n",
	       match ? "PASS: digest matches golden, boot allowed"
		     : "REFUSE: digest mismatch, tampered image, boot refused");
	return match;
}

/* monotonic counter read for latency measurement (mcounteren=0xf in S3K) */
static inline uint64_t rdtime(void)
{
	uint64_t t;
	__asm__ volatile("rdtime %0" : "=r"(t));
	return t;
}

// PID1 = supervisor. Owns all initial capabilities (PID1 by S3K convention).
//
// Closed loop:
//  1. Spawn PID2 (attitude, key) and PID3 (comm, attacker), give each a time
//     slice so the scheduler runs them while PID1 blocks in replyrecv.
//  2. IPC pair: PID1 holds server (replyrecv), PID3 holds client (call).
//  3. First call from PID3 hands us its resume_entry address (so we can
//     redirect its PC there on recovery without hardcoding a symbol across
//     independent ELFs). We reply to unblock it.
//  4. PID3 then performs the unauthorized write, PMP traps, its trap handler
//     captures epc/ecause/eval and calls us with MSG_FAULT_REPORT.
//  5. We mon_reg_set PID3's PC to resume_entry and mon_resume it -> directed
//     recovery. PID2's heartbeat keeps ticking throughout (it has its own
//     time slice), proving the key chain is not interrupted.

static void setup_uart(int idx)
{
	s3k_word_t base = (s3k_word_t)__uart_base;
	s3k_word_t size = 0x20;
	s3k_word_t slot = 2;
	s3k_word_t perm = S3K_MEM_PERM_RW;
	s3k_word_t addr = s3k_pmp_napot_encode(base, size);
	s3k_mem_pmp_set(idx, slot, perm, addr);
	s3k_sync();
}

#define PID2_MON 8
#define PID3_MON 16

#define MSG_RESUME_ADDR 0x11
#define MSG_FAULT_REPORT 0x12
#define MSG_PARAM_SET   0x13 /* dynamic arm: PID3 announces the PARAM_SET */

int main(void)
{
	setup_uart(MAX_MEMORY_FUEL);
	puts("PID1 >>> supervisor up");

#ifdef S3K_DEMO_IO_GUARD
	/*
	 * I/O guard arm: register PID2's key memory as a protected DMA range
	 * before any partition runs. Every DMA-sim transfer whose destination
	 * falls inside is refused by the kernel gate -- this is the positive
	 * verification of "the monitor vets DMA descriptor targets" (the I/O
	 * layered defense; demo-level stand-in for IOPMP/IOMMU).
	 */
	int prc = s3k_dma_protect(0, PID2_IMAGE_BASE, PID2_IMAGE_SIZE);
	printf("PID1 >>> [I/O guard] protected DMA range 0x%lx+0x%lx rc=%d\n", (unsigned long)PID2_IMAGE_BASE, (unsigned long)PID2_IMAGE_SIZE,
	       prc);
#endif

	puts("PID1 >>> spawning PID2 (attitude, key)");
	app2_init();
	puts("PID1 >>> spawning PID3 (comm, attacker)");
	app3_init();

	// Two IPC endpoints:
	//  - async server for PID2: it reports the address of its attitude-gain
	//    region (the attack target) without blocking. PID1 forwards that
	//    address to PID3 in the resume handshake, so no address is hardcoded
	//    in any ELF and the monitor mediating key memory layout is visible.
	//  - bsync server for PID3: resume_entry handshake + fault report.
	s3k_ipc_flag_t flags = S3K_IPC_FLAG_YIELD;
	s3k_ipc_mode_t mode = S3K_IPC_MODE_BSYNC;
	int server = s3k_ipc_derive(0, 2, mode, flags);
	int client = s3k_ipc_derive(server, 1, mode, flags);
	s3k_mon_ipc_grant(PID3_MON, client);
	s3k_mon_reg_set(PID3_MON, S3K_REG_A0, (s3k_word_t)client);

	int asrv = s3k_ipc_derive(0, 3, S3K_IPC_MODE_ASYNC, flags);
	int aclt = s3k_ipc_derive(asrv, 1, S3K_IPC_MODE_ASYNC, flags);
	s3k_mon_ipc_grant(PID2_MON, aclt);
	s3k_mon_reg_set(PID2_MON, S3K_REG_A0, (s3k_word_t)aclt);

	// Trusted-boot verification: measure PID2's image; if the digest does
	// not match the golden value, refuse to resume the key partition.
	if (!boot_verify_pid2()) {
		puts("PID1 >>> [boot-verify] key partition NOT started (tampered image)");
		while (1) {
#ifdef PLATFORM_CH32V307
			s3k_sleep_until(0); /* no timer IRQ: don't starve others */
#endif
		}
	}

	// Resume PID2 and PID3, give each a time slice (tutorial.05 pattern).
	// tsl_derive splits size slots off PID1's schedule; PID1's total is 32,
	// so we give PID2 and PID3 8 each and keep 16 for PID1. Giving either
	// the full 32 leaves nothing for the other (tsl_derive returns -2).
#ifdef S3K_DEMO_LOAD
	/* Load-sensitivity arm: tighter schedule, 4 slots each, PID1 keeps 24. */
	int slot2 = 4, slot3 = 4;
#else
	int slot2 = 8, slot3 = 8;
#endif
	s3k_mon_resume(PID2_MON);
	int rt2 = s3k_mon_tsl_derive(PID2_MON, 0, 1, true, slot2);
	printf("PID1 >>> tsl_derive PID2=%d\n", rt2);
	/* Let PID2 run one step so its async report of the gain region lands
	 * before we arecv below (PID2 asends at startup). */
	s3k_mon_yield(PID2_MON);
	s3k_mon_resume(PID3_MON);
	int rt3 = s3k_mon_tsl_derive(PID3_MON, 0, 1, true, slot3);
	printf("PID1 >>> tsl_derive PID3=%d\n", rt3);
	puts("PID1 >>> PID2/PID3 resumed + scheduled");

	// Receive PID2's reported attack-target address (async, non-blocking).
	// Default arm: the gain region. Code-section arm: quad_control_step.
	s3k_word_t pid2_gains = 0;
	s3k_ipc_arecv(asrv, &pid2_gains);
	printf("PID1 >>> PID2 attack target @ 0x%lx\n", (unsigned long)pid2_gains);

	// Round 1: receive PID3's resume_entry address.
	s3k_msg_t msg = {};
	s3k_ipc_replyrecv(server, &msg);
	if (msg.data[0] != MSG_RESUME_ADDR) {
		printf("PID1 >>> FAIL: expected resume addr, got 0x%lx\n", (unsigned long)msg.data[0]);
		while (1) {}
	}
	s3k_word_t pid3_resume = msg.data[1];
	printf("PID1 >>> PID3 resume_entry = 0x%lx\n", (unsigned long)pid3_resume);

	// Round 2: reply (unblock PID3 so it goes for the MAVLink PARAM_SET write,
	// forwarding the attack-target address in data[1]), then block-receive the
	// fault report.
	msg.data[0] = 0; // ack payload
#ifdef PLATFORM_CH32V307
	/* ch32 fixed layout: ATTACK_KP_ANG lives at this address in PID2's data
	 * segment (app2/platform/ch32v307.ld; keep in sync with `nm` whenever
	 * app2 relinks). The PID2-asend -> PID1-arecv forwarding is unreliable
	 * on this board, so the monitor passes the known address directly.
	 * QEMU keeps the reported-address flow. */
	msg.data[1] = 0x2000E268UL; /* attack target address */
#else
	msg.data[1] = pid2_gains; /* attack target address */
#endif

#if defined(S3K_DEMO_NO_PMP) || defined(S3K_DEMO_IO) || defined(S3K_DEMO_IO_GUARD)
	/*
	 * no-PMP / I/O arm: no fault is expected.
	 *  - no-PMP: the write is granted, PID3 never traps.
	 *  - I/O:   PID3 programs the DMA engine instead of writing; the M-mode
	 *           transfer is not gated by PMP, so it lands without a fault.
	 *  - I/O guard: the transfer is refused by the monitor-vetted DMA gate
	 *           (registered protected range), also without a PMP fault.
	 * Either way the monitor cannot rely on a PMP trap here -- that is the
	 * point. Reply once and observe the key partition's behaviour.
	 */
	s3k_ipc_reply(server, &msg);
#ifdef S3K_DEMO_IO_GUARD
	puts("PID1 >>> [I/O guard] DMA gate active; observing key partition");
#elif defined(S3K_DEMO_IO)
	puts("PID1 >>> [I/O] DMA path: PMP cannot block it; observing (limitation)");
#else
	puts("PID1 >>> [no-PMP] write granted, no fault expected; observing divergence");
#endif
	while (1) {
#ifdef PLATFORM_CH32V307
		/* No timer interrupt on QingKe V4F: preemption is poll-driven
		 * (kern/platform/ch32v307.c), so a bare spin would starve PID2
		 * and freeze the SysTick poll clock. Yield the slice instead. */
		s3k_sleep_until(0);
#endif
	}
#elif defined(S3K_DEMO_DYNAMIC)
	/*
	 * dynamic-PMP arm (CATE-style trust scoring): PID3 announces the
	 * PARAM_SET before writing. The monitor scores it: a negative roll gain
	 * is untrusted, so it dynamically revokes PID3's gain-region capability
	 * (s3k_mon_mem_pmp_clear) and only then lets PID3 proceed. The write now
	 * traps on PMP even though PID3 held a trusted RW grant a moment earlier
	 * -- the trust score dropped, so the permission was withdrawn.
	 */
	s3k_ipc_replyrecv(server, &msg);
	if (msg.data[0] != MSG_PARAM_SET) {
		printf("PID1 >>> FAIL: expected PARAM_SET announce, got 0x%lx\n",
		       (unsigned long)msg.data[0]);
		while (1) {}
	}
	/* trust score: payload is the float bit pattern; negative = untrusted */
	int32_t raw = (int32_t)msg.data[1];
	float gain;
	memcpy(&gain, &raw, sizeof(gain));
	printf("PID1 >>> [dynamic-PMP] scoring PARAM_SET: gain=%ld/1000\n", (long)(gain * 1000));
	if (gain < 0.f) {
		int rc = s3k_mon_mem_pmp_clear(PID3_MON, pid3_gain_cap);
		printf("PID1 >>> [dynamic-PMP] untrusted: revoked gain cap rc=%d\n", rc);
	} else {
		puts("PID1 >>> [dynamic-PMP] trusted gain, no revocation");
	}
	/* replyrecv: reply to the PARAM_SET announce (unblock PID3, it proceeds to
	 * write and now traps) and then receive the fault report from the revoked
	 * write, atomically. This matches the S3K server pattern. */
	msg.data[0] = 0;
	msg.data[1] = pid2_gains;
	s3k_msg_t msg2 = {};
	int rrecv = s3k_ipc_replyrecv(server, &msg2);
	printf("PID1 >>> [dynamic-PMP] replyrecv(2) rc=%d tag=0x%lx\n", rrecv,
	       (unsigned long)msg2.data[0]);
	if (msg2.data[0] != MSG_FAULT_REPORT) {
		printf("PID1 >>> FAIL: expected fault report, got 0x%lx\n",
		       (unsigned long)msg2.data[0]);
		while (1) {}
	}
	s3k_word_t ecause = msg2.data[1] & 0xff;
	s3k_word_t eval = msg2.data[1] >> 8;
	uint64_t t_fault = rdtime();
	printf("PID1 >>> fault reported: ecause=0x%lx eval=0x%lx (t=0x%lx)\n",
	       (unsigned long)ecause, (unsigned long)eval, (unsigned long)t_fault);

	/* directed recovery (in-place reset, same as the static arm) */
	int rs = s3k_mon_suspend(PID3_MON);
	int rr1 = s3k_mon_reg_set(PID3_MON, S3K_REG_PC, pid3_resume);
	int rr2 = s3k_mon_resume(PID3_MON);
	uint64_t t_recover = rdtime();
	printf("PID1 >>> recovered PID3: suspend=%d reg_set=%d resume=%d (t=0x%lx)\n",
	       rs, rr1, rr2, (unsigned long)t_recover);
	puts("PID1 >>> closed loop complete (dynamic)");
	while (1) {
#ifdef PLATFORM_CH32V307
		/* poll-driven preemption (no timer interrupt on QingKe V4F):
		 * yield instead of an arch spin so PID2's loop keeps ticking
		 * after the monitor parks. */
		s3k_sleep_until(0);
#endif
	}
#else
#ifdef S3K_DEMO_REPEAT
#ifndef REPEAT_ATTACKS
#define REPEAT_ATTACKS 10
#endif
	/*
	 * repeat-attack arm: the same capture -> report -> directed-recovery
	 * loop runs N times. After each recovery the attacker re-enters its
	 * service loop and attacks again; every round must be blocked by PMP
	 * and the key chain (PID2's attitude loop) must keep ticking
	 * throughout. Statistics (rounds, block rate, per-round recovery
	 * latency) are printed at the end, then PID3 settles into normal
	 * service.
	 */
	printf("PID1 >>> [repeat-attack] starting attack loop (%d rounds)\n", REPEAT_ATTACKS);
	uint64_t t_fault_min = UINT64_MAX, t_fault_max = 0, t_fault_sum = 0;
	int rounds = 0;
	for (rounds = 1; rounds <= REPEAT_ATTACKS; rounds++) {
		s3k_ipc_replyrecv(server, &msg);
		if (msg.data[0] != MSG_FAULT_REPORT) {
			printf("PID1 >>> FAIL: expected fault report, got 0x%lx\n",
			       (unsigned long)msg.data[0]);
			while (1) {}
		}
		s3k_word_t ecause = msg.data[1] & 0xff;
		s3k_word_t eval = msg.data[1] >> 8;
		uint64_t t_fault = rdtime();
		printf("PID1 >>> [round %d] fault reported: ecause=0x%lx eval=0x%lx (t=0x%lx)\n",
		       rounds, (unsigned long)ecause, (unsigned long)eval,
		       (unsigned long)t_fault);

		/* directed recovery: same in-place reset as the static arm */
		int rs = s3k_mon_suspend(PID3_MON);
		int rr1 = s3k_mon_reg_set(PID3_MON, S3K_REG_PC, pid3_resume);
		int rr2 = s3k_mon_resume(PID3_MON);
		uint64_t t_recover = rdtime();
		uint64_t lat = t_recover - t_fault;
		if (lat < t_fault_min)
			t_fault_min = lat;
		if (lat > t_fault_max)
			t_fault_max = lat;
		t_fault_sum += lat;
		printf("PID1 >>> [round %d] recovered PID3: suspend=%d reg_set=%d "
		       "resume=%d (t=0x%lx, latency=%lu)\n",
		       rounds, rs, rr1, rr2, (unsigned long)t_recover,
		       (unsigned long)lat);
	}
	printf("PID1 >>> [repeat-attack] rounds=%d blocked=%d latency(min/avg/max)=%lu/%lu/%lu ticks\n",
	       rounds - 1, rounds - 1, (unsigned long)t_fault_min,
	       (unsigned long)(t_fault_sum / REPEAT_ATTACKS), (unsigned long)t_fault_max);

	/* let PID3 run its service loop so the functional recovery is visible */
	s3k_mon_yield(PID3_MON);
	puts("PID1 >>> closed loop complete (repeat)");
	while (1) {
	}
#else
	s3k_ipc_replyrecv(server, &msg);
	if (msg.data[0] != MSG_FAULT_REPORT) {
		printf("PID1 >>> FAIL: expected fault report, got 0x%lx\n", (unsigned long)msg.data[0]);
		while (1) {}
	}
	s3k_word_t ecause = msg.data[1] & 0xff;
	s3k_word_t eval = msg.data[1] >> 8;
	uint64_t t_fault = rdtime();
	uint32_t s_fault = s3k_now();
#ifndef S3K_DEMO_NO_FAULT_LOG
	printf("PID1 >>> fault reported: ecause=0x%lx eval=0x%lx (t=0x%lx)\n",
	       (unsigned long)ecause, (unsigned long)eval, (unsigned long)t_fault);
#endif

		// Directed recovery. PID3 is currently BLOCKED on the IPC call it made
		// from its trap handler. mon_resume only clears SUSPENDED, not BLOCKED,
		// so we first mon_suspend (which clears BLOCKED via state &= ACQUIRED,
		// then sets SUSPENDED), then set PC to resume_entry, then resume: an
		// in-place reset at PC + resume granularity.
	int rs = s3k_mon_suspend(PID3_MON);
	int rr1 = s3k_mon_reg_set(PID3_MON, S3K_REG_PC, pid3_resume);
	int rr2 = s3k_mon_resume(PID3_MON);
	uint64_t t_recover = rdtime();
	uint32_t s_recover = s3k_now();
	g_t_fault = (uint32_t)t_fault;
	g_t_recover = (uint32_t)t_recover;
		/* latency from the 32-bit masks only - rdtime's high word is
		 * garbage on QingKe V4F (see globals comment), never let it
		 * enter the math */
		g_latency = g_t_recover - g_t_fault;
		/* STK pair sits adjacent to the rdtime pair (constant instruction
		 * skew cancels in both deltas), so both deltas cover the same
		 * printf+recovery window: rdtime_rate = g_latency/g_s_lat * 18 MHz. */
	g_s_fault = s_fault;
	g_s_recover = s_recover;
	g_s_lat = g_s_recover - g_s_fault;
	printf("PID1 >>> recovered PID3: suspend=%d reg_set=%d resume=%d (t=0x%lx)\n",
	       rs, rr1, rr2, (unsigned long)t_recover);

	// Yield to PID3 so it actually runs resume_entry before PID1 parks.
	s3k_mon_yield(PID3_MON);
	s3k_word_t st = 0;
	s3k_mon_state_get(PID3_MON, &st);
	printf("PID1 >>> PID3 state after recovery=0x%lx\n", (unsigned long)st);

	puts("PID1 >>> closed loop complete");

#ifdef S3K_DEMO_PMP_CHECK
	/* PMP region under check: PID2's RAM (base/size per platform layout) */
#define PMPCHK_BASE PID2_IMAGE_BASE
#define PMPCHK_SIZE 0x8000UL
	/*
	 * PMP configuration-integrity self-check (equivalent-fault experiment).
	 * Readback verification of the monitor's view of PID3's PMP entries:
	 *   state 1: baseline readback PASS  (no entry covers PID2 RAM)
	 *   state 2: after injecting an equivalent fault (a rogue RW entry over
	 *            PID2's RAM, as a physical glitch flipping the config would),
	 *            readback DETECTS the divergence
	 *   state 3: after revoking the rogue entry, readback PASS again
	 * All stages latch into g_pmp_* globals (SRAM-readable with no console).
	 */
	{
		s3k_pmp_slot_t slot; s3k_mem_perm_t perm; s3k_pmp_addr_t addr;
		int rogue_at = -1;

		/* baseline readback: PID3 must have no writable entry over PID2 RAM */
		int bad = 0;
		for (int j = 0; j < 16; j++) {
			if (s3k_mon_mem_pmp_get(PID3_MON, j, &slot, &perm, &addr) < 0)
				continue;
			if ((perm & S3K_MEM_PERM_RW) == S3K_MEM_PERM_RW &&
			    addr == s3k_pmp_napot_encode(PMPCHK_BASE, PMPCHK_SIZE))
				bad = 1;
		}
		g_pmp_state = bad ? 0xF0 : 1;
		printf("PID1 >>> [pmp-check] baseline: %s\n", bad ? "TAMPERED" : "PASS");

		/* inject equivalent fault: rogue RW entry over PID2 RAM (slot 3,
		 * free in the static arm) via a derived capability, exactly as a
		 * glitch-flipped config would grant */
		s3k_index_t rogue = s3k_mon_mem_derive(16, 0, 1, S3K_MEM_PERM_RW,
						       PMPCHK_BASE, PMPCHK_SIZE);
		if (rogue >= 0) {
			s3k_word_t napat = s3k_pmp_napot_encode(PMPCHK_BASE, PMPCHK_SIZE);
			int rc = s3k_mon_mem_pmp_set(16, rogue, 3, S3K_MEM_PERM_RW, napat);
			/* readback must now DETECT the divergence */
			bad = 0;
			for (int j = 0; j < 16; j++) {
				if (s3k_mon_mem_pmp_get(PID3_MON, j, &slot, &perm, &addr) < 0)
					continue;
				if ((perm & S3K_MEM_PERM_RW) == S3K_MEM_PERM_RW &&
				    addr == napat) {
					bad = 1;
					rogue_at = j;
				}
			}
			g_pmp_state = bad ? 2 : 0xF1;
			g_pmp_rogue_slot = (uint32_t)slot;
			g_pmp_rogue_perm = (uint32_t)perm;
			g_pmp_rogue_addr = (uint32_t)addr;
			printf("PID1 >>> [pmp-check] after fault inject: %s (slot=%d)\n",
			       bad ? "DETECTED" : "MISSED", rogue_at);
			/* restore: revoke the rogue entry, readback must PASS again */
			s3k_mon_mem_pmp_clear(16, rogue);
			bad = 0;
			for (int j = 0; j < 16; j++) {
				if (s3k_mon_mem_pmp_get(PID3_MON, j, &slot, &perm, &addr) < 0)
					continue;
				if ((perm & S3K_MEM_PERM_RW) == S3K_MEM_PERM_RW &&
				    addr == napat)
					bad = 1;
			}
			g_pmp_state = bad ? 0xF2 : 3;
			printf("PID1 >>> [pmp-check] after restore: %s\n",
			       bad ? "STILL TAMPERED" : "PASS");
		} else {
			g_pmp_state = 0xF3;
			printf("PID1 >>> [pmp-check] derive failed\n");
		}
	}
#endif

	while (1) {
#ifdef PLATFORM_CH32V307
		/* No timer interrupt on QingKe V4F: preemption is poll-driven
		 * (kern/platform/ch32v307.c), so a bare spin would never be
		 * preempted and would starve PID2/PID3 and stop the SysTick
		 * poll clock. Yield the slice instead. */
		s3k_sleep_until(0);
#endif
	}
#endif
#endif
}
