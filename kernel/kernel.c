#include "kernel.h"

extern char __bss[], __bss_end[], __stack_top[];
extern char __free_ram[], __free_ram_end[];
extern char __kernel_base[];

struct sbiret sbi_call(long arg0, long arg1, long arg2, long arg3, long arg4,
                       long arg5, long fid, long eid) {
  register long a0 __asm__("a0") = arg0;
  register long a1 __asm__("a1") = arg1;
  register long a2 __asm__("a2") = arg2;
  register long a3 __asm__("a3") = arg3;
  register long a4 __asm__("a4") = arg4;
  register long a5 __asm__("a5") = arg5;
  register long a6 __asm__("a6") = fid;
  register long a7 __asm__("a7") = eid;

  __asm__ __volatile__("ecall"
                       : "=r"(a0), "=r"(a1)
                       : "r"(a0), "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"(a5),
                         "r"(a6), "r"(a7)
                       : "memory");
  return (struct sbiret){.error = a0, .value = a1};
}

__attribute__((weak)) void putchar(char ch) { sbi_call(ch, 0, 0, 0, 0, 0, 0, 1); }

long sbi_getchar(void) {
  struct sbiret ret = sbi_call(0, 0, 0, 0, 0, 0, 0, 2);
  return ret.error;
}

__attribute__((weak)) long getchar(void) { return sbi_getchar(); }

paddr_t alloc_pages(uint32_t n) {
  static paddr_t next_paddr = 0;
  if (!next_paddr)
    next_paddr = (paddr_t)__free_ram;

  paddr_t paddr = next_paddr;
  next_paddr += n * PAGE_SIZE;

  if (next_paddr > (paddr_t)__free_ram_end)
    PANIC("out of memory");

  memset((void *)paddr, 0, n * PAGE_SIZE);
  return paddr;
}

void map_page(uint32_t *table1, uint32_t vaddr, paddr_t paddr, uint32_t flags) {
  if (!is_aligned(vaddr, PAGE_SIZE))
    PANIC("unaligned vaddr %x", vaddr);

  if (!is_aligned(paddr, PAGE_SIZE))
    PANIC("unaligned paddr %x", paddr);

  uint32_t vpn1 = (vaddr >> 22) & 0x3ff;
  if ((table1[vpn1] & PAGE_V) == 0) {
    // Create the 1st level page table if it doesn't exist.
    uint32_t pt_paddr = alloc_pages(1);
    table1[vpn1] = ((pt_paddr / PAGE_SIZE) << 10) | PAGE_V;
  }

  // Set the 2nd level page table entry to map the physical page.
  uint32_t vpn0 = (vaddr >> 12) & 0x3ff;
  uint32_t *table0 = (uint32_t *)((table1[vpn1] >> 10) * PAGE_SIZE);
  table0[vpn0] = ((paddr / PAGE_SIZE) << 10) | flags | PAGE_V;
}

void handle_trap(struct trap_frame *f) {
  uint32_t scause = READ_CSR(scause);
  uint32_t stval = READ_CSR(stval);
  uint32_t user_pc = READ_CSR(sepc);

  if (scause == SCAUSE_ECALL) {
    handle_syscall(f);
    user_pc += 4;
    WRITE_CSR(sepc, user_pc);
    return;
  }

  const char *desc = "unknown trap";
  switch (scause) {
    case 0:  desc = "instruction address misaligned"; break;
    case 1:  desc = "instruction access fault"; break;
    case 2:  desc = "illegal instruction"; break;
    case 3:  desc = "breakpoint"; break;
    case 4:  desc = "load address misaligned"; break;
    case 5:  desc = "load access fault"; break;
    case 6:  desc = "store/amo address misaligned"; break;
    case 7:  desc = "store/amo access fault"; break;
    case 12: desc = "instruction page fault"; break;
    case 13: desc = "load page fault"; break;
    case 15: desc = "store/amo page fault"; break;
  }

  printf("Trap: %s (scause=%x, stval=%x, sepc=%x)\n", desc, scause, stval,
         user_pc);
  PANIC("unexpected trap scause=%x, stval=%x, sepc=%x", scause, stval,
        user_pc);
}

