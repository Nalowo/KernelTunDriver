# The module name lives in TWO places: MODULE_NAME below, and obj-m/ktun-y in
# src/Kbuild.
MODULE_NAME := ktun

KDIR     ?= /lib/modules/$(shell uname -r)/build
SRC      := $(abspath src)
BUILD    := $(abspath build)
DEVTOOLS := $(abspath devtools)

.PHONY: all tools clean load unload format compdb \
        qemu-setup qemu-setup-debug qemu-build qemu-boot qemu-debug \
        gdb-attach qemu-test help

# --- Build the module. KDIR defaults to the host kernel; build.sh overrides it
# with the QEMU kernel. MO= sends every generated file straight to build/, so
# src/ stays clean -- no manual sweeping. MO= needs kbuild >= 6.13, which the
# QEMU kernel (6.18) provides. ---
all:
	@mkdir -p $(BUILD)
	$(MAKE) -C $(KDIR) M=$(SRC) MO=$(BUILD) modules
	@test -f $(BUILD)/$(MODULE_NAME).ko \
		|| { echo "ERROR: $(MODULE_NAME).ko not in build/ -- MO= needs kbuild >= 6.13"; exit 1; }
	@echo "  -> $(BUILD)/$(MODULE_NAME).ko"

# --- Userspace utility. Static: the BusyBox guest has no libc (R-U.1). ---
tools: $(BUILD)/ktunctl

$(BUILD)/ktunctl: tools/ktunctl.c src/ktun_ioctl.h
	@mkdir -p $(BUILD)
	gcc -static -O2 -Wall -Wextra -Isrc -o $@ tools/ktunctl.c

clean:
	-$(MAKE) -C $(KDIR) M=$(SRC) MO=$(BUILD) clean 2>/dev/null || true
	rm -rf $(BUILD)

# insmod/rmmod into the *host* kernel. On WSL2 use the qemu-* targets instead.
load: all
	sudo insmod $(BUILD)/$(MODULE_NAME).ko
unload:
	sudo rmmod $(MODULE_NAME)

format:
	clang-format -i src/*.c $(wildcard src/*.h) tools/*.c

# compile_commands.json for clangd / VS Code (needs `bear`). Built against the
# QEMU kernel via build.sh -- under WSL2 there is no host kernel tree to use.
# Output into .vscode/ so the root stays clean; settings.json points cpptools
# there. build/ is wiped first so kbuild actually recompiles and bear can
# capture the compiler invocations (an up-to-date build would yield an empty DB).
compdb:
	@mkdir -p .vscode
	@rm -rf $(BUILD)
	bear --output .vscode/compile_commands.json -- $(DEVTOOLS)/build.sh

# --- QEMU workflow (recommended, and required under WSL2) ---
qemu-setup:                 ## one-time: build minimal kernel + initramfs
	$(DEVTOOLS)/setup.sh
qemu-setup-debug:           ## one-time: build kernel WITH sanitizers (KASAN/lockdep/...)
	KERNEL_DEBUG=1 $(DEVTOOLS)/setup.sh
qemu-build:                 ## build this module against the QEMU kernel
	$(DEVTOOLS)/build.sh
qemu-boot: qemu-build       ## boot QEMU, interactive shell, module at /mnt/host/build
	$(DEVTOOLS)/boot.sh
qemu-debug: qemu-build      ## boot QEMU paused for GDB (terminal 1)
	$(DEVTOOLS)/boot.sh --gdb
gdb-attach:                 ## attach GDB to the paused guest (terminal 2)
	$(DEVTOOLS)/gdb.sh
qemu-test: qemu-build       ## automated insmod/rmmod of the built module
	$(DEVTOOLS)/test.sh

help:
	@grep -E '^[a-z-]+:.*?## .*$$' $(MAKEFILE_LIST) | \
		awk 'BEGIN{FS=":.*?## "}{printf "  \033[36m%-16s\033[0m %s\n", $$1, $$2}'
