KVER ?= $(shell uname -r)
LK_BUILD_DIR ?= /lib/modules/$(KVER)/build
LK_SRC_DIR_RHEL := /usr/src/kernels/$(KVER)

all: build

build:
	$(MAKE) -j -C $(LK_SRC_DIR_RHEL) M=$(PWD) modules

clean:
	$(MAKE) -j -C $(LK_SRC_DIR_RHEL) M=$(PWD) clean
