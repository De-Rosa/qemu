#ifndef HW_MISC_JLA_H
#define HW_MISC_JLA_H

#define TYPE_JLA "jla"

// pci identity
#define JLA_VENDOR_ID 0x1234
#define JLA_DEVICE_ID 0x4a4c

// address space for registers
#define JLA_BAR0_SIZE 0x1000

// identification values
#define JLA_ID_VALUE 0x4A4C4101      // "JLA"
#define JLA_VERSION_VALUE 0x00010000 // major version 1, minor 0

// register offsets
#define JLA_REG_ID 0x00      // read magic number
#define JLA_REG_VERSION 0x04 // read interface version
#define JLA_REG_STATUS 0x08  // read state
#define JLA_REG_CONTROL 0x0C // read/write commands
// reserved for ring addresses
#define JLA_REG_SCRATCH 0x30 // read/write tester (no effect)

// status bits
#define JLA_STATUS_READY 0x01

// control bits
#define JLA_CONTROL_ENABLE 0x01
#define JLA_CONTROL_RESET 0x10

#endif // HW_MISC_JLA_H
