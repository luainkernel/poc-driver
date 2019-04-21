# poc-driver - Linux kernel driver for lunatik

## Compiling

Run `make`.

## Usage

Loaded driver usage:

Example script helloworld.lua:
```
print("Hello, World!")
```

Terminal:
```
cat helloworld.lua > /dev/luadrv
```
check dmesg
