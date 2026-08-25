
if (get_option('platform') == 'qemu_virt')
  app1_platform_uart = files('ns16550a.c')
  app1_platform_ld = meson.current_source_dir() / 'qemu_virt.ld'
elif (get_option('platform') == 'cheshire') or (get_option('platform') == 'cheshire2')
  app1_platform_uart = files('ti16750.c')
  app1_platform_ld = meson.current_source_dir() / 'cheshire.ld'
elif (get_option('platform') == 'ch32v307')
  app1_platform_uart = files('ch32v307_uart.c')
  app1_platform_ld = meson.current_source_dir() / 'ch32v307.ld'
else
  error('Unknown platform: ' + get_option('platform'))
endif