__attribute__((naked)) __attribute__((aligned(4))) void kernel_entry(void) {
  __asm__ __volatile__("csrrw sp, sscratch, sp\n"
                       "addi sp, sp, -4 * 31\n"
                       "sw ra,  4 * 0(sp)\n"
                       "sw gp,  4 * 1(sp)\n"
                       "sw tp,  4 * 2(sp)\n"
                       "sw t0,  4 * 3(sp)\n"
                       "sw t1,  4 * 4(sp)\n"
                       "sw t2,  4 * 5(sp)\n"
                       "sw t3,  4 * 6(sp)\n"
                       "sw t4,  4 * 7(sp)\n"
                       "sw t5,  4 * 8(sp)\n"
                       "sw t6,  4 * 9(sp)\n"
                       "sw a0,  4 * 10(sp)\n"
                       "sw a1,  4 * 11(sp)\n"
                       "sw a2,  4 * 12(sp)\n"
                       "sw a3,  4 * 13(sp)\n"
                       "sw a4,  4 * 14(sp)\n"
                       "sw a5,  4 * 15(sp)\n"
                       "sw a6,  4 * 16(sp)\n"
                       "sw a7,  4 * 17(sp)\n"
                       "sw s0,  4 * 18(sp)\n"
                       "sw s1,  4 * 19(sp)\n"
                       "sw s2,  4 * 20(sp)\n"
                       "sw s3,  4 * 21(sp)\n"
                       "sw s4,  4 * 22(sp)\n"
                       "sw s5,  4 * 23(sp)\n"
                       "sw s6,  4 * 24(sp)\n"
                       "sw s7,  4 * 25(sp)\n"
                       "sw s8,  4 * 26(sp)\n"
                       "sw s9,  4 * 27(sp)\n"
                       "sw s10, 4 * 28(sp)\n"
                       "sw s11, 4 * 29(sp)\n"
                       "csrr a0, sscratch\n"
                       "sw a0,  4 * 30(sp)\n"
                       "addi a0, sp, 4 * 31\n"
                       "csrw sscratch, a0\n"
                       "mv a0, sp\n"
                       "call handle_trap\n"
                       "lw ra,  4 * 0(sp)\n"
                       "lw gp,  4 * 1(sp)\n"
                       "lw tp,  4 * 2(sp)\n"
                       "lw t0,  4 * 3(sp)\n"
                       "lw t1,  4 * 4(sp)\n"
                       "lw t2,  4 * 5(sp)\n"
                       "lw t3,  4 * 6(sp)\n"
                       "lw t4,  4 * 7(sp)\n"
                       "lw t5,  4 * 8(sp)\n"
                       "lw t6,  4 * 9(sp)\n"
                       "lw a0,  4 * 10(sp)\n"
                       "lw a1,  4 * 11(sp)\n"
                       "lw a2,  4 * 12(sp)\n"
                       "lw a3,  4 * 13(sp)\n"
                       "lw a4,  4 * 14(sp)\n"
                       "lw a5,  4 * 15(sp)\n"
                       "lw a6,  4 * 16(sp)\n"
                       "lw a7,  4 * 17(sp)\n"
                       "lw s0,  4 * 18(sp)\n"
                       "lw s1,  4 * 19(sp)\n"
                       "lw s2,  4 * 20(sp)\n"
                       "lw s3,  4 * 21(sp)\n"
                       "lw s4,  4 * 22(sp)\n"
                       "lw s5,  4 * 23(sp)\n"
                       "lw s6,  4 * 24(sp)\n"
                       "lw s7,  4 * 25(sp)\n"
                       "lw s8,  4 * 26(sp)\n"
                       "lw s9,  4 * 27(sp)\n"
                       "lw s10, 4 * 28(sp)\n"
                       "lw s11, 4 * 29(sp)\n"
                       "lw sp,  4 * 30(sp)\n"
                       "sret\n");
}

