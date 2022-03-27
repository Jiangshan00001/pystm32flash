# pystm32flash
python bindings of stm32flash serial/i2c flash lib



# it should work on windows, linux and macos.

# how to install it:
```
pip install pystm32flash
```



# how to use it:

## get device information:

```
import pystm32flash as stm32f
stm32f.api.set_device(b'/dev/ttyS0')
stm32f.api.run_it()

```
or:
```
import pystm32flash as stm32f
stm32f.api.set_device(b'/dev/i2c-0')
stm32f.api.run_it()
```

## write and verify:
```
import pystm32flash as stm32f
#show help:
stm32f.api.show_help()
stm32f.api.set_arg(b'-w', b'test.hex')
stm32f.api.set_arg(b'-v', b'')
stm32f.api.set_device(b'/dev/ttyS0')
stm32f.api.run_it()
```


## write and verify and start execution:
```
import pystm32flash as stm32f
#show help:
stm32f.api.show_help()
stm32f.api.set_arg(b'-w', b'test.hex')
stm32f.api.set_arg(b'-v', b'')
stm32f.api.set_arg(b'-g', b'0x0')
stm32f.api.set_device(b'/dev/ttyS0')
stm32f.api.run_it()
```


## read flash to file:
```
import pystm32flash as stm32f
#show help:
stm32f.api.show_help()
stm32f.api.set_arg(b'-r', b'test.bin')
stm32f.api.set_device(b'/dev/ttyS0')
stm32f.api.run_it()
```


# any problem?

make an issue:
https://github.com/Jiangshan00001/pystm32flash/issues




c code is mostly from:
https://github.com/ARMinARM/stm32flash