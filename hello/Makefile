obj-m+=hello.o

all:
	make -C $(SOURCE_DIR) M=$(PWD) modules
clean:
	make -C $(SOURCE_DIR) M=$(PWD) clean
