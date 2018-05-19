# poc-driver - Linux kernel driver for lunatik

## Compiling
To build module you need to:
1. Clone sources for
Driver
https://github.com/luainkernel/poc-driver
Lua (kernel port)
https://github.com/luainkernel/lunatik
2. Assume that you have kernel tree sources in /usr/src/linux
3. create symlinks to lunatik and poc-driver in /usr/src/linux/drivers with corresponding names
```
ln -s /where_you_put_lunatik_src /usr/src/linux/drivers/lunatik
ln -s /where_you_put_poc-driver_src /usr/src/linux/drivers/poc-driver
```
4. edit drivers/Kconfig to add following:
```
source drivers/lunatik/Kconfig
```
5. lunatik/Kconfig contents:
```
config LUNATIK
    tristate "Lunatik"
    
config LUNATIK_POC
    bool "Use poc driver"
    depends on LUNATIK
    default y
```
6. edit drivers/Makefile to add:
```
obj-$(CONFIG_LUNATIK) += lunatik/
```
7. lunatik/Makefile example code:
```
EXTRA_CFLAGS += -D_KERNEL
# for poc-driver:
EXTRA_CFLAGS += -I$(src)

obj-$(CONFIG_LUNATIK) += lunatik.o

lunatik-objs := lua/lapi.o lua/lcode.o lua/lctype.o lua/ldebug.o lua/ldo.o \
         lua/ldump.o lua/lfunc.o lua/lgc.o lua/llex.o lua/lmem.o \
	 lua/lobject.o lua/lopcodes.o lua/lparser.o lua/lstate.o \
         lua/lstring.o lua/ltable.o lua/ltm.o \
	 lua/lundump.o lua/lvm.o lua/lzio.o lua/lauxlib.o lua/lbaselib.o \
         lua/lbitlib.o lua/lcorolib.o lua/ldblib.o lua/lstrlib.o \
	 lua/ltablib.o lua/lutf8lib.o lua/loslib.o lua/lmathlib.o lua/linit.o

lunatik-objs += arch/$(ARCH)/setjmp.o

lunatik-${CONFIG_LUNATIK_POC} += ../poc-driver/luadev.o
```
8. Then:
```
cd /usr/src/linux
#compile
make modules -j4 ARCH=x86_64
#load
modprobe -v lunatik
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
then check dmesg for message



