// based off of edu.c

#include "qemu/osdep.h"

#include "hw/misc/jla.h"
#include "hw/pci/pci_device.h"
#include "qemu/module.h"
#include "qom/object.h"

OBJECT_DECLARE_SIMPLE_TYPE(JlaState, JLA)

struct JlaState {
  PCIDevice pdev;
  MemoryRegion mmio;

  uint32_t control;
  uint32_t status;
  uint32_t scratch;
};

// -- MMIO --

static uint64_t jla_mmio_read(void *opaque, hwaddr addr, unsigned size) {
  return ~0ULL;
}

static void jla_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                           unsigned size) {
  // empty
}

static const MemoryRegionOps jla_mmio_ops = {
    .read = jla_mmio_read,
    .write = jla_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid =
        {
            .min_access_size = 4,
            .max_access_size = 4,
        },
    .impl =
        {
            .min_access_size = 4,
            .max_access_size = 4,
        },

};

static void pci_jla_realize(PCIDevice *pdev, Error **errp) {
  JlaState *s = JLA(pdev);

  memory_region_init_io(&s->mmio, OBJECT(s), &jla_mmio_ops, s, "jla-mmio",
                        JLA_BAR0_SIZE);
  pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mmio);
}

static void jla_class_init(ObjectClass *klass, const void *data) {
  DeviceClass *dc = DEVICE_CLASS(klass);
  PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

  k->realize = pci_jla_realize;
  k->vendor_id = JLA_VENDOR_ID;
  k->device_id = JLA_DEVICE_ID;
  k->class_id = PCI_CLASS_OTHERS;
  dc->desc = "JLA job accelerator";
  set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo jla_types[] = {
    {
        .name = TYPE_JLA,
        .parent = TYPE_PCI_DEVICE,
        .instance_size = sizeof(JlaState),
        .class_init = jla_class_init,
        .interfaces =
            (const InterfaceInfo[]){
                {INTERFACE_CONVENTIONAL_PCI_DEVICE},
                {},
            },
    },
};

DEFINE_TYPES(jla_types)
