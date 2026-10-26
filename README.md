# LOS201 - Embedded Linux Projects

A collection of Embedded Linux labs and assignments developed for the LOS201 course at FPT University.

## Overview

This repository contains five projects covering Embedded Linux system development, kernel modules, device drivers, debugging, multithreading, and real-time programming.

## Development Environment

- Host OS: Ubuntu Linux
- Target: ARM Cortex-A9 (ARMv7-A)
- Emulator: QEMU (vexpress-a9)
- Linux Kernel: 5.15
- Languages: C and Shell Script
- Tools: GCC, ARM Cross-Compiler, GDB, Valgrind, strace, perf, Git

## Projects

### LAB-01: Embedded Linux System Setup

Build and configure a minimal Embedded Linux environment.

Topics:
- Linux Kernel configuration
- BusyBox configuration
- U-Boot bootloader
- Root filesystem and initramfs
- ARM emulation with QEMU

### LAB-02: Linux Character Device Driver

Develop and test a Linux character device driver.

Topics:
- Kernel module development
- Character device operations
- Device read/write interfaces
- Driver testing

### LAB-03: Debugging and Performance Analysis

Debug and analyze Linux applications.

Topics:
- GDB remote debugging
- Valgrind memory analysis
- System call tracing with strace
- CPU profiling with perf
- Real-time latency measurements

### ASM-01: Linux Device Driver Development

Implement and test Linux device drivers.

Topics:
- IOCTL character device driver
- Misc device driver
- Userspace test applications
- Kernel module testing

### ASM-02: Sensor Monitoring System

Develop a multithreaded sensor monitoring application.

Topics:
- Sensor character device driver
- Four POSIX worker threads
- Circular buffer and synchronization
- POSIX message queues
- Temperature and humidity alerts
- SCHED_FIFO real-time scheduling
- Jitter measurement
- Debugging and performance profiling

## Repository Structure

- LAB-01/: Embedded Linux system setup
- LAB-02/: Character device driver
- LAB-03/: Debugging and profiling
- ASM-01/: Device driver development
- ASM-02/: Sensor monitoring system
- README.md: Repository overview

## Notes

This repository contains source code, configurations, scripts, and selected testing results.

Building and running the projects may require an ARM cross-compilation toolchain, QEMU, and a compatible Linux kernel environment.

## Author

FPT University - Semiconductor Engineering