__attribute__((naked)) void switch_context(uint32_t *prev_sp,
                                           uint32_t *next_sp) {
  __asm__ __volatile__("addi sp, sp, -13 * 4\n"
                       "sw ra,  0  * 4(sp)\n"
                       "sw s0,  1  * 4(sp)\n"
                       "sw s1,  2  * 4(sp)\n"
                       "sw s2,  3  * 4(sp)\n"
                       "sw s3,  4  * 4(sp)\n"
                       "sw s4,  5  * 4(sp)\n"
                       "sw s5,  6  * 4(sp)\n"
                       "sw s6,  7  * 4(sp)\n"
                       "sw s7,  8  * 4(sp)\n"
                       "sw s8,  9  * 4(sp)\n"
                       "sw s9,  10 * 4(sp)\n"
                       "sw s10, 11 * 4(sp)\n"
                       "sw s11, 12 * 4(sp)\n"
                       "sw sp, (a0)\n"
                       "lw sp, (a1)\n"
                       "lw ra,  0  * 4(sp)\n"
                       "lw s0,  1  * 4(sp)\n"
                       "lw s1,  2  * 4(sp)\n"
                       "lw s2,  3  * 4(sp)\n"
                       "lw s3,  4  * 4(sp)\n"
                       "lw s4,  5  * 4(sp)\n"
                       "lw s5,  6  * 4(sp)\n"
                       "lw s6,  7  * 4(sp)\n"
                       "lw s7,  8  * 4(sp)\n"
                       "lw s8,  9  * 4(sp)\n"
                       "lw s9,  10 * 4(sp)\n"
                       "lw s10, 11 * 4(sp)\n"
                       "lw s11, 12 * 4(sp)\n"
                       "addi sp, sp, 13 * 4\n"
                       "ret\n");
}

__attribute__((naked)) void user_entry(void) {
  __asm__ __volatile__(
      "csrw sepc, %[sepc]\n"
      "csrw sstatus, %[sstatus]\n"
      "sret\n"
      :
      : [sepc] "r"(USER_BASE),
        [sstatus] "r"(SSTATUS_SPIE));
}

struct process procs[PROCS_MAX];

struct process *create_process(const void *image, size_t image_size) {
  struct process *proc = NULL;
  int i;
  for (i = 0; i < PROCS_MAX; i++) {
    if (procs[i].state == PROC_UNUSED) {
      proc = &procs[i];
      break;
    }
  }

  if (!proc)
    PANIC("no free process slots");

  uint32_t *sp = (uint32_t *)&proc->stack[sizeof(proc->stack)];
  *--sp = 0;
  *--sp = 0;
  *--sp = 0;
  *--sp = 0;
  *--sp = 0;
  *--sp = 0;
  *--sp = 0;
  *--sp = 0;
  *--sp = 0;
  *--sp = 0;
  *--sp = 0;
  *--sp = 0;
  *--sp = (uint32_t)((image != NULL) ? user_entry : 0);

  uint32_t *page_table = (uint32_t *)alloc_pages(1);
  for (paddr_t paddr = (paddr_t)__kernel_base;
       paddr < (paddr_t)__free_ram_end; paddr += PAGE_SIZE)
    map_page(page_table, paddr, paddr, PAGE_R | PAGE_W | PAGE_X);

  map_page(page_table, VIRTIO_BLK_PADDR, VIRTIO_BLK_PADDR, PAGE_R | PAGE_W);

  if (image) {
    for (uint32_t off = 0; off < image_size; off += PAGE_SIZE) {
      paddr_t page = alloc_pages(1);
      size_t remaining = image_size - off;
      size_t copy_size = PAGE_SIZE <= remaining ? PAGE_SIZE : remaining;
      memcpy((void *)page, (const char *)image + off, copy_size);
      map_page(page_table, USER_BASE + off, page,
               PAGE_U | PAGE_R | PAGE_W | PAGE_X);
    }
  }

  proc->pid = i + 1;
  proc->state = PROC_RUNNABLE;
  proc->sp = (uint32_t)sp;
  proc->page_table = page_table;
  return proc;
}

struct process *current_proc;
struct process *idle_proc;

void yield(void) {
  struct process *next = idle_proc;
  for (int i = 0; i < PROCS_MAX; i++) {
    struct process *proc = &procs[(current_proc->pid + i) % PROCS_MAX];
    if (proc->state == PROC_RUNNABLE && proc->pid > 0) {
      next = proc;
      break;
    }
  }

  if (next == current_proc)
    return;

  __asm__ __volatile__(
      "sfence.vma\n"
      "csrw satp, %[satp]\n"
      "sfence.vma\n"
      "csrw sscratch, %[sscratch]\n"
      :
      : [satp] "r"(SATP_SV32 | ((uint32_t)next->page_table / PAGE_SIZE)),
        [sscratch] "r"((uint32_t)&next->stack[sizeof(next->stack)]));

  struct process *prev = current_proc;
  current_proc = next;
  switch_context(&prev->sp, &next->sp);
}

