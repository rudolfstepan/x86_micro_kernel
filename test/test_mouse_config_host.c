#include <assert.h>
#include <stdio.h>
#include <string.h>
#define main config_program_main
#include "userspace/services/config/config_service.c"
#undef main

static char saved[4096], staged[4096];
static size_t cursor, staged_size;
static int failure, opens, publications;
static const char original[] = "schema=reist.input/1\nkeyboard.layout=de\nfuture.option=keep\n";
int x86os_getpid(void) { return 37; }
void x86os_puts(const char *s) { (void)s; }
int x86os_open(const char *p) { assert(!strcmp(p,"/etc/reist/input.conf")); ++opens; cursor=0; return 1; }
int x86os_create(const char *p) { assert(!strcmp(p,"/etc/reist/RIC00037.TMP")); staged_size=0; return failure==1 ? -5 : 2; }
int x86os_read(int fd, void *out, size_t n) {
    assert(fd==1); size_t remaining=strlen(saved)-cursor;
    if (n>remaining) n=remaining;
    memcpy(out,saved+cursor,n); cursor+=n; return (int)n;
}
int x86os_write(int fd, const void *in, size_t n) {
    assert(fd==2); if (failure==2) return -5;
    if (n>13) n=13; /* Real writer must handle partial writes. */
    assert(staged_size+n<sizeof(staged)); memcpy(staged+staged_size,in,n);
    staged_size+=n; staged[staged_size]=0; return (int)n;
}
int x86os_fsync(int fd) { assert(fd==2); return failure==3 ? -5 : 0; }
int x86os_close(int fd) { return failure==4 && fd==2 ? -5 : 0; }
int x86os_unlink(const char *p) { (void)p; return 0; }
int x86os_rename(const char *a,const char *b) {
    assert(!strcmp(a,"/etc/reist/RIC00037.TMP") && !strcmp(b,"/etc/reist/input.conf"));
    if (failure==5) return -5;
    strcpy(saved,staged); ++publications; return 0;
}
static void reset(void) { strcpy(saved,original); opens=publications=0; failure=0; }
int main(void) {
    char *args[]={"config","set","input","mouse.primary_button","right",
        "mouse.speed_percent","150","mouse.acceleration","flat",
        "mouse.natural_scroll","true","mouse.double_click_ms","750"};
    reset(); assert(reist_config_service_main(13,args)==0);
    assert(opens==1 && publications==1);
    assert(strstr(saved,"keyboard.layout=de\n") && strstr(saved,"future.option=keep\n"));
    assert(strstr(saved,"mouse.double_click_ms=750\n") && strstr(saved,"mouse.primary_button=right\n"));
    for (int bad=1;bad<=5;++bad) {
        reset(); failure=bad; assert(reist_config_service_main(13,args)==1);
        assert(!strcmp(saved,original) && publications==0);
    }
    reset(); args[12]="1001"; assert(reist_config_service_main(13,args)==2);
    assert(!opens && !publications); args[12]="750";
    reset(); args[11]=args[3]; assert(reist_config_service_main(13,args)==2);
    assert(!opens); args[11]="mouse.double_click_ms";
    reset(); assert(reist_config_service_main(12,args)==2 && !opens);
    assert(reist_config_service_main(15,args)==2 && !opens);
    reset(); assert(reist_config_service_main(5,args)==0 && publications==1);
    assert(strstr(saved,"mouse.primary_button=right\n"));
    puts("MOUSE_TEST_OK"); return 0;
}
