#include "s3k.h"

#include <stdio.h>

extern char __uart_base[]; // UART base address (0x10000000 on qemu_virt)

/*
 * NS16550A register layout (byte-stride on QEMU virt).
 * Directly mapped so the echo loop has zero libc buffering overhead.
 */
struct uart_regs {
	union {
		char rbr; /* Receiver buffer register (read only)  */
		char thr; /* Transmitter holding register (write only) */
	};
	char ier; /* Interrupt enabler register */
	union {
		char iir; /* Interrupt identification register (read only) */
		char fcr; /* FIFO control register (write only) */
	};
	char lcr;   /* Line control register */
	char __pad; /* MCR at offset 4, unused here */
	char lsr;   /* Line status register */
};

#define LSR_RX_READY 0x01 /* bit0: receiver data available */
#define LSR_TX_READY 0x60 /* bits5,6: THR empty + TEMT */

/* Grant the app RW PMP access to the UART MMIO window. */
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

int main(void)
{
	/* Make the UART MMIO region accessible from this app. */
	setup_uart(MAX_MEMORY_FUEL);

	volatile struct uart_regs *regs = (struct uart_regs *)__uart_base;

	/*
	 * Tight echo loop: read one byte from RBR, write the same byte
	 * back to THR. Polls LSR for RX-ready then TX-ready each iteration.
	 * No buffering, no syscalls per byte.
	 */
	while (1) {
		while (!(regs->lsr & LSR_RX_READY))
			;
		char c = regs->rbr;
		while (!(regs->lsr & LSR_TX_READY))
			;
		regs->thr = c;
	}

	return 0;
}
