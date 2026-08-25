#include "s3k.h"

#include <stdio.h>
#include <string.h>

extern char __uart_base[];

/* Assembly recovery entry (head.S): clears caller-saved GPRs + SP, then
 * calls resume_main. Its address is handed to the monitor at startup so
 * directed recovery redirects the PC here. */
extern void recovery_asm(void);

#define MSG_RESUME_ADDR 0x11
#define MSG_FAULT_REPORT 0x12
#define MSG_PARAM_SET   0x13 /* dynamic arm: announce PARAM_SET before write */

#ifdef S3K_DEMO_REPEAT
/* Repeat arm: number of attack rounds. After each recovery the attacker
 * re-enters its MAVLink service loop and attacks again; only after N blocked
 * rounds does it settle into normal service (functional recovery).
 * Default 10; the pressure arm (demopressure) overrides via
 * -DREPEAT_ATTACKS=100. */
#ifndef REPEAT_ATTACKS
#define REPEAT_ATTACKS 10
#endif
#endif

/* monotonic counter read for latency measurement (mcounteren=0xf in S3K) */
static inline uint64_t rdtime(void)
{
	uint64_t t;
	__asm__ volatile("rdtime %0" : "=r"(t));
	return t;
}

/* Attack target (PID2's gain region or code section, learnt from the wire via
 * PID1's handshake reply). Persists across recoveries (partition memory is
 * preserved by in-place reset), so a repeated attacker can re-attack. */
static s3k_word_t attack_target;

/* Number of attacks already launched. The repeat arm uses this in the
 * recovery entry to decide whether to attack again or enter the normal
 * service loop. */
static int attack_count; /* volatile? no: single-threaded partition */

/* One attack round; defined below. Forward-declared because resume_entry
 * (the recovery landing point) launches it in the repeat arm. */
static void attack_once(void);

/*
 * Attack narrative (matches CVE-2026-32709 / arXiv:2606.22289):
 * PID3 is the non-critical COMM partition. It parses a MAVLink message that
 * carries a PARAM_SET for the flight controller's attitude PID gains, then
 * performs the parameter write against the key task's memory. With PMP the
 * store traps (cause=7); without PMP it lands and the attitude loop diverges.
 *
 * The target address is not hardcoded: PID2 reports it to PID1 (monitor) at
 * startup, PID1 forwards it in the resume handshake reply (data[1]). PID3
 * learns the address from the wire, mirroring how a real attacker would.
 */
#define ATTACK_PAYLOAD (-1.5f) /* reverse roll feedback: Kp_ang[0] = -1.5 */

/* Recovery entry: PID1 redirects PID3's PC here via mon_reg_set after a
 * violation is captured and reported. Landing here means the closed loop
 * (capture -> report -> supervisor-directed recovery) completed.
 *
 * Repeat arm: the partition is reset in place (PC redirected here, memory
 * preserved). We re-enter the MAVLink service loop: count the round, and if
 * more attacks remain, launch the next one; after all N rounds are blocked
 * we settle into the normal service loop -- the attacker partition keeps
 * working, it just can never touch key memory. */
/* Evidence globals for the recovery-context-clearing check (data segment
 * survives in-place reset): g_att_preload records the malicious GPR value
 * the attacker planted before the faulting write; g_rec_ctx_ok is written
 * by recovery_asm with the cleared a0 (0), proving the clearing ran. */
volatile unsigned g_att_preload = 0xDEADu;
volatile unsigned g_rec_ctx_ok = 0xDEADu;

