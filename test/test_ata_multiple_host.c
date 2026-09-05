#undef NDEBUG
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "drivers/block/ata.h"

#define ATA_WAIT_TIMEOUT_MS 500U
#define ATA_POLL_DELAY_MS 1U
drive_t detected_drives[MAX_DRIVES];
typedef struct {
    uint16_t base; uint32_t lba; bool is_master, valid; uint8_t data[512];
} ata_cache_entry_t;
static ata_cache_entry_t caches[32];
static uint32_t consecutive_read_failures;
static uint64_t now;
static uint32_t sleeps, spins, commands, identifies, sets, transferred;
static uint32_t blocks[20], block_count, busy, step, fail_block;
static uint8_t command, count_register, last_read_command;
static uint8_t reg_values[16];
static unsigned reg_count;
static uint16_t max_word, current_word;
static bool frozen, sleep_fail, sleepable, irqs, in_irq, fail_final, fail_set, fail_identify;
static int task, forced_status;
static uint8_t port_status(void) {
    if (forced_status >= 0) return (uint8_t)forced_status;
    if (busy) { --busy; return 0x81; } /* ERR is not valid under BSY. */
    if (command == ATA_IDENTIFY) return fail_identify ? 0x41 : 0x48;
    if (command == ATA_SET_MULTIPLE_MODE) return fail_set ? 0x41 : 0x40;
    if (command == ATA_READ_MULTIPLE || command == ATA_READ_MULTIPLE_EXT ||
        command == ATA_READ_SECTORS || command == ATA_READ_SECTORS_EXT) {
        if (fail_block && block_count == fail_block) return 0x41;
        if (transferred < count_register) return 0x48;
        return fail_final ? 0x41 : 0x40;
    }
    return 0x40;
}
static uint8_t inb(uint16_t port) { (void)port; return port_status(); }
static void outb(uint16_t port, uint8_t value) {
    if (port == ATA_SECTOR_CNT(0x1f0)) count_register=value;
    if (port == ATA_COMMAND(0x1f0)) {
        command=value; ++commands;
        if (value==ATA_IDENTIFY) ++identifies;
        else if (value==ATA_SET_MULTIPLE_MODE) {
            ++sets; if (!fail_set) current_word=0x100U | count_register;
        } else last_read_command=value;
    } else if (port >= ATA_SECTOR_CNT(0x1f0) && port <= ATA_LBA_HIGH(0x1f0)) {
        if (reg_count<sizeof(reg_values)) reg_values[reg_count++]=value;
    }
}
static void insw(uint16_t port, void *buffer, unsigned words) {
    (void)port;
    if (command==ATA_IDENTIFY) {
        assert(words==256); uint16_t *data=buffer;
        memset(data,0,512); data[0]=0x40; data[47]=max_word; data[59]=current_word;
        command=0; return;
    }
    assert(words && words%256==0 && words<=20*256);
    assert(block_count<20); blocks[block_count++]=words/256;
    for (unsigned i=0;i<words/256;i++)
        memset((uint8_t *)buffer+i*512,(int)(transferred+i+1),512);
    transferred+=words/256;
}
uint16_t ata_control_port_for_base(uint16_t base) { return base+0x206; }
static void ata_selection_delay(uint16_t base) {
    for (unsigned i=0;i<4;i++) (void)inb(ATA_ALT_STATUS(base));
}
static uint64_t pit_monotonic_ms(void) { return now; }
static bool scheduler_can_sleep(void) { return sleepable; }
static int scheduler_current_task_id(void) { return task; }
static bool irq_enabled(void) { return irqs; }
static bool irq_in_context(void) { return in_irq; }
static int scheduler_sleep_ms(uint32_t ms) {
    ++sleeps; if (!frozen) now+=step ? step : ms;
    return sleep_fail ? -1 : 0;
}
static void pit_delay(uint32_t ms) { ++spins; if (!frozen) now+=ms; }
static bool ata_select_target(uint16_t base, uint8_t head, uint32_t ms) {
    (void)base; (void)head; (void)ms; return true;
}
static int ata_resource_index(uint16_t base, bool master) {
    return base==0x1f0 && master ? 0 : -1;
}
static ata_cache_entry_t *ata_cache_slot(uint16_t base, uint32_t lba, bool master) {
    (void)base; (void)master; return &caches[lba%32];
}
/* PRODUCTION */
static void reset(void) {
    memset(caches,0,sizeof(caches)); memset(detected_drives,0,sizeof(detected_drives));
    detected_drives[0].sectors=UINT32_MAX; detected_drives[0].lba48_supported=true;
    now=sleeps=spins=commands=identifies=sets=transferred=0;
    block_count=busy=step=fail_block=0; command=count_register=last_read_command=0;
    max_word=0x8010; current_word=0x110; frozen=sleep_fail=false;
    sleepable=irqs=true; in_irq=fail_final=fail_set=fail_identify=false; task=1;
    reg_count=0; memset(reg_values,0,sizeof(reg_values));
    forced_status=-1;
}
static unsigned cache_count(void) {
    unsigned count=0; for(unsigned i=0;i<32;i++) count+=caches[i].valid; return count;
}
int main(void) {
    uint8_t bytes[20*512+2];
    reset(); memset(bytes,0xEE,sizeof(bytes));
    assert(ata_read_sectors_pio_impl(0x1f0,12,20,bytes+1,true));
    assert(identifies==1 && sets==0 && block_count==2);
    assert(blocks[0]==16 && blocks[1]==4 && last_read_command==ATA_READ_MULTIPLE);
    assert(cache_count()==20 && bytes[0]==0xEE && bytes[sizeof(bytes)-1]==0xEE);
    for(unsigned i=0;i<20*512;i++) assert(bytes[i+1]==i/512+1);
    /* Fresh identity after a reset-like mode change; no cached enabled mode. */
    command=0; transferred=block_count=0; current_word=0;
    assert(ata_read_sectors_pio_impl(0x1f0,12,20,bytes,true));
    assert(identifies==2 && sets==1 && current_word==0x110);
    reset(); current_word=0x104;
    assert(ata_read_sectors_pio_impl(0x1f0,12,9,bytes,true));
    assert(block_count==3 && blocks[0]==4 && blocks[1]==4 && blocks[2]==1);
    reset(); assert(ata_read_sectors_pio_impl(0x1f0,ATA_LBA28_LIMIT,3,bytes,true));
    assert(last_read_command==ATA_READ_MULTIPLE_EXT && block_count==1);
    assert(reg_count==8 && reg_values[0]==0 && reg_values[1]==0x10 && reg_values[4]==3);
    reset(); detected_drives[0].lba48_supported=false;
    assert(!ata_read_sectors_pio_impl(0x1f0,ATA_LBA28_LIMIT,3,bytes,true)); assert(!commands);
    reset(); assert(ata_read_sectors_pio_impl(0x1f0,ATA_LBA28_LIMIT-1,3,bytes,true));
    assert(last_read_command==ATA_READ_MULTIPLE_EXT);
    reset(); assert(ata_read_sectors_pio_impl(0x1f0,0,1,bytes,true));
    assert(!identifies && !sets && last_read_command==ATA_READ_SECTORS);
    reset(); max_word=0x8080; current_word=0x180;
    assert(ata_read_sectors_pio_impl(0x1f0,0,20,bytes,true));
    assert(block_count==1 && blocks[0]==20);
    reset(); max_word=0x8080; current_word=0;
    assert(ata_read_sectors_pio_impl(0x1f0,0,20,bytes,true));
    assert(sets==1 && current_word==0x110);
    reset(); assert(ata_program_pio_batch(0x1f0,5,3,true,true,false,false,0));
    assert(last_read_command==ATA_WRITE_SECTORS && count_register==3 && !identifies);
    reset(); assert(ata_program_pio_batch(0x1f0,ATA_LBA28_LIMIT,3,true,true,true,false,0));
    assert(last_read_command==ATA_WRITE_SECTORS_EXT && count_register==3 && !identifies);
    reset(); assert(!ata_read_sectors_pio_impl(0x1f0,UINT32_MAX-1,3,bytes,true)); assert(!commands);
    reset(); assert(!ata_read_sectors_pio_impl(0x1f0,0,21,bytes,true)); assert(!commands);
    reset(); assert(!ata_read_sectors_pio_impl(0x1f0,0,0,bytes,true)); assert(!commands);
    reset(); assert(!ata_read_sectors_pio_impl(0x1f0,0,1,NULL,true)); assert(!commands);
    for(unsigned i=0;i<4;i++) {
        reset(); uint16_t bad[]={0,0x8011,0xff10,0x8001}; max_word=bad[i];
        assert(ata_read_sectors_pio_impl(0x1f0,0,3,bytes,true));
        assert(last_read_command==ATA_READ_SECTORS && block_count==3 && !sets);
    }
    reset(); current_word=0x103;
    assert(ata_read_sectors_pio_impl(0x1f0,0,3,bytes,true));
    assert(last_read_command==ATA_READ_SECTORS && !sets);
    reset(); fail_identify=true;
    assert(!ata_read_sectors_pio_impl(0x1f0,0,20,bytes,true)); assert(commands==1 && !cache_count());
    reset(); current_word=0; fail_set=true;
    assert(!ata_read_sectors_pio_impl(0x1f0,0,20,bytes,true)); assert(commands==2 && !cache_count());
    reset(); fail_block=1;
    assert(!ata_read_sectors_pio_impl(0x1f0,0,20,bytes,true));
    assert(transferred==16 && commands==2 && !cache_count());
    reset(); fail_final=true;
    assert(!ata_read_sectors_pio_impl(0x1f0,0,20,bytes,true)); assert(!cache_count());
    for(unsigned i=0;i<4;i++) {
        reset(); int bad[]={0,0xff,0x41,0x60}; forced_status=bad[i];
        assert(!ata_pio_wait_status(0x1f0,0x08,0,false,5)); assert(!sleeps);
    }
    reset(); forced_status=0x48;
    assert(!ata_pio_wait_status(0x1f0,0x40,0x08,false,5)); assert(!sleeps);
    reset(); busy=1; assert(ata_pio_wait_status(0x1f0,0x40,0x08,false,5));
    assert(sleeps==1 && !spins);
    reset(); busy=1000; frozen=true;
    assert(!ata_pio_wait_status(0x1f0,0x40,0x08,false,5)); assert(sleeps<=502 && !spins);
    reset(); busy=1; step=10;
    assert(!ata_pio_wait_status(0x1f0,0x40,0x08,false,5)); assert(sleeps==1);
    reset(); busy=1; sleep_fail=true;
    assert(!ata_pio_wait_status(0x1f0,0x40,0x08,false,5)); assert(sleeps==1);
    reset(); busy=1; sleepable=false;
    assert(!ata_pio_wait_status(0x1f0,0x40,0x08,false,5)); assert(!sleeps && !spins);
    reset(); busy=1; task=-1;
    assert(ata_pio_wait_status(0x1f0,0x40,0x08,false,5)); assert(spins==1 && !sleeps);
    reset(); busy=1; task=-1; irqs=false;
    assert(!ata_pio_wait_status(0x1f0,0x40,0x08,false,5)); assert(!spins);
    puts("ATA_MULTIPLE_HOST_OK"); return 0;
}
