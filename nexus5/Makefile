# -----------------------------------------------------------------------------
# This Makefile builds the fpc_irq driver
# It can be used both for build the driver inside the kernel tree but from
# outside.
#
# To build the driver inside kernel.
# Copy the C files + this Makefile to the kernel tree
# for example to drivers/input/misc/fpc
# Then add the line: "obj-y  +=fpc/"
# to the file "drivers/input/misc/Makefile"
# Note the / at the end that marks to add a directory
#
# When build the driver outside the kernel
# export these variables for cross compiling:
# export ARCH=arm64
# export CROSS_COMPILE=arm-linux-eabi-
# export KERNELDIR=[path to the built kernel]
# export srctree=[path to the MT6795 kernel source code]
# The built kernel is often placed at "out/target/<pf>/obj/KERNEL_OBJ"
# -----------------------------------------------------------------------------
PWD := $(shell pwd)

# -----------------------------------------------------------------------------
# Any of these two defines can either be set in the defconfig file or set here.
#
# Set this to have a kernel built-in driver
# The result will be that the driver is included in the kernel
# CONFIG_INPUT_FPC1020_IRQ = y
#
# Set to build the driver as a module, Must be set if built outside kernel
# The result will be a fpc_irq.ko kernel module and that have to be loaded
# in some way. This is good to have if devloping the driver.
# CONFIG_INPUT_FPC1020_IRQ = m

# Source files
fpc_src = \
	fpc_irq.c \
	nexus.c

fpc1020_irq-y := $(fpc_src:.c=.o)
obj-$(CONFIG_INPUT_FPC1020_IRQ) := fpc1020_irq.o

# -----------------------------------------------------------------------------
# Rule used when building the driver outside the kernel
# The result will be a fpc_irq.ko
# -----------------------------------------------------------------------------
fpc_irq: $(fpc_src)
	$(MAKE) -C $(KERNELDIR) M=$(PWD) CFLAGS_MODULE=-fno-pic modules