/* recovery_asm clears caller-saved GPRs + SP, then calls here. */
void resume_main(void)
{
	/* Re-establish UART PMP in case the recovery path cleared dynamic slots. */
	s3k_word_t base = (s3k_word_t)__uart_base;
	s3k_word_t addr = s3k_pmp_napot_encode(base, 0x20);
	s3k_mem_pmp_set(MAX_MEMORY_FUEL, 2, S3K_MEM_PERM_RW, addr);
	s3k_sync();
	attack_count++;
	printf("PID3 >>> recovery ctx cleared: attacker preload a0=0x%x -> recovered a0=0x%x, sp reset (count=%d)\n",
	       (unsigned)g_att_preload, (unsigned)g_rec_ctx_ok, attack_count);
	printf("PID3 >>> resumed at recovery entry (count=%d)\n", attack_count);
#ifdef S3K_DEMO_REPEAT
	if (attack_count <= REPEAT_ATTACKS) {
		/* persistent attacker: launch the next attack round. The first
		 * round was launched from main() with count=1; rounds 2..N are
		 * launched here after each recovery. */
		attack_once();
		/* only reached if the write unexpectedly landed (no-PMP combo) */
		puts("PID3 >>> write unexpectedly landed -- observing divergence");
		while (1) {
		}
	}
	/* All N rounds blocked and recovered: back to normal service. */
	puts("PID3 >>> all attack rounds blocked; entering MAVLink service loop");
	for (int beats = 0;; beats++) {
		s3k_sleep_until(0);
		if (beats % 100 == 0)
			printf("PID3 >>> service heartbeat beat=%d\n", beats);
	}
#else
	while (1) {
#ifdef PLATFORM_CH32V307
		/* No timer interrupt on QingKe V4F: preemption is poll-driven
		 * (kern/platform/ch32v307.c), so a pure spin can never be
		 * preempted and would freeze PID1/PID2 forever the moment the
		 * recovered attacker lands here. Yield the slice instead. */
		s3k_sleep_until(0);
#endif
	}
#endif
}

static s3k_word_t trap_stack[1024] __attribute__((aligned(16)));

/* client endpoint handed by PID1 via A0; trap handler reads it from here */
s3k_index_t client_ep_global;

/* PID3's trap handler: capture the PMP fault context, report to PID1, block. */
static void trap_handler(void) __attribute__((interrupt("machine")));
static void trap_handler(void)
{
	/* re-establish UART mapping (pattern from demo.violation) */
	s3k_word_t base = (s3k_word_t)__uart_base;
	s3k_word_t addr = s3k_pmp_napot_encode(base, 0x20);
	s3k_mem_pmp_set(MAX_MEMORY_FUEL, 2, S3K_MEM_PERM_RW, addr);
	s3k_sync();

	s3k_word_t epc = s3k_vreg_get(S3K_VREG_EPC);
	s3k_word_t esp = s3k_vreg_get(S3K_VREG_ESP);
	s3k_word_t ecause = s3k_vreg_get(S3K_VREG_ECAUSE);
	s3k_word_t eval = s3k_vreg_get(S3K_VREG_EVAL);
	uint64_t t_capture = rdtime();

	printf("PID3 >>> MAVLink param write BLOCKED: "
	       "epc=0x%lx esp=0x%lx ecause=0x%lx eval=0x%lx (t=0x%lx)\n",
	       (unsigned long)epc, (unsigned long)esp, (unsigned long)ecause,
	       (unsigned long)eval, (unsigned long)t_capture);
	if (ecause == 0x7)
		puts("PID3 >>> STORE/AMO access fault: PMP write violation");

	/* Report fault context to PID1 (supervisor). data[1] packs cause low 8
	 * bits and fault address in the rest. */
	s3k_msg_t msg = {};
	msg.data[0] = MSG_FAULT_REPORT;
	msg.data[1] = (eval << 8) | (ecause & 0xff);
	s3k_ipc_call(client_ep_global, &msg);

	puts("PID3 >>> fault reported, blocking for supervised recovery");
	while (1) {
	}
}

static void setup_trap(void)
{
	s3k_vreg_set(S3K_VREG_TPC, (s3k_word_t)trap_handler);
	s3k_vreg_set(S3K_VREG_TSP, (s3k_word_t)trap_stack + sizeof(trap_stack));
}

/*
 * Minimal MAVLink framing for the narrative: a real PARAM_SET over the wire
 * is a 0xFD-prefixed MAVLink2 frame with msgid 23 (PARAM_SET) whose payload
 * contains {target_system, target_component, param_id[16], param_value,
 * param_type}. We echo the parsed fields so the log shows the attack is a
 * parameter-injection, not a raw memory poke.
 */
