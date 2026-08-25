#include "s3k.h"

#include <stdio.h>
#include <math.h>

#include "quad_core.h"

extern char __uart_base[];

/* --- Attack target: PID gains, kept in a fixed, exported region ---------- */
/*
 * PID3 (attacker) will try to write these. The gains live in the data of the
 * PID2 ELF. PID2 reports the address to PID1 (monitor) over IPC at startup;
 * PID1 forwards it to PID3. This mirrors the real architecture where the
 * monitor knows the key memory layout and the attacker learns it from the
 * wire (MAVLink PARAM_SET carries param ids, not addresses — the mapping is
 * the victim side's job). Hardcoding the resolved ELF address here instead
 * would be fragile across builds, so we deliberately resolve it at runtime.
 */
float ATTACK_KP_ANG[3] = {6.9f, 6.9f, 25.f};

static void setup_uart(int idx)
{
	s3k_word_t base = (s3k_word_t)__uart_base;
	s3k_word_t size = 0x20;
	s3k_word_t slot = 2;
	s3k_word_t perm = S3K_MEM_PERM_RW; /* Read/write permissions */
	s3k_word_t addr = s3k_pmp_napot_encode(base, size);
	s3k_mem_pmp_set(idx, slot, perm, addr);
	s3k_sync();
}

/* Every N control steps, emit one log line with the attitude for plotting. */
#define LOG_EVERY 20

/* async client endpoint handed by PID1 via A0: used to report the attack
 * target address so no address is hardcoded in any ELF. */
static s3k_index_t async_ep;

int main(s3k_word_t a0)
{
	async_ep = (s3k_index_t)a0;
	qdrone_t q;
	pid3_t pos_p, ang_p;

	float Kp_pos[3] = {0.95f, 0.95f, 15.f};
	float Kd_pos[3] = {1.8f, 1.8f, 15.f};
	float Ki_pos[3] = {0.2f, 0.2f, 1.f};
	float Ki_sat_pos[3] = {1.1f, 1.1f, 1.1f};
	float Kp_ang[3] = {6.9f, 6.9f, 25.f};
	float Kd_ang[3] = {3.7f, 3.7f, 9.f};
	float Ki_ang[3] = {0.1f, 0.1f, 0.1f};
	float Ki_sat_ang[3] = {0.1f, 0.1f, 0.1f};

	/* start offset from hover, small angular perturbation (wind gust) */
	float pos[3] = {0.5f, -0.5f, 0.f};
	float vel[3] = {0.f, 0.f, 0.f};
	float ang[3] = {0.f, 0.f, 0.f};
	float ang_vel[3] = {0.05f, -0.08f, 0.03f};
	float dt = 0.01f; /* 100 Hz control loop */
	int i;

	setup_uart(MAX_MEMORY_FUEL);
	puts("PID2 >>> attitude loop up (key task)");

	quad_init(&q, pos, vel, ang, ang_vel, dt);
	pid_init(&pos_p, Kp_pos, Ki_pos, Kd_pos, Ki_sat_pos, dt);
	pid_init(&ang_p, Kp_ang, Ki_ang, Kd_ang, Ki_sat_ang, dt);

	/* Report the attack-target address to PID1 (monitor) via async IPC.
	 * Default arm: the exported gain region. Code-section arm
	 * (S3K_DEMO_CODE_RO): the address of quad_control_step itself, so the
	 * attacker targets the control *logic* (overwriting its first
	 * instruction) instead of the gain data -- the "modify the attitude
	 * control logic" attack surface. Either way no address is hardcoded in
	 * any ELF. */
#ifdef S3K_DEMO_CODE_RO
	s3k_word_t target = (s3k_word_t)quad_control_step;
	printf("PID2 >>> attack target = code section quad_control_step @ 0x%lx reported\n",
	       (unsigned long)target);
#else
	s3k_word_t target = (s3k_word_t)ATTACK_KP_ANG;
	printf("PID2 >>> attack target ATTACK_KP_ANG @ 0x%lx reported\n",
	       (unsigned long)target);
#endif
	s3k_ipc_asend(async_ep, target);

	/* 300 s simulated time, 100 Hz -> 30000 steps. Log every 100 (1 s). */
	for (i = 0; i < 30000; i++) {
		/*
		 * The P gain is read from the exported parameter region every
		 * tick, the same way a real flight controller reads its gains
		 * from a parameter store. If the attacker's write lands (no
		 * PMP), this is where the corrupted value enters the loop.
		 */
		ang_p.Kp[0] = ATTACK_KP_ANG[0];
		ang_p.Kp[1] = ATTACK_KP_ANG[1];
		ang_p.Kp[2] = ATTACK_KP_ANG[2];
		quad_control_step(&q, &pos_p, &ang_p);
		if (i % LOG_EVERY == 0) {
			printf("PID2 A %6.1f %8.3f %8.3f %8.3f\n",
			       q.time,
			       q.angle[0] * 180.0 / M_PI,
			       q.angle[1] * 180.0 / M_PI,
			       q.angle[2] * 180.0 / M_PI);
		}
#ifdef PLATFORM_CH32V307
		/* CH32V307 has no hardware timer interrupt (mip.MTIP never
		 * asserts, see kern preempt.h): a spinning U-mode loop would
		 * starve the other partitions forever. Yield every step so the
		 * scheduler can interleave PID3 (and PID1). QEMU keeps its
		 * interrupt-driven preemption. */
		s3k_sleep_until(0);
#endif
	}
	puts("PID2 >>> looptail");
	while (1) {
#ifdef PLATFORM_CH32V307
		s3k_sleep_until(0);
#endif
	}
}