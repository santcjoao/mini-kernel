# ==============================================================================
# Makefile - Kernel RISC-V 32-bit (SO)
# ==============================================================================

CC        := clang
OBJCOPY   := llvm-objcopy
QEMU      := qemu-system-riscv32

CFLAGS    := -std=c11 -O2 -g3 -Wall -Wextra \
             --target=riscv32-unknown-elf -fuse-ld=lld \
             -fno-stack-protector -ffreestanding -nostdlib \
             -Ikernel -Iuser -Icommon

BUILD_DIR := build
KERNEL_DIR:= kernel
USER_DIR  := user
COMMON_DIR:= common

USER_APP_ELF := $(BUILD_DIR)/user/shell.elf
USER_APP_BIN := $(BUILD_DIR)/user/shell.bin
USER_APP_OBJ := $(BUILD_DIR)/user/shell.bin.o
KERNEL_ELF   := $(BUILD_DIR)/kernel.elf
DIST_DIR     := $(BUILD_DIR)/dist
DIST_STAGE   := $(DIST_DIR)/stage
DIST_TAR_GZ  := $(DIST_DIR)/kernel.tar.gz

# Template do script run.sh para o ambiente de submissão plano
define RUN_SH_TEMPLATE
#!/bin/bash
set -xue

QEMU=qemu-system-riscv32
OBJCOPY=llvm-objcopy

CC=clang
CFLAGS="-std=c11 -O2 -g3 -Wall -Wextra --target=riscv32-unknown-elf -fuse-ld=lld -fno-stack-protector -ffreestanding -nostdlib"

# Build the shell (application)
$$CC $$CFLAGS -Wl,-Tuser.ld -Wl,-Map=shell.map -o shell.elf shell.c user.c common.c
$$OBJCOPY --set-section-flags .bss=alloc,contents -O binary shell.elf shell.bin
$$OBJCOPY -Ibinary -Oelf32-littleriscv shell.bin shell.bin.o

# Build the kernel
$$CC $$CFLAGS -Wl,-Tkernel.ld -Wl,-Map=kernel.map -o kernel.elf kernel.c common.c shell.bin.o

(if [ ! -f disk.img ]; then echo "hello from disk image!" > disk.img; fi)

$$QEMU -machine virt -bios default -nographic -serial mon:stdio --no-reboot \
    -drive id=drive0,file=disk.img,format=raw,if=none \
    -device virtio-blk-device,drive=drive0,bus=virtio-mmio-bus.0 \
    -kernel kernel.elf
endef

export RUN_SH_TEMPLATE

.PHONY: all run clean dist help

all: $(KERNEL_ELF)

# Diretórios de build
$(BUILD_DIR)/kernel $(BUILD_DIR)/user $(DIST_DIR):
	@mkdir -p $@

# 1. Compilação da aplicação de usuário
$(USER_APP_ELF): $(USER_DIR)/shell.c $(USER_DIR)/user.c $(COMMON_DIR)/common.c $(USER_DIR)/user.ld | $(BUILD_DIR)/user
	$(CC) $(CFLAGS) -Wl,-T$(USER_DIR)/user.ld -Wl,-Map=$(BUILD_DIR)/user/shell.map -o $@ \
		$(USER_DIR)/shell.c $(USER_DIR)/user.c $(COMMON_DIR)/common.c

# 2. Conversão para binário cru
$(USER_APP_BIN): $(USER_APP_ELF)
	$(OBJCOPY) --set-section-flags .bss=alloc,contents -O binary $< $@

# 3. Encapsulamento em objeto linkável para embutimento no kernel
$(USER_APP_OBJ): $(USER_APP_BIN)
	(cd $(BUILD_DIR)/user && $(OBJCOPY) -Ibinary -Oelf32-littleriscv shell.bin shell.bin.o)

# 4. Compilação e linkagem do kernel
$(KERNEL_ELF): $(KERNEL_DIR)/kernel.c $(COMMON_DIR)/common.c $(KERNEL_DIR)/kernel.ld $(USER_APP_OBJ) | $(BUILD_DIR)/kernel
	$(CC) $(CFLAGS) -Wl,-T$(KERNEL_DIR)/kernel.ld -Wl,-Map=$(BUILD_DIR)/kernel/kernel.map -o $@ \
		$(KERNEL_DIR)/kernel.c $(COMMON_DIR)/common.c $(USER_APP_OBJ)

# Execução no QEMU
run: $(KERNEL_ELF)
	@if [ ! -f disk.img ]; then echo "hello from disk image!" > disk.img; fi
	$(QEMU) -machine virt -bios default -nographic -serial mon:stdio --no-reboot \
		-drive id=drive0,file=disk.img,format=raw,if=none \
		-device virtio-blk-device,drive=drive0,bus=virtio-mmio-bus.0 \
		-kernel $(KERNEL_ELF)

# Limpeza determinística
clean:
	rm -rf $(BUILD_DIR) *.elf *.bin *.o *.map *.tar *.tar.gz
	@rm -f docs/relatorios/*/*.aux docs/relatorios/*/*.log docs/relatorios/*/*.bbl docs/relatorios/*/*.blg docs/relatorios/*/*.out docs/relatorios/*/*.toc

# Pacote para submissão (.tar.gz com layout plano)
dist: $(KERNEL_ELF) | $(DIST_DIR)
	@rm -rf $(DIST_STAGE)
	@mkdir -p $(DIST_STAGE)
	@cp $(KERNEL_DIR)/kernel.* $(DIST_STAGE)/
	@cp $(USER_DIR)/user.* $(USER_DIR)/shell.c $(DIST_STAGE)/
	@cp $(COMMON_DIR)/common.* $(DIST_STAGE)/
	@cp $(KERNEL_ELF) $(DIST_STAGE)/
	@cp $(USER_APP_OBJ) $(USER_APP_BIN) $(USER_APP_ELF) $(DIST_STAGE)/
	@printf '%s\n' "$$RUN_SH_TEMPLATE" > $(DIST_STAGE)/run.sh
	@chmod +x $(DIST_STAGE)/run.sh
	@tar -czf $(DIST_TAR_GZ) -C $(DIST_STAGE) \
		common.c common.h kernel.c kernel.h kernel.ld \
		run.sh shell.c user.c user.h user.ld kernel.elf \
		shell.bin.o shell.bin shell.elf
	@cp $(DIST_TAR_GZ) ./kernel.tar.gz
	@rm -rf $(DIST_STAGE)
	@echo "==> Pacote de submissão gerado: $(DIST_TAR_GZ) e ./kernel.tar.gz"

help:
	@echo "Alvos disponíveis:"
	@echo "  make              - Compila kernel e aplicação (gera build/kernel.elf)"
	@echo "  make run          - Compila e executa no QEMU"
	@echo "  make clean        - Remove diretório build/ e arquivos temporários"
	@echo "  make dist         - Gera exclusivamente o pacote .tar.gz de submissão"