static void mavlink_echo_param_set(float value)
{
	static const char param_id[] = "ANG_ROLL_P";
	union {
		float f;
		unsigned char b[4];
	} v;
	v.f = value;
	printf("PID3 >>> MAVLink frame: msgid=23 PARAM_SET "
	       "param_id=%s value=%.3f (0x%02x%02x%02x%02x)\n",
	       param_id, (double)value, v.b[0], v.b[1], v.b[2], v.b[3]);
}

/*
 * One attack round. Which write is performed depends on the arm:
 *   default       - direct store to the key-task gain (traps on PMP)
 *   S3K_DEMO_IO   - DMA-engine simulation (M-mode memcpy, not PMP-gated)
 *   S3K_DEMO_DYNAMIC - announce PARAM_SET to the monitor first, then store
 *   S3K_DEMO_CODE_RO - store an illegal instruction over PID2's control
 *                      routine (the "modify the attitude control logic"
 *                      attack surface; also traps on PMP when armed)
 * Callable repeatedly: after a recovery the partition is reset in place
 * (PC redirected here via resume_entry), its memory survives, so a
 * persistent attacker can launch the next round with the same target.
 */
static void attack_once(void)
{
	/* adversarial preload: plant a malicious value in a0 and t0 before the
	 * faulting write; recovery_asm must clear them so the recovered run
	 * cannot consume attacker-controlled register state. */
	__asm__ volatile("li a0, 0xBAD" ::: "a0");
	__asm__ volatile("li t0, 0xC0FFEE" ::: "t0");
	g_att_preload = 0xBADu;
	uint64_t t_attack = rdtime();
	printf("PID3 >>> attacking target @0x%lx (count=%d, t=0x%lx)\n",
	       (unsigned long)attack_target, attack_count, (unsigned long)t_attack);
#if defined(S3K_DEMO_IO) || defined(S3K_DEMO_IO_GUARD)
	/*
	 * I/O arm: PID3 does NOT write directly (that would trap on PMP). Instead
	 * it programs the simulated DMA engine (s3k_dma_sim -> M-mode memcpy) to
	 * move a malicious gain value into PID2's key memory. A bus-master DMA
	 * initiator is not the CPU hart, so PMP does not gate this transfer.
	 *
	 * demoio (unguarded): the transfer lands despite PID3 having no RW grant
	 * on the target -- the limitation standard PMP has against DMA.
	 * demoioguard (guarded): the monitor registered PID2's memory as a
	 * protected DMA range, so the kernel gate refuses the transfer (rc=-1)
	 * before the copy runs -- the positive verification of the layered
	 * defense. Same attack, same DMA engine, only the monitor's vetted
	 * descriptor policy differs.
	 */
	union { float f; uint8_t b[4]; } pv;
	pv.f = ATTACK_PAYLOAD;
	s3k_word_t local_buf;
	memcpy(&local_buf, pv.b, 4);
	uint64_t t0 = rdtime();
	int rc = s3k_dma_sim(attack_target, (s3k_word_t)&local_buf, 4);
	uint64_t t1 = rdtime();
	printf("PID3 >>> [I/O] DMA sim dst=0x%lx rc=%d (t=%lu->%lu, %lu ticks)\n",
	       (unsigned long)attack_target, rc,
	       (unsigned long)t0, (unsigned long)t1,
	       (unsigned long)(t1 - t0));
#ifdef S3K_DEMO_IO_GUARD
	puts("PID3 >>> [I/O guard] DMA transfer REFUSED by monitor-vetted gate");
	puts("PID3 >>> [I/O guard] key memory stays intact; layered defense works");
#else
	puts("PID3 >>> [I/O] DMA transfer landed -- PMP did not block it");
	puts("PID3 >>> [I/O] this is the limitation: monitor must vet DMA targets");
#endif
#elif defined(S3K_DEMO_DYNAMIC)
	/*
	 * dynamic-PMP arm: announce the PARAM_SET to the monitor first. The
	 * monitor scores it, revokes our gain-region grant if untrusted, then
	 * replies. We proceed to write regardless -- if the grant was revoked
	 * the store now traps on PMP (the CATE-style trust-drop took effect
	 * at runtime).
	 */
	s3k_msg_t msg = {};
	msg.data[0] = MSG_PARAM_SET;
	union { float f; uint32_t u; } pv;
	pv.f = ATTACK_PAYLOAD;
	msg.data[1] = (s3k_word_t)pv.u;
	mavlink_echo_param_set(ATTACK_PAYLOAD);
	printf("PID3 >>> announcing PARAM_SET to monitor (gain=%.3f)\n",
	       (double)ATTACK_PAYLOAD);
	s3k_ipc_call(client_ep_global, &msg);
	printf("PID3 >>> monitor replied, target gain @0x%lx; writing\n",
	       (unsigned long)msg.data[1]);
	attack_target = msg.data[1];
	printf("PID3 >>> writing %.3f to key-task gain @0x%lx (t=0x%lx)\n",
	       (double)ATTACK_PAYLOAD, (unsigned long)attack_target,
	       (unsigned long)t_attack);
	volatile float *target = (volatile float *)attack_target;
	*target = ATTACK_PAYLOAD;
	puts("PID3 >>> write unexpectedly landed");
#elif defined(S3K_DEMO_CODE_RO)
	/*
	 * Code-section arm: instead of the gain data, the attack overwrites the
	 * first instruction of PID2's control routine with an illegal encoding
	 * (0x00000000). This is the "modify the attitude control logic" attack
	 * surface. With PMP the store traps (cause=7); without PMP the control
	 * routine is corrupted and the key task faults when it next executes it.
	 */
	printf("PID3 >>> [code-RO] writing illegal instruction to control routine @0x%lx\n",
	       (unsigned long)attack_target);
	*(volatile uint32_t *)attack_target = 0x00000000u;
	puts("PID3 >>> [code-RO] code overwrite landed (no PMP)");
#else
	mavlink_echo_param_set(ATTACK_PAYLOAD);
	printf("PID3 >>> writing %.3f to key-task gain @0x%lx (t=0x%lx)\n",
	       (double)ATTACK_PAYLOAD, (unsigned long)attack_target,
	       (unsigned long)t_attack);
	volatile float *target = (volatile float *)attack_target;
	*target = ATTACK_PAYLOAD;
	puts("PID3 >>> write landed (no PMP protection)");
#endif
}

