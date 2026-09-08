#ifndef REIST_SCRIPT_FETCH_HPP
#define REIST_SCRIPT_FETCH_HPP
#include "script_protocol.h"
#include "browser_resources.h"
#include "x86os.h"
namespace reist::browser {
class ScriptFetch final {
public:
    ScriptFetch()=default;
    ScriptFetch(const ScriptFetch&)=delete;
    ScriptFetch& operator=(const ScriptFetch&)=delete;
    int start(const char *document,const char *canonical);
    void poll(); // <=8 zero-timeout receives OR one <=4KiB local read.
    void cancel();
    void reset_cache(); // Caller must fence JS's borrowed source first.
    int release(); // Explicit, only after successful reap; destructor never waits.
    bool busy() const { return state_==1 || state_==2 || state_==4; }
    bool ready() const { return state_==3; }
    bool skipped() const { return ready() && skipped_; }
    bool stranded() const { return state_==6; }
    int error() const { return error_; }
    const char *data() const { return ready() && !skipped_ ? result_ : nullptr; }
    uint32_t length() const { return data()?length_:0; }
    uint32_t progress() const { return progress_; }
    int pid() const { return pid_; }
private:
    struct Cache;
    Cache *cache_=nullptr;
    uint8_t *buffer_=nullptr;
    const char *result_=nullptr;
    uint32_t state_=0,endpoint_=0,generation_=0,used_=0,total_=0,length_=0,redirects_=0,progress_=0;
    int pid_=0,fd_=-1,error_=0;
    bool skipped_=false,killed_=false;
    uint64_t last_=0,deadline_=0,reap_deadline_=0,alias_deadline_=UINT64_MAX;
    char document_[BROWSER_RESOURCE_URL_CAPACITY]{},initial_[BROWSER_RESOURCE_URL_CAPACITY]{},url_[BROWSER_RESOURCE_URL_CAPACITY]{};
    int clock();
    int peer();
    int hop();
    void fence();
    void fail(int);
    void finish();
    void complete(uint32_t,uint32_t,uint32_t);
};
}
#endif
