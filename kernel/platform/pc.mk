ifeq ($(ARCH),x86_64)
PLATFORM_DRIVER_SRCS += ../src/kernel/drivers/vga.c \
                        ../src/kernel/drivers/serial.c \
                        ../src/kernel/drivers/driver.c \
                        ../src/kernel/drivers/rtc.c \
                        ../src/kernel/drivers/usb/common.c \
                        ../src/kernel/drivers/usb/core.c \
                        ../src/kernel/drivers/usb/uhci.c \
                        ../src/kernel/drivers/usb/ohci.c \
                        ../src/kernel/drivers/usb/ehci.c \
                        ../src/kernel/drivers/usb/xhci.c \
                        ../src/kernel/drivers/keyboard.c \
                        ../src/kernel/drivers/mouse.c \
                        ../src/kernel/drivers/ata.c \
                        ../src/kernel/drivers/rtl8168.c \
                        ../src/kernel/drivers/gpu.c \
                        ../src/kernel/drivers/framebuffer.c \
                        ../src/kernel/drivers/psf.c \
                        ../src/kernel/firmware/acpi.c \
                        ../src/kernel/arch/x86_64/irq/apic.c \
                        ../src/kernel/arch/x86_64/irq/pic8259.c \
                        ../src/kernel/arch/x86_64/timer/pit8253.c \
                        ../src/kernel/arch/x86_64/bus/pci_cfg_io.c
else
$(error PLATFORM=pc currently supports only ARCH=x86_64)
endif
