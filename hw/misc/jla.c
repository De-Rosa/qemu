// based off of edu.c

#include "qemu/osdep.h"

#include "hw/misc/jla.h"
#include "hw/misc/trace.h"
#include "hw/pci/pci_device.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qom/object.h"
#include "trace-hw_misc.h"

#define TYPE_JLA "jla"
OBJECT_DECLARE_SIMPLE_TYPE(JlaState, JLA)

struct JlaState {
  PCIDevice pdev;
  MemoryRegion mmio;

  uint32_t status;
  uint32_t control;
  uint32_t scratch;

  uint32_t ring_size;
  uint64_t sq_base;
  uint32_t sq_head;
  uint32_t sq_tail;

  QEMUTimer job_timer;
  bool busy;
  struct jla_job curr;
  int64_t curr_start;
};

static void jla_reset_state(JlaState *s) {
  s->status = 0;
  s->control = 0;
  s->scratch = 0;

  s->ring_size = 0;
  s->sq_base = 0;
  s->sq_head = 0;
  s->sq_tail = 0;

  timer_del(&s->job_timer);
  s->busy = false;
  s->curr_start = 0;
}

static void jla_error(JlaState *s, const char *err) {
  s->status |= JLA_STATUS_ERROR;
  qemu_log_mask(LOG_GUEST_ERROR, "jla: %s\n", err);
}

// -- job starting --
static void jla_start_next_job(JlaState *s) {
  if (s->busy || s->sq_head == s->sq_tail ||
      !(s->control & JLA_CONTROL_ENABLE)) {
    return;
  }

  uint64_t len = sizeof(s->curr);
  dma_addr_t addr = s->sq_base + (uint64_t)s->sq_head + len;
  if (pci_dma_read(&s->pdev, addr, &s->curr, len) != MEMTX_OK) {
    jla_error(s, "DMA read for job failed");
    return;
  }

  // currently only supports job of id 0
  if (le16_to_cpu(s->curr.kernel_id) != 0) {
    jla_error(s, "attempted to run job != 0");
    return;
  }

  uint64_t duration = le64_to_cpu(s->curr.duration);
  if (duration > JLA_MAX_DURATION) {
    jla_error(s, "job length longer than max allowed");
    return;
  }

  s->busy = true;
  s->status |= JLA_STATUS_BUSY;
  s->curr_start = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

  trace_jla_job_start(s->sq_head, le32_to_cpu(s->curr.job_id), duration,
                      le64_to_cpu(s->curr.estimate_duration));

  timer_mod(&s->job_timer, s->curr_start + duration);
}

static void jla_job_done(void *opaque) {
  JlaState *s = opaque;

  int64_t end = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

  trace_jla_job_done(le32_to_cpu(s->curr.job_id), end - s->curr_start,
                     le64_to_cpu(s->curr.duration));

  s->sq_head = (s->sq_head + 1) & (s->ring_size - 1);
  s->busy = false;
  s->status &= ~JLA_STATUS_READY;

  jla_start_next_job(s);
}

// -- MMIO --

static uint64_t jla_mmio_read(void *opaque, hwaddr addr, unsigned size) {
  JlaState *s = opaque;
  uint64_t val = ~0ULL;

  switch (addr) {
  case JLA_REG_ID:
    val = JLA_ID_VALUE;
    break;
  case JLA_REG_VERSION:
    val = JLA_VERSION_VALUE;
    break;
  case JLA_REG_STATUS:
    val = s->status;
    break;
  case JLA_REG_CONTROL:
    val = s->control;
    break;
  case JLA_REG_SCRATCH:
    val = s->scratch;
    break;
  case JLA_REG_SQ_BASE_LO:
    val = extract64(s->sq_base, 0, 32);
    break;
  case JLA_REG_SQ_BASE_HI:
    val = extract64(s->sq_base, 32, 32);
    break;
  case JLA_REG_RING_SIZE:
    val = s->ring_size;
    break;
  case JLA_REG_SQ_TAIL:
    val = s->sq_tail;
    break;
  case JLA_REG_SQ_HEAD:
    val = s->sq_head;
    break;
  default:
    qemu_log_mask(LOG_GUEST_ERROR,
                  "jla: read from unknown register 0x%" HWADDR_PRIx "\n", addr);
    break;
  }
  return val;
}

static void jla_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                           unsigned size) {
  JlaState *s = opaque;

  switch (addr) {
  case JLA_REG_CONTROL:
    if (val & JLA_CONTROL_RESET) {
      jla_reset_state(s);
      break;
    }
    s->control = val & JLA_CONTROL_ENABLE;
    if (s->control & JLA_CONTROL_ENABLE) {
      s->status |= JLA_STATUS_READY;
    } else {
      s->status &= ~JLA_STATUS_READY;
    }

    break;
  case JLA_REG_SCRATCH:
    s->scratch = val;
    break;
  case JLA_REG_SQ_BASE_LO:
    s->sq_base = deposit64(s->sq_base, 0, 32, val);
    break;
  case JLA_REG_SQ_BASE_HI:
    s->sq_base = deposit64(s->sq_base, 32, 32, val);
    break;
  case JLA_REG_RING_SIZE:
    if (val == 0 || val > JLA_RING_MAX || (val & (val - 1))) {
      jla_error(s, "ring size must be a power of two and at most JLA_RING_MAX");
      break;
    }
    if (s->busy) {
      jla_error(s, "cannot change ring size while busy");
      break;
    }
    s->ring_size = val;
    s->sq_head = 0;
    s->sq_tail = 0;
    break;
  case JLA_REG_SQ_TAIL: // doorbell
    if (!(s->control & JLA_CONTROL_ENABLE)) {
      jla_error(s, "doorbell while disabled device");
      break;
    }
    if (s->ring_size == 0 || s->sq_base == 0) {
      jla_error(s, "doorbell before ring configured");
      break;
    }
    if (val >= s->ring_size) {
      jla_error(s, "tail out of range");
      break;
    }
    s->sq_tail = val;
    jla_start_next_job(s);
    break;
  case JLA_REG_ID:
  case JLA_REG_VERSION:
  case JLA_REG_STATUS:
    qemu_log_mask(LOG_GUEST_ERROR,
                  "jla: write to readonly register 0x%" HWADDR_PRIx "\n", addr);
    break;
  default:
    qemu_log_mask(LOG_GUEST_ERROR,
                  "jla: write to unknown register 0x%" HWADDR_PRIx "\n", addr);
    break;
  }
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

  timer_init_ns(&s->job_timer, QEMU_CLOCK_VIRTUAL, jla_job_done, s);

  jla_reset_state(s);
}

static void pci_jla_exit(PCIDevice *pdev) {
  JlaState *s = JLA(pdev);
  timer_del(&s->job_timer);
}

static void jla_class_init(ObjectClass *klass, const void *data) {
  DeviceClass *dc = DEVICE_CLASS(klass);
  PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

  k->realize = pci_jla_realize;
  k->exit = pci_jla_exit;
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
