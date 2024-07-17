# blkm

Bio-based block device driver module for Linux kernel.

## Kernel version

Driver was made for Linux kernel version 6.8.5.

## Preinstallation

You need to have the source code of your Linux kernel on your machine to build this module.

To see your kernel version, run `uname -r`.

Kernel source code is usually located in `/usr/src/` or in `/usr/src/kernels/`. If you don't have your kernel there, you might need to install it from https://www.kernel.org/.

## Build

From repo's directory, run `make`. If you see an error, it can mean that you need to open Makefile and correct your kernel's location (or name).

To insert module into running kernel, run `insmod blkm.ko`.

To remove module from running kernel, run `rmmod blkm.ko`.

## Disclaimer

It is highly recommended to run custom kernel modules not on your host machine, but on a guest (virtual) machine. Be aware that this module can **CORRUPT** or **DESTROY** your data if used carelessly.
Please use it with caution.

## License

Distributed under the GPL-2.0 License. Please see [LICENSE](https://github.com/spisladqo/blkm/blob/main/LICENSE) for more details.