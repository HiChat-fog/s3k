#include "s3k.h"

#include <stdio.h>

extern char __uart_base[]; // UART base address

/* Partition spawn layout. QEMU virt keeps the 64KB-per-partition layout the
 * run_all.sh assertions were written against; the CH32V307 port shrinks the
 * regions to fit the image sizes inside the 64KB SRAM:
 *   PID2 (attitude/key) 0x20000800 +26K
 *   PID3 (comm/attacker) 0x20007000 +16K
 *   PID1 itself links at 0x2000B000 +14K (app1/platform/ch32v307.ld). */
#ifdef PLATFORM_CH32V307
#define PID2_RAM_BASE 0x20008000UL /* 32K, NAPOT-aligned (image 24.6K) */
#define PID2_RAM_SIZE 0x8000UL
#define PID3_RAM_BASE 0x20004000UL /* 16K, NAPOT-aligned (image 14.3K) */
#define PID3_RAM_SIZE 0x4000UL
#else
#define PID2_RAM_BASE 0x80020000UL
#define PID2_RAM_SIZE 0x10000UL
#define PID3_RAM_BASE 0x80040000UL
#define PID3_RAM_SIZE 0x10000UL
#endif

// Helper: derive a memory capability under a monitor and pin a PMP slot to it.
// Pattern copied verbatim from tutorial.06.ipc/app1/init2.c.
void mem_init(s3k_word_t mon_idx, s3k_word_t idx, s3k_word_t slot, s3k_word_t cfree, s3k_word_t perm, s3k_word_t base,
	     s3k_word_t size)
{
	idx = s3k_mon_mem_derive(mon_idx, idx, cfree, perm, base, size);
	if (idx < 0) {
		printf("Failed to derive memory capability %lx\n", (unsigned long)base);
		return;
	}

	s3k_word_t addr = s3k_pmp_napot_encode(base, size);
	int err = s3k_mon_mem_pmp_set(mon_idx, idx, slot, perm, addr);
	if (err < 0) {
		printf("Failed to set PMP for derived memory %lx, err=%d\n", (unsigned long)base, err);
		return;
	}
}

// Spawn a process at a given RAM base/size on a given monitor index.
// Mirrors tutorial.06's app2_init, parameterised so we can spawn PID2 and PID3.
static void spawn_at(s3k_word_t mon_idx, s3k_word_t ram_base, s3k_word_t ram_size)
{
	s3k_word_t ram_perm = S3K_MEM_PERM_RWX;
	s3k_word_t uart_base = (s3k_word_t)__uart_base;
	s3k_word_t uart_size = 0x20;

	// RAM: slot 1, fuel 1
	mem_init(mon_idx, 0, 1, 1, ram_perm, ram_base, ram_size);
	// UART: slot 2, fuel 1
	mem_init(mon_idx, 16, 2, 1, S3K_MEM_PERM_RW, uart_base, uart_size);

	if (s3k_mon_reg_set(mon_idx, S3K_REG_PC, ram_base) != 0) {
		printf("Failed to set PC for monitor %lu\n", (unsigned long)mon_idx);
		return;
	}
}

// PID2 (attitude, key) @ 0x80020000, monitor index 8
void app2_init(void)
{
	spawn_at(8, PID2_RAM_BASE, PID2_RAM_SIZE);
}

// PID3 (comm, attacker) @ 0x80040000, monitor index 16 (pid3 = idx 2*MAX_MONITOR_FUEL)
//
// With PMP (default): PID3's RAM capability is only its own 0x80040000..0x80050000,
// so any store to PID2's gain region traps with ecause=0x7 (the "with-PMP" arm).
//
// With -Ddemonopmp=true: PID3 *additionally* gets a second memory capability
// spanning PID2's RAM, so the PARAM_SET store against 0x80025318 lands and the
// loop diverges (the "no-PMP" arm). PID3's own image still loads at 0x80040000.
// This is the switch that makes the two-arm comparison real: the *same* MAVLink
// PARAM_SET, the same PID2 workload, only the PMP grant differs.
/* PID3's capability index for the gain-region access (dynamic arm), so the
 * monitor can revoke it at runtime. Set by app3_init. */
s3k_index_t pid3_gain_cap = -1;

void app3_init(void)
{
	puts("PID1 >>> spawning PID3 (comm, attacker)");
	spawn_at(16, PID3_RAM_BASE, PID3_RAM_SIZE);
#ifdef S3K_DEMO_NO_PMP
	/* second capability: RW over PID2's RAM (0x80020000..0x80030000),
	 * mimicking a system with no partition isolation. */
	mem_init(16, 0, 3, 1, S3K_MEM_PERM_RW, PID2_RAM_BASE, PID2_RAM_SIZE);
	puts("PID1 >>> [no-PMP arm] PID3 granted RW over PID2 RAM too");
#elif defined(S3K_DEMO_DYNAMIC)
	/* dynamic-PMP arm: PID3 starts trusted with RW over the gain region.
	 * The monitor will revoke this capability when it scores the PARAM_SET
	 * as untrusted (CATE-style trust scoring -> permission adjustment). */
	pid3_gain_cap = s3k_mon_mem_derive(16, 0, 1, S3K_MEM_PERM_RW,
					   PID2_RAM_BASE, PID2_RAM_SIZE);
	s3k_word_t addr = s3k_pmp_napot_encode(PID2_RAM_BASE, PID2_RAM_SIZE);
	s3k_mon_mem_pmp_set(16, pid3_gain_cap, 3, S3K_MEM_PERM_RW, addr);
	puts("PID1 >>> [dynamic-PMP arm] PID3 granted RW over PID2 gain region (trusted)");
#else
	puts("PID1 >>> [with-PMP arm] PID3 isolated to own RAM");
#endif
}