void handle_syscall(struct trap_frame *f) {
  switch (f->a3) {
    case SYS_PUTCHAR:
      putchar(f->a0);
      break;
    case SYS_GETCHAR:
      while (1) {
        long ch = sbi_getchar();
        if (ch >= 0) {
          f->a0 = ch;
          break;
        }
        yield();
      }
      break;
    case SYS_EXIT:
      printf("process %d exited\n", current_proc->pid);
      current_proc->state = PROC_EXITED;
      yield();
      PANIC("unreachable");
    default:
      PANIC("unexpected syscall a3=%x\n", f->a3);
  }
}

uint32_t virtio_reg_read32(unsigned offset) {
  return *((volatile uint32_t *)(VIRTIO_BLK_PADDR + offset));
}

uint64_t virtio_reg_read64(unsigned offset) {
  return *((volatile uint64_t *)(VIRTIO_BLK_PADDR + offset));
}

void virtio_reg_write32(unsigned offset, uint32_t value) {
  *((volatile uint32_t *)(VIRTIO_BLK_PADDR + offset)) = value;
}

void virtio_reg_fetch_and_or32(unsigned offset, uint32_t value) {
  virtio_reg_write32(offset, virtio_reg_read32(offset) | value);
}

struct virtio_virtq *blk_request_vq;
struct virtio_blk_req *blk_req;
paddr_t blk_req_paddr;
uint64_t blk_capacity;

struct virtio_virtq *virtq_init(unsigned index) {
  paddr_t virtq_paddr =
      alloc_pages(align_up(sizeof(struct virtio_virtq), PAGE_SIZE) / PAGE_SIZE);
  struct virtio_virtq *vq = (struct virtio_virtq *)virtq_paddr;
  vq->queue_index = index;
  vq->used_index = (volatile uint16_t *)&vq->used.index;
  virtio_reg_write32(VIRTIO_REG_QUEUE_SEL, index);
  virtio_reg_write32(VIRTIO_REG_QUEUE_NUM, VIRTQ_ENTRY_NUM);
  virtio_reg_write32(VIRTIO_REG_QUEUE_PFN, virtq_paddr / PAGE_SIZE);
  return vq;
}

void virtq_kick(struct virtio_virtq *vq, int desc_index) {
  vq->avail.ring[vq->avail.index % VIRTQ_ENTRY_NUM] = desc_index;
  vq->avail.index++;
  __sync_synchronize();
  virtio_reg_write32(VIRTIO_REG_QUEUE_NOTIFY, vq->queue_index);
  vq->last_used_index++;
}

bool virtq_is_busy(struct virtio_virtq *vq) {
  return vq->last_used_index != *vq->used_index;
}

void virtio_blk_init(void) {
  if (virtio_reg_read32(VIRTIO_REG_MAGIC) != 0x74726976) {
    printf("virtio: virtio-blk device not found\n");
    return;
  }
  if (virtio_reg_read32(VIRTIO_REG_VERSION) != 1) {
    printf("virtio: invalid version\n");
    return;
  }
  if (virtio_reg_read32(VIRTIO_REG_DEVICE_ID) != VIRTIO_DEVICE_BLK) {
    printf("virtio: invalid device id\n");
    return;
  }

  // 1. Reset the device.
  virtio_reg_write32(VIRTIO_REG_DEVICE_STATUS, 0);
  // 2. Set ACKNOWLEDGE status bit.
  virtio_reg_fetch_and_or32(VIRTIO_REG_DEVICE_STATUS, VIRTIO_STATUS_ACK);
  // 3. Set DRIVER status bit.
  virtio_reg_fetch_and_or32(VIRTIO_REG_DEVICE_STATUS, VIRTIO_STATUS_DRIVER);
  // Set page size: 4KB pages.
  virtio_reg_write32(VIRTIO_REG_PAGE_SIZE, PAGE_SIZE);
  // Initialize queue for disk read/write requests.
  blk_request_vq = virtq_init(0);
  // 6. Set DRIVER_OK status bit.
  virtio_reg_write32(VIRTIO_REG_DEVICE_STATUS, VIRTIO_STATUS_DRIVER_OK);

  // Get the disk capacity.
  blk_capacity = virtio_reg_read64(VIRTIO_REG_DEVICE_CONFIG + 0) * SECTOR_SIZE;
  printf("virtio-blk: capacity is %d bytes\n", (int)blk_capacity);

  // Allocate a region to store requests to the device.
  blk_req_paddr = alloc_pages(align_up(sizeof(*blk_req), PAGE_SIZE) / PAGE_SIZE);
  blk_req = (struct virtio_blk_req *)blk_req_paddr;
}

