
# 要编译的所有C文件
SRC = $(wildcard *.c)
PKG_CONFIG = pkg-config
CFLAGS = -fPIC -shared -I/nfs/home/yanyue/tools/qemu/include/qemu 
CFLAGS += $(shell $(PKG_CONFIG) --cflags glib-2.0)


# 编译生成.so文件，前缀加上lib
lib%.so: %.c
	gcc $(CFLAGS) -o $@ $<

# all = 所有 lib*.so
all: $(SRC:%.c=lib%.so)

clean:
	rm -f *.so

run:
	qemu-riscv64  -plugin ./libinsn.so,match=ecall -d plugin ~/tests/riscv_tests/hello