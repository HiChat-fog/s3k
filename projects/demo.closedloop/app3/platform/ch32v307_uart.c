/* ch32v307_uart.c - WCH USART1 polled console for the CH32V307 platform.
 *
 * Register layout follows the CH32V307 reference manual (USART base
 * 0x40004400). Only the two registers needed for a polled console are
 * touched: STATR (status) and DATAR (data). Baud/word configuration is
 * left to the loader/pmp_probe stage so this driver stays inert until the
 * UART is actually initialized.
 * TODO(board): verify register offsets and bit positions on hardware. */
#include <stdio.h>
#include <s3k/syscall.h>

#define USART1_BASE 0x40013800u

#define USART_STATR (*(volatile unsigned int *)(USART1_BASE + 0x00))
#define USART_DATAR (*(volatile unsigned int *)(USART1_BASE + 0x04))

#define STATR_TXE (1u << 7)  /* Transmit data register empty */
#define STATR_TC  (1u << 6)  /* Transmission complete */
#define STATR_RXNE (1u << 5) /* Read data register not empty */

int __uart_putc(char c, FILE *f)
{
	(void)f;
	while (!(USART_STATR & STATR_TXE))
		;
	USART_DATAR = (unsigned char)c;
#ifdef PLATFORM_CH32V307
	/* Mirror every console byte to the kernel over the DBG_PUTC syscall so
	 * it streams out through the on-chip USBFS CDC console (no UART wiring
	 * needed - the board's own USB connector shows up as /dev/ttyACM*). */
	s3k_dbg_putc((unsigned char)c);
#endif
	return (unsigned char)c;
}

int __uart_getc(FILE *f)
{
	(void)f;
	if (USART_STATR & STATR_RXNE)
		return (int)(USART_DATAR & 0xff);
	return 0;
}

static FILE __stdio = FDEV_SETUP_STREAM(__uart_putc, __uart_getc, NULL, _FDEV_SETUP_RW);

FILE *const stdin = &__stdio;
__strong_reference(stdin, stdout);
__strong_reference(stdin, stderr);
