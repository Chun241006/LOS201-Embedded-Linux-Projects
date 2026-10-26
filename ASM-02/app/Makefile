# ASM2 - Sensor Monitoring System
# Build targets for ARM Cortex-A9 and Ubuntu x86_64

CC_ARM := arm-linux-gnueabihf-gcc
CC_HOST := gcc

CFLAGS := -std=c11 -Wall -Wextra -Werror -pthread -Iinclude

SOURCES := src/main.c \
           src/circular_buffer.c \
           src/sensor_thread.c \
           src/processor_thread.c \
           src/alert_thread.c \
           src/logger_thread.c

LIBS := -lrt -lm

ARM_TARGET := sms_app_arm
HOST_TARGET := sms_app_debug

.PHONY: all arm host clean

all: arm host

arm:
	$(CC_ARM) $(CFLAGS) -O2 -static \
	$(SOURCES) -o $(ARM_TARGET) $(LIBS)

host:
	$(CC_HOST) $(CFLAGS) -O0 -g \
	$(SOURCES) -o $(HOST_TARGET) $(LIBS)

clean:
	rm -f $(ARM_TARGET) $(HOST_TARGET)
