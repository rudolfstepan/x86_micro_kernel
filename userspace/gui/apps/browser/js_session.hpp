#ifndef BROWSER_JS_SESSION_HPP
#define BROWSER_JS_SESSION_HPP
#include "js_protocol.h"
#include "x86os.h"
namespace reist::browser {
class JsSession final {
public:
    enum class State { closed,sending,receiving,idle,reaping,failed,stranded };
    JsSession() = default;
    JsSession(const JsSession&)=delete;
    JsSession& operator=(const JsSession&)=delete;
    ~JsSession(); // Requires explicit close/cancel + successful reap; never blocks.
    int start(uint32_t document,uint64_t seed,uint32_t fixture_mode=0);
    // Source and staging are borrowed until the operation stops being busy.
    // Successful result() is then a read-only view of caller storage; cleanup
    // invalidates the view without touching that already published buffer.
    int evaluate(const char *,uint32_t length,char *staging,uint32_t capacity,uint32_t budget_ms=5000);
    int health(bool collect=false);
    int shutdown();
    void cancel();
    void poll(); // <=8 timeout-zero IPC operations; caller yields on no progress.
    State state() const { return phase_; }
    bool busy() const { return phase_==State::sending || phase_==State::receiving || phase_==State::reaping; }
    bool ready() const { return phase_==State::idle; }
    int error() const { return error_; }
    int exit_status() const { return exit_status_; }
    uint32_t engine_status() const { return engine_status_; }
    int pid() const { return pid_; }
    uint32_t generation() const { return header_.child_generation; }
    uint32_t progress() const { return progress_; }
    const char *result() const;
    uint32_t result_length() const { return result() ? receive_.total : 0; }
    const uint32_t *stats() const;
private:
    State phase_=State::closed;
    x86os_ipc_handle_t request_=0,reply_=0;
    int pid_=0,error_=0,exit_status_=-1;
    uint32_t engine_status_=0,recoveries_=0,document_=0,sent_=0,input_size_=0,capacity_=0,progress_=0;
    uint64_t last_=0,reap_deadline_=0,seed_=0;
    bool sent_empty_=false,killed_=false;
    const char *input_=nullptr;
    char *output_=nullptr;
    uint32_t internal_[6]{};
    js_service_header header_{};
    js_service_receive receive_{0,UINT32_MAX,UINT32_MAX};
    int clock(uint64_t &);
    int peer();
    int begin(uint32_t op,const void *,uint32_t,void *,uint32_t,uint32_t);
    void fence();
    void fail(int);
    void reap();
};
}
#endif
