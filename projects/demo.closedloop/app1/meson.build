subdir('platform')

app1_elf = executable(
	'app1.elf',
	sources: files(
		'head.S',
		'main.c',
		'init2.c',
		'sha256.c',
		'golden.c'
	) + app1_platform_uart,
	c_args: [
		'-specs=picolibc.specs',
		'-DUART_REG_STRIDE=' + get_option('uart_stride'),
		(get_option('platform') == 'ch32v307' ? ['-DPLATFORM_CH32V307', '-Os', '-DPICOLIBC_INTEGER_PRINTF', '-DPICOLIBC_TINY_PRINTF'] : []),
		'-DMAX_MEMORY_FUEL=' + get_option('nmemoryfuel').to_string(),
	] + (get_option('demonopmp') ? ['-DS3K_DEMO_NO_PMP'] : []) +
	  (get_option('demodynamic') ? ['-DS3K_DEMO_DYNAMIC'] : []) +
	  (get_option('demobadboot') ? ['-DS3K_DEMO_BAD_BOOT'] : []) +
	  (get_option('demoio') ? ['-DS3K_DEMO_IO'] : []) +
	  (get_option('democodero') ? ['-DS3K_DEMO_CODE_RO'] : []) +
	  (get_option('demorepeat') ? ['-DS3K_DEMO_REPEAT'] : []) +
	  (get_option('demoioguard') ? ['-DS3K_DEMO_IO_GUARD'] : []) +
  (get_option('demopressure') ? ['-DS3K_DEMO_REPEAT', '-DREPEAT_ATTACKS=100'] : []) +
  (get_option('demoload') ? ['-DS3K_DEMO_LOAD'] : []) +
  (get_option('demonofaultlog') ? ['-DS3K_DEMO_NO_FAULT_LOG'] : []) +
  (get_option('demopmpcheck') ? ['-DS3K_DEMO_PMP_CHECK'] : []),
	link_args: [
		'-nostartfiles',
		'-specs=picolibc.specs',
		'-T', app1_platform_ld,
	],
	# track the linker script as a link dependency (see app2/meson.build).
	link_depends: app1_platform_ld,
	dependencies: [
		libs3k_dep,
	],
)
