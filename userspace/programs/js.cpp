#include "runner.hpp"
#include <reist/libc.h>
#include <string.h>
extern "C" int main(int argc,char **argv) {
    if(argc==2 && !strcmp(argv[1],"--help")) {
        reist::script::diagnostic(64); return 0;
    }
    if(reist_libc_init_process(8U*1024U*1024U)) return 71;
    reist::script::Source source;
    int result=reist::script::prepare(argc,argv,source);
    if(!result) result=reist::script::execute(source);
    else reist::script::diagnostic(result);
    reist::script::release(source);
    if(reist_libc_reset()) return 70;
    return result;
}
