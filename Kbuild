ccflags-y :=	-Wall					\
		-Wextra					\
		-Wno-missing-field-initializers		\
		-Wno-unused-parameter			\
		-Wformat				\
		-O2					\
		-std=gnu18				\
		-g					\
		-Werror=format-security			\
		-Werror=implicit-function-declaration

blkm-y := driver.o skiplist.o

obj-m := blkm.o
