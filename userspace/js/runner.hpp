#ifndef REIST_JS_RUNNER_HPP
#define REIST_JS_RUNNER_HPP
#include "js_session.hpp"
#include "script_protocol.h"
namespace reist::script {
struct Source final { char *packet=nullptr; uint32_t length=0; };
// Return shell status. Ownership transfers only on success; release once.
int prepare(int argc,const char *const *argv,Source &);
void release(Source &);
int publish(const void *,uint32_t);
int settle(JsSession &,bool keyboard=false);
int execute(const Source &,bool keyboard=true);
int diagnostic(int code);
}
#endif
