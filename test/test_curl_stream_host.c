#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define main static curl_application_main
#include "userspace/programs/curl.c"
#undef main

static const unsigned char *wire;
static size_t wire_length, wire_at, fragment;
static unsigned char output_bytes[300000];
static size_t output_length;
static unsigned write_calls;
static size_t write_limit;
static unsigned fail_write_at;
static int sink_fd=9;
static uint64_t clock_ms;
static int write_error, close_error, rename_error;
static unsigned closed, renamed, removed;
static unsigned ipc_calls,ipc_used,ipc_total,ipc_fail_at,ipc_not_delegated;
static uint8_t ipc_bytes[300000];
int x86os_sleep_ms(uint32_t ms) { clock_ms+=ms; return 0; }
int x86os_ipc_send_bulk_timeout(x86os_ipc_handle_t h,const x86os_ipc_bulk_message_t *m,uint32_t timeout) {
    assert(h==42 && timeout && timeout<=CURL_IO_TIMEOUT_MS &&
        m->version==X86OS_IPC_BULK_MESSAGE_VERSION && m->struct_size==sizeof(*m));
    ++ipc_calls;
    if(ipc_not_delegated) { --ipc_not_delegated; return -9; }
    if(ipc_fail_at==ipc_calls) return -32;
    reist_curl_ipc_packet_t p; memcpy(&p,m->payload,sizeof(p));
    return reist_curl_ipc_accept(&p,m->length,h,ipc_bytes,sizeof(ipc_bytes),&ipc_used,&ipc_total);
}
int x86os_write(int fd, const void *bytes, size_t length) {
    assert(fd == sink_fd && length <= sizeof(output_bytes) - output_length);
    ++write_calls;
    if(fail_write_at==write_calls) return -28;
    if (write_error) return write_error;
    if(write_limit && length>write_limit) length=write_limit;
    memcpy(output_bytes + output_length, bytes, length); output_length += length;
    return (int)length;
}
int x86os_monotonic_ms(uint64_t *result) { *result = clock_ms++; return 0; }
int x86os_close(int fd) { assert(fd == 9); ++closed; return close_error; }
int x86os_rename(const char *from, const char *to) {
    assert(!strcmp(from, "/tmp/result.curl-part") && !strcmp(to, "/tmp/result"));
    assert(closed && !close_error); ++renamed; return rename_error;
}
int x86os_unlink(const char *path) { assert(!strcmp(path, "/tmp/result.curl-part")); ++removed; return 0; }
static int receive(void *opaque, uint8_t *bytes, uint32_t capacity) {
    (void)opaque;
    size_t amount = wire_length - wire_at;
    if (amount > capacity) amount = capacity;
    if (amount > fragment) amount = fragment;
    memcpy(bytes, wire + wire_at, amount); wire_at += amount; return (int)amount;
}
static int transfer_include(const char *response, size_t split, uint32_t limit, unsigned include) {
    wire = (const unsigned char *)response; wire_length = strlen(response);
    wire_at = output_length = clock_ms = write_calls = 0; fragment = split;
    curl_stream_t stream = {0, 0, receive};
    return include ? receive_response(&stream, 9, limit, 1) : receive_body(&stream, 9, limit);
}
static int transfer(const char *response, size_t split, uint32_t limit) {
    return transfer_include(response, split, limit, 0);
}
static uint32_t request_sent,request_calls;
static char expected_request[CURL_REQUEST_CAPACITY];
static int send_request(void *ctx,const uint8_t *data,uint32_t n) {
    (void)ctx; assert(n && n<=X86OS_TCP_MAX_SEGMENT);
    if(n>17) n=17; /* Exercise short writes as well as segment boundaries. */
    assert(!memcmp(data,expected_request+request_sent,n)); request_sent+=n; ++request_calls; return (int)n;
}
int main(void) {
    static char large[90000+64];
    const char prefix[]="HTTP/1.1 200 OK\r\nContent-Length: 90000\r\n\r\n";
    memcpy(large,prefix,sizeof(prefix)-1);
    memset(large+sizeof(prefix)-1,'x',90000);
    assert(!transfer(large,1460,90000));
    printf("CURL_FILE_WRITES bytes=%zu calls=%u\n",output_length,write_calls); fflush(stdout);
    assert(output_length==90000 && write_calls==1);
    for(size_t i=0;i<90000;++i) assert(output_bytes[i]=='x');
    write_limit=1700;
    assert(!transfer(large,1460,90000) && output_length==90000 && write_calls==53);
    write_limit=0;
    static char multi[270000+64];
    const char multi_prefix[]="HTTP/1.1 200 OK\r\nContent-Length: 270000\r\n\r\n";
    memcpy(multi,multi_prefix,sizeof(multi_prefix)-1);
    memset(multi+sizeof(multi_prefix)-1,'y',270000);
    assert(!transfer(multi,1460,270000) && write_calls==3 && output_length==270000);
    for(size_t i=0;i<output_length;++i) assert(output_bytes[i]=='y');
    fail_write_at=3;
    assert(transfer(multi,1460,270000)==-28 && file_buffer.failed==-28);
    assert(output_length==2*CURL_FILE_BUFFER_CAPACITY && !file_buffer.used && !file_buffer.enabled);
    fail_write_at=0;
    assert(!transfer(large,1460,90000) && output_length==90000 && !file_buffer.failed);
    assert(transfer("HTTP/1.1 200 OK\r\nContent-Length: 9\r\n\r\nshort",1,20)<0 && !output_length);
    /* stdout must publish as the stream arrives, without a file-sized delay. */
    sink_fd=X86OS_STDOUT_FILENO;
    wire=(const unsigned char *)large; wire_length=strlen(large); wire_at=output_length=clock_ms=write_calls=0;
    fragment=1460;
    curl_stream_t stdout_stream={0,0,receive};
    assert(!receive_body(&stdout_stream,sink_fd,90000) && output_length==90000 && write_calls>1);
    sink_fd=9;
    /* The private browser mode never writes response bytes to a file/stdout.
     * Framing failure must prevent publication; late peer loss stays an error. */
    static uint8_t staging[100000]; ipc_output.bytes=staging; ipc_output.capacity=sizeof(staging); ipc_output.used=0;
    assert(!transfer(large,1460,90000) && !output_length && !write_calls);
    ipc_not_delegated=2;
    assert(!publish_ipc_output(42) && ipc_used==90000 && ipc_total==90000);
    for(unsigned i=0;i<ipc_used;++i) assert(ipc_bytes[i]=='x');
    ipc_calls=ipc_used=ipc_total=0; ipc_fail_at=2;
    assert(publish_ipc_output(42)==-32 && ipc_used==REIST_CURL_IPC_DATA);
    ipc_fail_at=0; ipc_output.bytes=0; ipc_output.used=ipc_output.capacity=0;
    reist_curl_ipc_packet_t invalid={REIST_CURL_IPC_MAGIC,42,0,5,{1}};
    for(unsigned which=0;which<6;++which) {
        reist_curl_ipc_packet_t p=invalid; uint32_t used=0,total=0;
        uint8_t target[5]={9,9,9,9,9};
        if(which==0) ++p.magic; if(which==1) ++p.endpoint; if(which==2) ++p.offset;
        if(which==3) p.total=6; if(which==4) p.total=0; if(which==5) total=4;
        assert(reist_curl_ipc_accept(&p,21,42,target,5,&used,&total)==-84 && !used && target[0]==9);
    }
    char *ipc_argv[]={"curl","--reist-ipc","42","http://example.test/"}; curl_options_t ipc_options;
    assert(!parse_options(4,ipc_argv,&ipc_options) && ipc_options.ipc_endpoint==42);
    ipc_argv[2]="0"; assert(parse_options(4,ipc_argv,&ipc_options)<0);
    static reist_curl_url_t long_url; memset(long_url.path,'x',8000); long_url.path[0]='/'; long_url.path[8000]=0;
    strcpy(long_url.host,"example.test"); long_url.port=80; long_url.scheme=REIST_CURL_SCHEME_HTTP;
    uint32_t request_size=0; assert(!build_request(&long_url,expected_request,&request_size));
    curl_stream_t request_stream={0,send_request,0};
    assert(!send_all(&request_stream,(const uint8_t *)expected_request,request_size));
    assert(request_sent==request_size && request_size>8000 && request_calls>6);
    assert(curl_exit_status(CURL_STAGE_TCP,-5)==7);
    assert(curl_exit_status(CURL_STAGE_TCP,-110)==28);
    assert(curl_exit_status(CURL_STAGE_TLS,-13)==35);
    assert(curl_exit_status(CURL_STAGE_RESPONSE,-90)==63);
    assert(curl_exit_status(CURL_STAGE_OUTPUT,-5)==23);
    static const char response[] = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n"
        "Content-Type: text/html; charset=UTF-8\r\n\r\n"
        "2;id=test\r\nhe\r\n3\r\nllo\r\n0\r\nX-Trace: ok\r\n\r\n";
    for (size_t split = 1; split <= sizeof(response); ++split) {
        int result = transfer(response, split, 5);
        if (result) printf("CHUNKED split=%zu result=%d\n", split, result);
        fflush(stdout);
        assert(result == 0 && output_length == 5 && !memcmp(output_bytes, "hello", 5));
    }
    assert(transfer(response, 1, 4) == -90);
    assert(transfer("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
                    "2\r\nx\r\n0\r\n\r\n", 1, 50) < 0);
    assert(transfer("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
                    "FFFFFFFFFFFFFFFFF\r\n", 3, 50) < 0);
    assert(transfer("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
                    "1\r\nx\r\n0\r\nContent-Length: 1\r\n\r\n", 1, 50) < 0);
    assert(transfer("HTTP/1.0 200 OK\r\nContent-Length: 5\r\n\r\nhello", 1, 5) == 0);
    assert(transfer("HTTP/1.0 200 OK\r\n\r\nhello", 1, 5) == 0);
    assert(transfer("HTTP/1.0 200 OK\r\n\r\nhello!", 1, 5) == -90);
    assert(transfer("HTTP/1.0 200 OK\r\nContent-Length: 6\r\n\r\nhello", 1, 20) < 0);
    static const char interim[] = "HTTP/1.1 103 Early Hints\r\nLink: </style.css>\r\n\r\n"
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n";
    static const char included[] = "HTTP/1.1 103 Early Hints\r\nLink: </style.css>\r\n\r\n"
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
        "1 \t; x = \"a;\\\"b\";flag\r\nx\r\n0\r\n\r\n";
    for (size_t split = 1; split <= sizeof(included); ++split) {
        assert(transfer_include(included, split, 1, 1) == 0);
        assert(output_length == sizeof(interim) && !memcmp(output_bytes, interim, sizeof(interim)-1));
        assert(output_bytes[output_length-1] == 'x');
    }
    const char *bad[] = {
        "HTTP/1.1 101 Switching Protocols\r\n\r\n",
        "HTTP/1.1 204 No Content\r\nContent-Length: 0\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n1;\r\nx\r\n0\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n1;a=\"x\r\nx\r\n0\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nContent-Length: 1\r\n\r\n1\r\nx\r\n0\r\n\r\n"
    };
    for (size_t i=0; i<sizeof(bad)/sizeof(bad[0]); ++i) assert(transfer(bad[i],1,50)<0);
    assert(transfer("HTTP/1.1 204 No Content\r\n\r\n",1,1)==0 && !output_length);
    char *argv[] = {"curl", "--include", "-o", "/tmp/result", "http://example.test/?q=1"};
    curl_options_t options;
    assert(parse_options(5,argv,&options)==0 && options.include_headers);
    argv[1] = "-i"; assert(parse_options(5,argv,&options)==0 && options.include_headers);
    reist_curl_url_t url; char request[CURL_REQUEST_CAPACITY]; uint32_t used;
    assert(reist_curl_parse_http_url(options.url,&url)==0 && build_request(&url,request,&used)==0);
    assert(strstr(request,"GET /?q=1 HTTP/1.1\r\nHost: example.test\r\n"));
    assert(strstr(request,"Accept-Encoding: identity\r\n"));
    for (unsigned fail=0; fail<3; ++fail) {
        closed=renamed=removed=0; close_error=fail==1 ? -5 : 0; rename_error=fail==2 ? -28 : 0;
        int result=finish_output("/tmp/result.curl-part",options,9,0);
        assert((result==0)==(fail==0) && closed==1 && renamed==(fail!=1) && removed==(fail!=0));
    }
    closed=renamed=removed=0; close_error=rename_error=0;
    assert(finish_output("/tmp/result.curl-part",options,9,-84)==-84 && closed==1 && !renamed && removed==1);
    write_error=-28; assert(transfer(response,1,5)<0);
    write_error=10; assert(transfer(response,1,5)<0); write_error=0; /* Oversized OS reply. */
    curl_body_reader_t reader={0}; curl_stream_t stream={0,0,receive}; reader.stream=&stream;
    reader.started=reader.progressed=0; clock_ms=CURL_TRANSFER_IDLE_TIMEOUT_MS;
    assert(reader_available(&reader)==-110);
    reader.progressed=CURL_TRANSFER_HARD_TIMEOUT_MS-1; clock_ms=CURL_TRANSFER_HARD_TIMEOUT_MS;
    assert(reader_available(&reader)==-110);
    reader.started=reader.progressed=100; clock_ms=1; assert(reader_available(&reader)==-5);
    puts("CURL_STREAM_HOST_OK"); return 0;
}
