/* R3.38 guest proof. Run only on disposable test media. No private guard
 * syscall authority: this is an ordinary userspace-shell program. */
#include "x86os.h"
#include "../storage/include/reist/vfs_file_client.h"
#include "../storage/include/reist/vfs_namespace_client.h"
#include "../storage/include/reist/vfs_symlink_client.h"

static int equal(const char *a, const char *b) {
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static int fail(int stage, int result) {
    x86os_puts("OBJGUARD FAIL stage="); x86os_print_number(stage);
    x86os_puts(" result="); x86os_print_number(result); x86os_putchar('\n');
    return stage;
}
#define REQUIRE(x, n) do { int result_ = (x); if (result_ != 0) return fail(n, result_); } while (0)
#define EXPECT(x, want, n) do { int result_ = (x); if (result_ != (want)) return fail(n, result_); } while (0)
static void decimal(uint32_t value, char out[11]) {
    char reversed[10]; unsigned n=0, i=0;
    do { reversed[n++] = (char)('0'+value%10); value/=10; } while (value && n<10);
    while (n) out[i++] = reversed[--n];
    out[i] = 0;
}
static uint32_t number(const char *text) {
    uint32_t n=0;
    for (unsigned i=0; text[i]; ++i) {
        if (i>=10 || text[i]<'0' || text[i]>'9' || n>(UINT32_MAX-9)/10) return 0;
        n=n*10+(uint32_t)(text[i]-'0');
    }
    return n;
}
static int join(char out[128], const char *root, const char *name) {
    unsigned i=0, j=0;
    while (root[i] && i<96) { out[i]=root[i]; ++i; }
    if (root[i] || !i || root[0]!='/') return -22;
    if (out[i-1]!='/') out[i++]='/';
    while (name[j] && i<127) out[i++]=name[j++];
    out[i]=0; return name[j] ? -22 : 0;
}
static int open_file(const char *path, reist_vfs_file_handle_t *handle) {
    int result = reist_vfs_file_open_rights(path, 4000, REIST_VFS_FILE_RIGHT_ALL, handle);
    /* EAGAIN means no object was published, never replay a write. */
    if (result == -11) result = reist_vfs_file_open_rights(path, 4000, REIST_VFS_FILE_RIGHT_ALL, handle);
    return result;
}
static int wait_child(int pid, uint32_t timeout, int *exit_status) {
    uint64_t start=0, now=0;
    if (pid<=0 || x86os_monotonic_ms(&start)) return -22;
    do {
        for (uint32_t i=0; i<32; ++i) {
            x86os_process_info_t info;
            int result=x86os_process_info(i, &info);
            if (result<0) return result;
            if (!result) break;
            if (info.pid==pid && info.state==X86OS_PROCESS_ZOMBIE)
                return x86os_wait(pid, exit_status)==pid ? 0 : -5;
        }
        if (x86os_sleep_ms(10) || x86os_monotonic_ms(&now) || now<start) return -5;
    } while (now-start<timeout);
    (void)x86os_kill(pid);
    return -110;
}
static int child_adopt(uint32_t endpoint) {
    reist_vfs_file_handle_t handle=0;
    uint64_t start=0, now=0;
    REQUIRE(x86os_monotonic_ms(&start), 70);
    int result;
    do {
        result=reist_vfs_file_adopt(1000, &handle);
        if (!result) break;
        if (result!=-11) return fail(71, result);
        REQUIRE(x86os_sleep_ms(10),72);
        REQUIRE(x86os_monotonic_ms(&now),73);
    } while (now>=start && now-start<3000);
    if (result) return fail(74,result);
    uint8_t byte;
    EXPECT(reist_vfs_file_read(handle,&byte,1),-13,75); /* STAT-only delegation */
    x86os_file_info_t info;
    REQUIRE(reist_vfs_file_fstat(handle,&info),76);
    x86os_ipc_message_t message={.version=X86OS_IPC_MESSAGE_VERSION,
        .struct_size=sizeof(message),.length=1};
    message.payload[0]=0x38;
    REQUIRE(x86os_ipc_send_timeout(endpoint,&message,1000),77);
    REQUIRE(x86os_sleep_ms(2500),78);
    /* Deliberate Ring-3 owner fault: kernel containment/reap must release it. */
    __asm__ volatile("ud2");
    return 79;
}
static int owner_fault(const char *path) {
    reist_vfs_file_handle_t handle=0;
    REQUIRE(open_file(path,&handle),40);
    x86os_ipc_handle_t endpoint=0;
    REQUIRE(x86os_ipc_create(&endpoint),41);
    char encoded[11]; decimal(endpoint,encoded);
    const char *arguments[]={"objgdtst","adopt",encoded};
    int child=x86os_spawnv("/bin/objgdtst.prg",3,arguments);
    if (child<=0) return fail(42,child);
    REQUIRE(x86os_ipc_delegate(endpoint,child,X86OS_IPC_RIGHT_SEND),43);
    x86os_process_identity_t identity;
    REQUIRE(x86os_process_identity_of(child,&identity),44);
    REQUIRE(reist_vfs_file_delegate(handle,&identity,REIST_VFS_FILE_RIGHT_STAT),45);
    x86os_ipc_message_t message={.version=X86OS_IPC_MESSAGE_VERSION,
        .struct_size=sizeof(message)};
    REQUIRE(x86os_ipc_receive_timeout(endpoint,&message,4000),46);
    if (message.version!=X86OS_IPC_MESSAGE_VERSION || message.struct_size!=sizeof(message) ||
        message.length!=1 || message.payload[0]!=0x38) return fail(47,-5);
    REQUIRE(reist_vfs_file_close(handle),48);
    EXPECT(x86os_unlink(path),-2,49); /* legacy syscall's frozen coarse error */
    int status=0;
    REQUIRE(wait_child(child,5000,&status),50);
    if (!status) return fail(51,-5);
    REQUIRE(x86os_ipc_close(endpoint),52);
    REQUIRE(x86os_unlink(path),53);
    x86os_puts("OBJGUARD OWNER_FAULT_OK delegated-stat-only close-source reap\n");
    return 0;
}
static int make_file(const char *path) {
    x86os_file_info_t info;
    int exists=x86os_stat(path,&info);
    if (exists!=-2) return exists==0 ? -17 : exists;
    int fd=x86os_create(path);
    if (fd<0) return fd;
    int written=x86os_write(fd,"old",3);
    int closed=x86os_close(fd);
    return written==3 && !closed ? 0 : -5;
}
static int fat_test(const char *root, int supports_rename) {
    char file[128], alias[128], other[128], moved[128];
    REQUIRE(join(file,root,"og338a.txt"),1);
    REQUIRE(join(alias,root,"OG338A.TXT"),2);
    REQUIRE(join(other,root,"og338b.txt"),3);
    REQUIRE(join(moved,root,"og338c.txt"),4);
    REQUIRE(make_file(file),5); REQUIRE(make_file(other),6);
    reist_vfs_file_handle_t held=0;
    REQUIRE(open_file(alias,&held),7);
    /* Existing syscalls collapse VFS_BUSY to -2 (unlink) / -5 (rename).
     * Do not change ABI0..128 for this mechanism package. Host tests assert
     * the exact VFS_BUSY; this guest also checks the unchanged file bytes. */
    EXPECT(x86os_unlink(file),-2,8);
    if (supports_rename) {
        EXPECT(x86os_rename(file,moved),-5,9);
        EXPECT(x86os_rename(other,alias),-5,10);
    }
    REQUIRE(x86os_unlink(other),11);
    char bytes[3]; EXPECT(reist_vfs_file_read(held,bytes,3),3,12);
    if (bytes[0]!='o'||bytes[1]!='l'||bytes[2]!='d') return fail(13,-5);
    REQUIRE(reist_vfs_file_close(held),14);
    if (supports_rename) {
        REQUIRE(x86os_rename(file,moved),15);
        REQUIRE(x86os_rename(moved,file),16);
    } else {
        /* FAT12 has no rename backend. Do not count its ENOTSUP adapter as
         * proof of BUSY, nor add a new namespace feature to this package. */
        EXPECT(x86os_rename(file,moved),-5,15);
        x86os_puts("OBJGUARD FAT12_RENAME_UNSUPPORTED\n");
    }
    REQUIRE(owner_fault(file),17);
    x86os_puts("OBJGUARD FAT_OK "); x86os_puts(root); x86os_putchar('\n');
    return 0;
}
static int ext2_test(void) {
    const char *path="/mnt/hdd1/target.txt", *alias="/mnt/hdd1/fast-link";
    const char *moved="/mnt/hdd1/renamed.txt";
    REQUIRE(reist_vfs_symlink("target.txt",alias,4000),19);
    reist_vfs_file_handle_t held=0;
    REQUIRE(open_file(alias,&held),20);
    EXPECT(reist_vfs_unlink(path,4000),-16,21);
    EXPECT(reist_vfs_rename(path,moved,4000),-16,22);
    REQUIRE(reist_vfs_file_close(held),23);
    int legacy=x86os_open(path);
    if (legacy<0) return fail(24,legacy);
    EXPECT(reist_vfs_unlink(path,4000),-16,25);
    EXPECT(reist_vfs_rename(path,moved,4000),-16,26);
    REQUIRE(x86os_close(legacy),27);
    REQUIRE(reist_vfs_rename(path,moved,4000),28);
    REQUIRE(reist_vfs_rename(moved,path,4000),29);
    REQUIRE(reist_vfs_unlink(alias,4000),30);
    x86os_puts("OBJGUARD EXT2_OK both-registries symlink-alias close-rename\n");
    return 0;
}
static int restart_test(void) {
    reist_vfs_file_handle_t held=0;
    REQUIRE(open_file("/README.TXT",&held),60);
    const char *arguments[]={"svcctl","restart","5"};
    int pid=x86os_spawnv("/sbin/svcctl.prg",3,arguments), status=-1;
    REQUIRE(wait_child(pid,6000,&status),61);
    EXPECT(status,0,62);
    char bytes[3];
    int read=reist_vfs_file_read(held,bytes,3);
    if (read>=0) return fail(63,read);
    (void)reist_vfs_file_close(held);
    REQUIRE(open_file("/README.TXT",&held),64);
    EXPECT(reist_vfs_file_read(held,bytes,3),3,65);
    REQUIRE(reist_vfs_file_close(held),66);
    x86os_puts("OBJGUARD RESTART_OK stale-denied fresh-read\n");
    return 0;
}
int main(int argc,char **argv) {
    if (argc==3 && equal(argv[1],"adopt")) return child_adopt(number(argv[2]));
    reist_file_object_guard_request_t denied={0};
    denied.version=REIST_FILE_OBJECT_VERSION; denied.struct_size=sizeof(denied);
    denied.operation=REIST_FILE_OBJECT_SNAPSHOT;
    EXPECT(x86os_file_object_guard(&denied),-13,80);
    if (argc==3 && equal(argv[1],"fat")) return fat_test(argv[2],1);
    if (argc==3 && equal(argv[1],"fat12")) return fat_test(argv[2],0);
    if (argc==2 && equal(argv[1],"ext2")) return ext2_test();
    if (argc==2 && equal(argv[1],"restart")) return restart_test();
    x86os_puts("Usage (disposable media): objgdtst fat|fat12 /mount | ext2 | restart\n");
    return 2;
}
