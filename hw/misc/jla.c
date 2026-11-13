/*
 * JLA: barebones PCI device. Shows up on the bus with its own IDs,
 * no registers or behaviour yet.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/pci/pci_device.h"
#include "qom/object.h"
#include "hw/misc/jla.h"

OBJECT_DECLARE_SIMPLE_TYPE(JlaState, JLA)

struct JlaState {
    PCIDevice pdev;
};

static void jla_realize(PCIDevice *pdev, Error **errp)
{
}

static void jla_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = jla_realize;
    k->vendor_id = JLA_VENDOR_ID;
    k->device_id = JLA_DEVICE_ID;
    k->class_id = PCI_CLASS_OTHERS;
    dc->desc = "JLA job accelerator";
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo jla_types[] = {
    {
        .name          = TYPE_JLA,
        .parent        = TYPE_PCI_DEVICE,
        .instance_size = sizeof(JlaState),
        .class_init    = jla_class_init,
        .interfaces = (const InterfaceInfo[]) {
            { INTERFACE_CONVENTIONAL_PCI_DEVICE },
            { },
        },
    },
};

DEFINE_TYPES(jla_types)
