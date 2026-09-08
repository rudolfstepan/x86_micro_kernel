#ifndef REIST_JS_FILE_BROKER_HPP
#define REIST_JS_FILE_BROKER_HPP
#include "file_protocol.h"
namespace reist::script {
class FileBroker final {
public:
    FileBroker()=default;
    FileBroker(const FileBroker&)=delete;
    FileBroker& operator=(const FileBroker&)=delete;
    ~FileBroker(); // Explicit bounded close, never fallible destructor cleanup.
    int admit(const char paths[4][192],uint32_t count);
    int serve(const void *,uint32_t size,uint64_t deadline);
    int close();
    const js_file_manifest &manifest() const {return manifest_;}
    const void *data() const {return response_;}
    uint32_t size() const {return response_size_;}
    bool uncertain() const {return uncertain_;}
private:
    js_file_manifest manifest_{};
    uint32_t handles_[4]{},calls_=0,bytes_=0,response_size_=0;
    uint64_t last_=0;
    char *response_=nullptr;
    bool uncertain_=false;
    int remaining(uint64_t,uint32_t &);
};
}
#endif
