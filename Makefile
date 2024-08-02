KVER ?= $(shell uname -r)
OS := $(shell lsb_release -si)

LK_SRC_DIR_FED := /usr/src/kernels/$(KVER)
LK_SRC_DIR_UB := /usr/src/linux-headers-$(KVER)

ifeq ($(OS),Fedora)
	LK_SRC_DIR := $(LK_SRC_DIR_FED)
else
	LK_SRC_DIR := $(LK_SRC_DIR_UB)
endif

all: build

build:
	$(MAKE) -j -C $(LK_SRC_DIR) M=$(PWD) modules

clean:
	$(MAKE) -j -C $(LK_SRC_DIR) M=$(PWD) clean