/* PID3 = comm (attacker). Handshake with PID1, then MAVLink param injection. */
int main(s3k_word_t client_ep)
{
	client_ep_global = (s3k_index_t)client_ep;

	printf("PID3 >>> up, client_ep=%u\n", (unsigned)client_ep);
	setup_trap();

	/* Round 1: hand PID1 our resume_entry address. */
	s3k_msg_t msg = {};
	msg.data[0] = MSG_RESUME_ADDR;
	msg.data[1] = (s3k_word_t)recovery_asm;
	puts("PID3 >>> sending resume_entry to PID1");
	s3k_ipc_call(client_ep_global, &msg);
	/* reply.data[1] carries the attack-target address PID2 reported. */
	attack_target = msg.data[1];
	printf("PID3 >>> ack received, target @0x%lx; launching MAVLink PARAM_SET "
	       "injection\n", (unsigned long)attack_target);

	/* Yield a few time slots so PID2's attitude loop ticks interleave. */
	for (int i = 0; i < 3; ++i) {
		s3k_sleep_until(0);
	}

	attack_count = 1;
	attack_once();
	puts("PID3 >>> observing attitude...");
	while (1) {
#ifdef PLATFORM_CH32V307
		/* No timer interrupt on CH32V307: yield so PID2/PID1 keep
		 * their slots (see app2/main.c). */
		s3k_sleep_until(0);
#endif
	}
}