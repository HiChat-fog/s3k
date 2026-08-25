#include "s3k.h"

#include <stdio.h>

extern char __uart_base[]; // UART base address.

// Configure PMP slot 2 to cover the UART region with read/write permissions.
static void setup_uart(int idx)
{
	s3k_word_t base = (s3k_word_t)__uart_base;
	s3k_word_t size = 0x20;
	s3k_word_t slot = 2;
	s3k_word_t perm = S3K_MEM_PERM_RW; // Read/write permissions.
	s3k_word_t addr = s3k_pmp_napot_encode(base, size);

	s3k_mem_pmp_set(idx, slot, perm, addr);
	s3k_sync();
	puts("uart set up!");
}

static s3k_word_t trap_stack[1024] __attribute__((aligned(16)));

// Recovery counter, incremented every time the violation is caught and
// execution is redirected here from the trap handler.
static unsigned recovery_count = 0;

// Resume entry point: execution lands here after the trap handler has
// redirected EPC away from the faulting store instruction.
static void resume_entry(void)
{
	recovery_count++;
	puts("execution RESUMED after violation capture+recovery");
	printf("recovery count = %u\n", recovery_count);
	puts("demo complete");
	// Never return: the app keeps running so the demo does not fall off the
	// end of the process image.
	while (1) {
	}
}

static void trap_handler(void) __attribute__((interrupt("machine")));
static void trap_handler(void)
{
	// The trap may run with the dynamically configured PMP slots cleared,
	// so re-establish the UART mapping before any output.
	setup_uart(MAX_MEMORY_FUEL);

	s3k_word_t epc = s3k_vreg_get(S3K_VREG_EPC);
	s3k_word_t esp = s3k_vreg_get(S3K_VREG_ESP);
	s3k_word_t ecause = s3k_vreg_get(S3K_VREG_ECAUSE);
	s3k_word_t eval = s3k_vreg_get(S3K_VREG_EVAL);

	printf("[VIOLATION CAPTURED] epc=0x%lx esp=0x%lx ecause=0x%lx eval=0x%lx\n",
	       epc, esp, ecause, eval);
	if (ecause == 0x7) {
		puts("  -> STORE/AMO access fault: PMP write violation");
	}
	printf("  -> recovered: redirecting EPC to resume entry 0x%lx\n",
	       (uint64_t)resume_entry);

	// Redirect the faulting context so that, on return, execution resumes in
	// the safe resume entry instead of retrying the forbidden store.
	s3k_vreg_set(S3K_VREG_EPC, (uint64_t)resume_entry);
}

void setup_trap(void (*trap_handler)(void), void *trap_stack_base, uint64_t trap_stack_size)
{
	// Sets the trap handler.
	s3k_vreg_set(S3K_VREG_TPC, (uint64_t)trap_handler);
	// Set the trap stack.
	s3k_vreg_set(S3K_VREG_TSP, ((uint64_t)trap_stack_base) + trap_stack_size);
}

int main(void)
{
	// Install the trap handler + trap stack (pattern from tutorial.03).
	setup_trap(trap_handler, trap_stack, 1024);
	// Map the UART (PMP slot 2). The default capability (slot 1) already maps
	// the app RAM at 0x80000000..0x81000000 RWX; 0x82000000 is outside every
	// permitted region, so a store there must trap.
	setup_uart(MAX_MEMORY_FUEL);

	puts("=== PMP violation capture + recovery demo ===");
	puts("attempting write to PMP-forbidden address 0x82000000...");

	// Deliberate store to an address no PMP slot authorises. A volatile
	// uint64_t pointer forces a real 'sd' (store) instruction, which raises a
	// STORE/AMO access fault (ecause=0x7) with eval equal to 0x82000000.
	volatile uint64_t *forbidden = (volatile uint64_t *)0x82000000;
	*forbidden = 0xdeadbeef;

	// The trap handler redirects EPC to resume_entry, so this is unreachable.
	while (1) {
	}
}
