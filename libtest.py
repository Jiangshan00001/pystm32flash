import pystm32flash as stm32f
stm32f.api.show_help()
stm32f.api.set_arg(b'-w', b'test.hex')
stm32f.api.set_arg(b'-v', b'')
stm32f.api.set_device(b'/dev/ttyS0')
stm32f.api.run_it()

