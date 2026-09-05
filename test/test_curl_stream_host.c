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
static unsigned char output_bytes[65536];
static size_t output_length;
static uint64_t clock_ms;
static int write_error, close_error, rename_error;
static unsigned closed, renamed, removed;
int x86os_write(int fd, const void *bytes, size_t length) {
    assert(fd == 9 && length <= sizeof(output_bytes) - output_length);
    if (write_error) return write_error;
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
    wire_at = output_length = clock_ms = 0; fragment = split;
    curl_stream_t stream = {0, 0, receive};
    return include ? receive_response(&stream, 9, limit, 1) : receive_body(&stream, 9, limit);
}
static int transfer(const char *response, size_t split, uint32_t limit) {
    return transfer_include(response, split, limit, 0);
}
int main(void) {
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
