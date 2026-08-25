
/*
 * recovery_asm - directed-recovery landing point (PC is redirected here by
 * the monitor's reg_set after a PMP fault). Clears the caller-saved GPRs
 * (a0-a7, t0-t6) and ra, resets SP to the clean stack top, and records the
 * cleared a0 into g_rec_ctx_ok so the C side can log the evidence. This
 * closes the context-residue window: register values an attacker preloaded
 * before the faulting write no longer survive into the recovered run.
 */
.globl recovery_asm
.section .text
recovery_asm:
	li	a0, 0
	li	a1, 0
	li	a2, 0
	li	a3, 0
	li	a4, 0
	li	a5, 0
	li	a6, 0
	li	a7, 0
	li	t0, 0
	li	t1, 0
	li	t2, 0
	li	t3, 0
	li	t4, 0
	li	t5, 0
	li	t6, 0
	li	ra, 0
	la	sp, __stack_top
	la	t6, g_rec_ctx_ok
	sw	a0, 0(t6)
	call	resume_main
_hang_rec:
	j	_hang_rec

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
