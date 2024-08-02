# blkm

Bio-based block device driver for log-structured storaging.

## Kernel version

Driver was made for Linux kernel version 6.8.

## Preinstallation

You need to have the source code of your Linux kernel on your machine to build this module.

To see your kernel version, run `uname -r`.

Kernel source code is usually located in `/usr/src/` or in `/usr/src/kernels/`. If you don't have your kernel there, you might need to install it from https://www.kernel.org/.

## Build

From repo's directory, run `make`. If you see an error, it can mean that you need to open Makefile and correct your kernel's location (or name).
It can also mean that your kernel version does not support this driver.

## Usage

To insert module into running kernel, run `insmod blkm.ko`.

To remove module from running kernel, run `rmmod blkm.ko`.

To set base block device, run `echo "/name-of-device" > /sys/module/blkm/parameters/blkm_base`

To open base block device and create virtual device, run `echo "1" > /sys/module/blkm/parameters/blkm_open`

To close base block device and destroy virtual device, run `echo "1" > /sys/module/blkm/parameters/blkm_close`

Try reading or writing to virtual device with `dd`.

## Disclaimer

It is highly recommended to run custom kernel modules not on your host machine, but on a guest (virtual) machine. Be aware that this module can **CORRUPT** or **DESTROY** your data if used carelessly.
Please use it with caution.

## License

Distributed under the GPL-2.0 License. Please see [LICENSE](https://github.com/spisladqo/blkm/blob/main/LICENSE) for more details.