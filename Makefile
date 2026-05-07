KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)
BUILD_DIR := $(PWD)/build

all: $(BUILD_DIR) module server

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

module: $(BUILD_DIR)
	mkdir -p $(BUILD_DIR)/kernel
	cp $(PWD)/kernel/tg_protocol.h $(BUILD_DIR)/kernel/
	sleep 0.1
	cp $(PWD)/kernel/telegram_fs.c $(BUILD_DIR)/kernel/
	cp $(PWD)/kernel/Makefile $(BUILD_DIR)/kernel/
	$(MAKE) -C $(KDIR) M=$(BUILD_DIR)/kernel modules
	cp $(BUILD_DIR)/kernel/telegram_fs.ko $(BUILD_DIR)/

server: $(BUILD_DIR)
	g++ -std=c++20 user/telegram_server.cpp -o $(BUILD_DIR)/telegram_server

clean:
	rm -rf $(BUILD_DIR)
