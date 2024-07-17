KVER ?= $(shell uname -r)
LK_SRC_DIR := /usr/src/kernels/$(KVER)

all: build

build:
	$(MAKE) -j -C $(LK_SRC_DIR) M=$(PWD) modules

clean:
	$(MAKE) -j -C $(LK_SRC_DIR) M=$(PWD) clean
