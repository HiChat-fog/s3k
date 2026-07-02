project('tutorial.04.timing_cap', 'c',
	version: '0.1',
	meson_version: '>=1.1.0',
	default_options: [
		'buildtype=debugoptimized',
		'c_std=gnu11',
	]
)

s3k = subproject('s3k')
libs3k_dep = s3k.get_variable('lib_dep')
s3k_elf = s3k.get_variable('elf')

subdir('app1')
subdir('app2')

gdb_riscv64 = find_program('riscv64-unknown-elf-gdb', required: false)
run_target(
	'gdb-run',
	command: [
		gdb_riscv64,
		'-ex', 'set confirm off',
		'-ex', 'set pagination off',
		'-ex', 'target extended-remote 127.0.0.1:3333',
		'-ex', 'set *(int*)0x3001000 = 0x0f',
		'-ex', 'set *(int*)0x3001010 = 0x1',
		'-ex', 'load ' + app1_elf.full_path(),
		'-ex', 'load ' + app2_elf.full_path(),
		'-ex', 'load ' + s3k_elf.full_path(),
		'-ex', 'continue',
	],
	depends : [s3k_elf, app1_elf, app2_elf],
)

qemu_system_riscv64 = find_program('qemu-system-riscv64', required: false)
run_target(
	'qemu-run',
	command: [
		qemu_system_riscv64,
		'-machine', 'virt',
		'-bios', 'none',
		'-kernel', s3k_elf.full_path(),
		'-nographic',
		'-m', '1G',
		'-icount', '1',
		'-device', 'loader,file=' + app1_elf.full_path(),
		'-device', 'loader,file=' + app2_elf.full_path(),
		'-device', 'loader,addr=0x90000000,cpu-num=0',
	],
	depends : [s3k_elf, app1_elf, app2_elf],
)

qemu_system_riscv64 = find_program('qemu-system-riscv64', required: false)
run_target(
	'qemu-debug',
	command: [
		qemu_system_riscv64,
		'-machine', 'virt',
		'-bios', 'none',
		'-kernel', s3k_elf.full_path(),
		'-nographic',
		'-m', '1G',
		'-icount', '1',
		'-device', 'loader,file=' + app1_elf.full_path(),
		'-device', 'loader,file=' + app2_elf.full_path(),
		'-device', 'loader,addr=0x90000000,cpu-num=0',
		'-gdb', 'tcp::3333',
        '-S',
	],
	depends : [s3k_elf, app1_elf, app2_elf],
)
