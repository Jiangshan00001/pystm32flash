import pystm32flash
dir(pystm32flash)
pystm32flash.api.show_help(b'program')
pystm32flash.api.set_arg(b'-w', b'test.hex')
pystm32flash.api.set_arg(b'-v', b'')
pystm32flash.api.set_device(b'/dev/ttyS0')
pystm32flash.api.run_it(b"stm32flash")

