#include "mouse_model.h"
#include "reist/vfs_file_client.h"
#include <string.h>

int mouse_model_read(mouse_model_t *m, reist_mouse_settings_t *settings) {
    reist_vfs_file_handle_t file=REIST_VFS_FILE_INVALID_HANDLE;
    int status=reist_vfs_file_open_rights("/etc/reist/input.conf",REIST_VFS_FILE_DEFAULT_TIMEOUT_MS,
        REIST_VFS_FILE_RIGHT_READ|REIST_VFS_FILE_RIGHT_STAT,&file);
    if (status) return status;
    x86os_file_info_t info;
    status=reist_vfs_file_fstat(file,&info);
    if (!status && (info.type!=X86OS_FILE || info.size>sizeof(m->bytes))) status=-75;
    size_t used=0;
    while (!status && used<info.size) {
        size_t count=info.size-used;
        if (count>X86OS_VFS_SHADOW_READ_CAPACITY) count=X86OS_VFS_SHADOW_READ_CAPACITY;
        int amount=reist_vfs_file_read(file,m->bytes+used,count);
        if (amount<=0 || (size_t)amount>count) status=amount<0 ? amount : -5;
        else used+=(size_t)amount;
    }
    char extra;
    if (!status && reist_vfs_file_read(file,&extra,1U)!=0) status=-75;
    int closed=reist_vfs_file_close(file);
    if (!status) status=closed;
    if (!status) status=reist_config_parse(m->bytes,used,"reist.input/1",&m->document);
    if (!status) status=reist_mouse_settings_parse(&m->document,settings);
    return status;
}
void mouse_model_initialize(mouse_model_t *m) {
    memset(m,0,sizeof(*m)); reist_mouse_settings_defaults(&m->saved);
    m->writable=mouse_model_read(m,&m->saved)==0;
    m->draft=m->saved;
    m->status=m->writable ? "Wirksam beim naechsten Desktopstart" : "Nur Lesen: Konfiguration ungueltig";
}
int mouse_model_save(mouse_model_t *m) {
    if (!m->writable || m->child || m->fatal || !reist_mouse_settings_valid(&m->draft)) return -16;
    m->pending=m->draft;
    if (reist_mouse_settings_format(&m->pending,m->values) || x86os_monotonic_ms(&m->started_ms)) return -5;
    const char *args[13]={"/sbin/config.prg","set","input"};
    for (uint32_t i=0;i<5U;++i) { args[3+i*2]=reist_mouse_keys[i]; args[4+i*2]=m->values[i]; }
    int child=x86os_spawnv(args[0],13,args);
    if (child<=0) { m->status="Speichern verweigert"; return child ? child : -5; }
    m->child=child; m->child_generation=m->cancel_sent=0;
    m->status="Speichern ..."; return 0;
}
static int identity_valid(mouse_model_t *m,const x86os_process_identity_t *id) {
    return id->version==1U && id->struct_size==sizeof(*id) && id->pid==m->child &&
        id->generation && (!m->child_generation || id->generation==m->child_generation);
}
int mouse_model_cancel(mouse_model_t *m) {
    if (!m->child) return 0;
    if (m->cancel_sent) return -11;
    x86os_process_identity_t id;
    int status=x86os_process_identity_of(m->child,&id);
    if (status==-3) { (void)mouse_model_poll(m); return m->child ? -11 : 0; }
    m->cancel_sent=1;
    if (status || !identity_valid(m,&id)) { m->fatal=1; m->status="Abbruch: Identitaet ungueltig"; return -13; }
    m->child_generation=id.generation;
    status=x86os_kill(m->child);
    m->status=status ? "Abbruch verweigert" : "Speichern abgebrochen";
    if (status) m->fatal=1;
    return status;
}
int mouse_model_poll(mouse_model_t *m) {
    if (!m->child || m->fatal) return 0;
    x86os_process_identity_t id;
    int status=x86os_process_identity_of(m->child,&id);
    if (status==-3) {
        int child=m->child, exit_status=-1;
        int waited=x86os_wait(child,&exit_status); /* Only observed-exited owned child. */
        m->child=0;
        reist_mouse_settings_t saved;
        if (waited==child && !exit_status && !m->cancel_sent &&
            !mouse_model_read(m,&saved) && !memcmp(&saved,&m->pending,sizeof(saved))) {
            m->saved=saved; m->status="Gespeichert: naechster Desktopstart";
            x86os_puts("MOUSE_SETTINGS_SAVED\n");
        } else m->status="Nicht bestaetigt: bitte neu oeffnen";
        return 1;
    }
    if (status || !identity_valid(m,&id)) {
        m->fatal=1; m->writable=0; m->status="Prozessidentitaet ungueltig"; return 1;
    }
    m->child_generation=id.generation;
    uint64_t now=0;
    if (x86os_monotonic_ms(&now) || now<m->started_ms || now-m->started_ms>=5000U) {
        if (!m->cancel_sent) (void)mouse_model_cancel(m);
        else if (now<m->started_ms || now-m->started_ms>=6000U) m->fatal=1;
        return 1;
    }
    return 0;
}
