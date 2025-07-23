#include <stdio.h>
#include <stdint.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/ip.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <string>
#include <vector>

const size_t k_max_msg = 32 << 20;  // 32 MB

void die(const char *msg) {
    perror(msg);
    exit(1);
}

static int32_t read_full(int fd, char *buf, size_t n) {
    while (n > 0) {
        ssize_t rv = read(fd, buf, n);
        if (rv <= 0) return -1;
        assert((size_t)rv <= n);
        n -= rv;
        buf += rv;
    }
    return 0;
}

static int32_t write_all(int fd, const char *buf, size_t n) {
    while (n > 0) {
        ssize_t st = write(fd, buf, n);
        if (st <= 0) return -1;
        assert((size_t)st <= n);
        n -= st;
        buf += st;
    }
    return 0;
}

// ✅ Send DB command: ["set", "key", "value"], ["get", "key"], etc.
static int32_t send_req(int fd, const std::vector<std::string> &cmd) {
    uint32_t nstr = cmd.size();

    // Compute payload length: 4 for nstr + each (4 + len)
    uint32_t payload_len = 4;
    for (auto &s : cmd) {
        payload_len += 4 + s.size();
    }
    if (payload_len > k_max_msg) return -1;

    // total to send = 4 (for payload_len) + payload_len
    std::vector<char> wbuf(4 + payload_len);
    char *p = wbuf.data();

    // Write payload_len
    memcpy(p, &payload_len, 4);
    p += 4;

    // Write nstr
    memcpy(p, &nstr, 4);
    p += 4;

    // Write each [len + string]
    for (auto &s : cmd) {
        uint32_t slen = s.size();
        memcpy(p, &slen, 4);
        p += 4;
        memcpy(p, s.data(), slen);
        p += slen;
    }

    // Send
    return write_all(fd, wbuf.data(), wbuf.size());
}

// ✅ Read server response: [4 bytes resp_len][4 bytes status][data]
static int32_t read_res(int fd) {
    uint32_t len;
    if (read_full(fd, (char *)&len, 4)) {
        fprintf(stderr, "eof or read error\n");
        return -1;
    }
    if (len > k_max_msg) {
        fprintf(stderr, "response too long\n");
        return -1;
    }

    std::vector<char> buf(len);
    if (read_full(fd, buf.data(), len)) {
        fprintf(stderr, "read body failed\n");
        return -1;
    }

    // First 4 bytes = status
    if (len < 4) {
        fprintf(stderr, "bad response size\n");
        return -1;
    }

    uint32_t status;
    memcpy(&status, buf.data(), 4);
    std::string data(buf.data() + 4, buf.data() + len);

    printf("server status: %u\n", status);
    if (!data.empty()) {
        printf("server data: %s\n", data.c_str());
    } else {
        printf("server data: <empty>\n");
    }
    return 0;
}

int main() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) die("socket");

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1234);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (connect(fd, (const struct sockaddr *)&addr, sizeof(addr)) < 0) {
        die("connect");
    }

    // ✅ Define a list of DB commands
    std::vector<std::vector<std::string>> query_list = {
        {"set", "foo", "bar"},
        {"get", "foo"},
        {"set", "baz", "12345"},
        {"get", "baz"},
        {"del", "foo"},
        {"get", "foo"}
    };

    // 1. Send all requests
    for (auto &cmd : query_list) {
        if (send_req(fd, cmd)) {
            fprintf(stderr, "send_req failed\n");
            close(fd);
            return 1;
        }
    }

    // 2. Read all responses
    for (size_t i = 0; i < query_list.size(); i++) {
        if (read_res(fd)) {
            fprintf(stderr, "read_res failed\n");
            close(fd);
            return 1;
        }
    }

    close(fd);
    return 0;
}
