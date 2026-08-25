.globl _start

.section .text.init

_start:
	.option push
	.option norelax
	la	gp,__global_pointer$
	.option pop
	// Set up the stack pointer
	.option push
	.option norelax
	la	sp,__stack_top
	.option pop
	
#ifdef PLATFORM_CH32V307
	// Copy .data from its flash LMA to the SRAM VMA (code runs from flash,
	// writable state lives in SRAM -- see ch32v307.ld).
	la	t0,_data
	la	t1,_end
	la	t2,_data_lma
1:	bgeu	t0,t1,2f
	lw	t3,0(t2)
	sw	t3,0(t0)
	addi	t0,t0,4
	addi	t2,t2,4
	j	1b
2:
#endif

	// Zero .bss (the hardware loader does not do it for us).
	la	t0,_bss
	la	t1,_end
1:	bgeu	t0,t1,2f
	sb	x0,0(t0)
	addi	t0,t0,1
	j	1b
2:

	// Call main function
	call	main
_hang:
	// Infinite loop to hang the program
	j 	_hang
