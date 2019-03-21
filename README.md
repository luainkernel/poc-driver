# poc-driver - Linux kernel driver for lunatik

## Compiling and running
To build module you need to:
1. Clone sources
git clone --recursive https://github.com/luainkernel/poc-driver
2. Assume that you have kernel tree sources in /usr/src/linux
3. Then compile:
```
make
```
4. Run (as root):
```
insmod ./dependencies/lunatik/lunatik.ko
insmod poc-driver.ko
```

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
