obj-m+=g_ops_gadget.o

all:
	make -C $(SOURCE_DIR) M=$(PWD) modules EXTRA_CFLAGS="-g -DDEBUG"
clean:
	make -C $(SOURCE_DIR) M=$(PWD) clean