void read_write_disk(void *buf, unsigned sector, int is_write) {
  if (!blk_request_vq || !blk_req) {
    printf("virtio: device not initialized\n");
    return;
  }

  if (sector >= blk_capacity / SECTOR_SIZE) {
    printf("virtio: tried to read/write sector=%d, but capacity is %d\n",
           sector, (int)(blk_capacity / SECTOR_SIZE));
    return;
  }

  // Construct request according to virtio-blk specification.
  blk_req->sector = sector;
  blk_req->type = is_write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN;
  if (is_write)
    memcpy(blk_req->data, buf, SECTOR_SIZE);

  // Construct virtqueue descriptors (using 3 descriptors).
  struct virtio_virtq *vq = blk_request_vq;
  vq->descs[0].addr = blk_req_paddr;
  vq->descs[0].len = sizeof(uint32_t) * 2 + sizeof(uint64_t);
  vq->descs[0].flags = VIRTQ_DESC_F_NEXT;
  vq->descs[0].next = 1;

  vq->descs[1].addr = blk_req_paddr + offsetof(struct virtio_blk_req, data);
  vq->descs[1].len = SECTOR_SIZE;
  vq->descs[1].flags = VIRTQ_DESC_F_NEXT | (is_write ? 0 : VIRTQ_DESC_F_WRITE);
  vq->descs[1].next = 2;

  vq->descs[2].addr = blk_req_paddr + offsetof(struct virtio_blk_req, status);
  vq->descs[2].len = sizeof(uint8_t);
  vq->descs[2].flags = VIRTQ_DESC_F_WRITE;

  // Notify device that there is a new request.
  virtq_kick(vq, 0);

  // Wait until device finishes processing (polling).
  while (virtq_is_busy(vq))
    ;

  // virtio-blk: status != 0 means error.
  if (blk_req->status != 0) {
    printf("virtio: warn: failed to read/write sector=%d status=%d\n",
           sector, blk_req->status);
    return;
  }

  // For read operations, copy data into buffer.
  if (!is_write)
    memcpy(buf, blk_req->data, SECTOR_SIZE);
}

void read_disk(void *buf, unsigned sector) {
  read_write_disk(buf, sector, 0);
}

void write_disk(const void *buf, unsigned sector) {
  read_write_disk((void *)buf, sector, 1);
}

void kernel_main(void) {
  memset(__bss, 0, (size_t)__bss_end - (size_t)__bss);

  WRITE_CSR(stvec, (uint32_t)kernel_entry);

  virtio_blk_init();

  if (blk_capacity > 0) {
    char disk_buf[SECTOR_SIZE];
    read_write_disk(disk_buf, 0, false);
    disk_buf[sizeof(disk_buf) - 1] = '\0';
    printf("virtio-blk: sector 0 read: %s\n", disk_buf);

    strcpy(disk_buf, "sector 1 persisted write test");
    read_write_disk(disk_buf, 1, true);

    memset(disk_buf, 0, sizeof(disk_buf));
    read_write_disk(disk_buf, 1, false);
    disk_buf[sizeof(disk_buf) - 1] = '\0';
    printf("virtio-blk: sector 1 verified: %s\n", disk_buf);

    read_write_disk(disk_buf, blk_capacity / SECTOR_SIZE + 10, false);
  }

  idle_proc = create_process(NULL, 0);
  idle_proc->pid = 0;
  current_proc = idle_proc;

  if (_binary_shell_bin_start && (size_t)_binary_shell_bin_size > 0) {
    uint8_t *shell_bin = (uint8_t *)_binary_shell_bin_start;
    printf("shell_bin size = %d\n", (int)_binary_shell_bin_size);
    printf("shell_bin[0] = %x\n", shell_bin[0]);

    create_process(_binary_shell_bin_start, (size_t)_binary_shell_bin_size);
  }

  yield();
  PANIC("switched to idle process");
}

__attribute__((section(".text.boot"))) __attribute__((naked)) void boot(void) {
  __asm__ __volatile__("mv sp, %[stack_top]\n"
                       "j kernel_main\n"
                       :
                       : [stack_top] "r"(__stack_top));
}
