subdir('platform')
app2_elf = executable(
	'app2.elf',
	sources: files(
		'head.S',
		'main.c',
		'quad_core.c',
	) + app2_platform_uart,
	c_args: [
		'-specs=picolibc.specs',
		'-DUART_REG_STRIDE=' + get_option('uart_stride'),
		(get_option('platform') == 'ch32v307' ? ['-DPLATFORM_CH32V307', '-Os', '-DPICOLIBC_INTEGER_PRINTF', '-DPICOLIBC_TINY_PRINTF'] : []),
		'-DMAX_MEMORY_FUEL=' + get_option('nmemoryfuel').to_string(),
	] + (get_option('democodero') ? ['-DS3K_DEMO_CODE_RO'] : []),
	link_args: [
		'-nostartfiles',
		'-specs=picolibc.specs',
		'-T', app2_platform_ld,
	],
	# track the linker script as a link dependency: -T paths in link_args
	# alone are invisible to ninja, so a changed .ld would silently skip
	# relinking.
	link_depends: app2_platform_ld,
	dependencies: [
		libs3k_dep,
	],
)
