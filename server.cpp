#include <stdio.h>
#include <assert.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netinet/ip.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <poll.h>
#include <stdbool.h>
#include <vector>
#include <fcntl.h>
#include <map>

struct Buffer {
  uint8_t* buf_start;
  uint8_t* read_start;
  uint8_t* read_end;
  uint8_t* buf_end;
};

struct Conn {
  int fd = -1;

  bool want_read = false;
  bool want_write = false;
  bool want_close = false;

  struct Buffer* incoming;
  struct Buffer* outgoing;
};

struct Response {
  uint32_t status = 0;
  std::vector<uint8_t> data;
};

enum {
    RES_OK = 0,
    RES_ERR = 1,
    RES_NX = 2,
};

static std::map<std::string, std::string> database;

static struct Buffer* create_buf(const size_t size = 32) {
  struct Buffer *buf = new Buffer(); //struct allocation
  uint8_t *mem = new uint8_t[size]; //storage allocation : dellocate both carefully

  buf->buf_start = mem;
  buf->read_start = mem;
  buf->read_end = mem;
  buf->buf_end = mem+size;
  
  return buf;
}

static size_t buf_size(struct Buffer *buf) {
  return (size_t)(buf->read_end - buf->read_start);
}

static void buf_delete(struct Buffer *buf) {
  delete[] buf->buf_start;
  delete buf;
}

static Buffer* buf_push(struct Buffer *buf, uint8_t *data, size_t len) {
  uint32_t cap = buf->buf_end - buf->read_start;
  uint32_t buf_length = buf_size(buf);
  uint32_t new_len = buf_length + len;
  if(new_len >= cap) {
    while(new_len >= cap) {
      cap *= 2;
    }
    struct Buffer* grown = create_buf(cap);
    memcpy(grown->read_end, buf->read_start, buf_length);
    grown->read_end = grown->read_end + buf_length;

    //delete[] buf->buf_start;
    //delete buf;
    buf_delete(buf);
    buf = grown;
  }

  memcpy(buf->read_end, data, len);
  buf->read_end = buf->read_end + len;
  return buf;
}

static Buffer* buf_pop(struct Buffer *buf, size_t len) {
  buf->read_start += len;

  uint32_t read_bytes = buf_size(buf);

  if((size_t)(buf->read_start - buf->buf_start) > (size_t)(buf->buf_end - buf->buf_start)/2) {
    memmove(buf->buf_start, buf->read_start, read_bytes);
    buf->read_start = buf->buf_start;
    buf->read_end = buf->read_start + read_bytes;
  }

  return buf;
}

const size_t k_max_msg = 32 << 20;//4096;

static void msg(const char* msg) {
  fprintf(stderr, "%s\n", msg);
}

static void msg_errno(const char* msg) {
  fprintf(stderr, "[errno:%d] %s\n", errno, msg);
}

static void die(int fd, const char* msg) {
  int err = errno;
  close(fd);
  fprintf(stderr, "[%d] %s\n", err, msg);
  abort();
}

static void set_fd_non_blocking(int fd) {
  errno = 0;
  int flags = fcntl(fd, F_GETFL, 0);
  if(flags < 0) {
    die(fd, "fcntl() get flags error");
    return;
  }

  flags |= O_NONBLOCK;

  errno = 0;
  (void)fcntl(fd, F_SETFL, flags);
  if(errno) {
    die(fd, "fcntl() set flags error");
  }
}

static bool read_u32(const uint8_t *&data, const uint8_t *end, uint32_t &out) {
  if(data+4 > end) return false;
  memcpy(&out, data, 4);
  data+=4;
  return true;
}

static bool read_str(const uint8_t *&data, const uint8_t *end, uint32_t len, std::string &out) {
  if(data+len > end) {
    printf("data+len > end in read+str\n");
    return false;
  }
  out.assign(data, data+len);
  data+=len;
  return true;
}

static int32_t parse_req(const uint8_t* data, size_t size, std::vector<std::string>& out) { 
  const uint8_t* end = data+size;
  uint32_t nstr = 0;
  if(!read_u32(data, end, nstr)) return -1;
  if(nstr > k_max_msg) return -1;
  
  while(out.size() < nstr) {
    uint32_t len = 0;
    if(!read_u32(data, end, len)) return -1;
    
    out.push_back(std::string());
    if(!read_str(data, end, len, out.back())) return -1;

  }
  if(data != end) return -1;

  return 0;
}

static void do_request(std::vector<std::string> &cmd, struct Response &resp) {
  if(cmd.size() == 2 && cmd[0] == "get") {
    if(database.find(cmd[1]) != database.end()) {
      resp.data.assign(database[cmd[1]].begin(), database[cmd[1]].end());
    } else {
      resp.status = RES_NX;
    }
  }
  else if(cmd.size() == 3 && cmd[0] == "set") {
    database[cmd[1]] = cmd[2];
  }
  else if(cmd.size() == 2 && cmd[0] == "del") {
    database.erase(cmd[1]);
  }
  else {
    resp.status = RES_ERR;
  }
}

static void make_response(struct Response &resp, struct Buffer *&out) {
  uint32_t resp_len = 4 + resp.data.size();
  out = buf_push(out, (uint8_t*)&resp_len, 4);
  out = buf_push(out, (uint8_t*)&resp.status, 4);
  if(!resp.data.empty()) {
    out = buf_push(out, resp.data.data(), resp.data.size());
  }
}

static bool try_one_request(struct Conn *conn) {

  if(buf_size(conn->incoming) < 4) return false;
  uint32_t len = 0;
  memcpy(&len, conn->incoming->read_start, 4);

  if(len > k_max_msg || len < 4) {
    printf("req too large\n");
    conn->want_close = true;
    return false;
  }

  if(buf_size(conn->incoming) < len+4) {
    return false;
  }

  uint8_t *request = conn->incoming->read_start + 4;

  std::vector<std::string> cmd;
  if(parse_req(request, len, cmd) < 0) {
    printf("coudn't parse request\n");
    conn->want_close = true;
    return false;
  }

  struct Response resp;
  do_request(cmd, resp);
  make_response(resp, conn->outgoing);

  buf_pop(conn->incoming, 4+len);
  
  return true;
}


static void handle_write(struct Conn* conn) {
  size_t size = buf_size(conn->outgoing);
  assert(size > 0);

  int rv = write(conn->fd, conn->outgoing->read_start, size);
  if(rv < 0 && errno == EAGAIN) return;
  if(rv < 0) {
    conn->want_close = true;
    return;
  }

  conn->outgoing = buf_pop(conn->outgoing, (size_t)rv);
  size = buf_size(conn->outgoing);
  if(size == 0) {
    conn->want_read = true;
    conn->want_write = false;
  }
}

static void handle_read(struct Conn* conn) {
  uint8_t buf[64*1024];
  int rv = read(conn->fd, buf, (size_t)sizeof(buf));

  if(rv < 0 && errno == EAGAIN) return;
  if(rv < 0) {
    msg_errno("read() error");
    conn->want_close = true;
    return;
  }
  if(rv == 0) {
    if(buf_size(conn->incoming) == 0 && buf_size(conn->outgoing) == 0) {
      msg("client closed");
    } else {
      msg("unexpected EOF");
    }

    conn->want_close = true;
    return;
  }

  /*for(int i = 0; i < rv; i++) {
    printf("%02X", buf[i]);
  }
  printf("\n");
  */

  conn->incoming = buf_push(conn->incoming, buf, (size_t)rv);
  while(try_one_request(conn)) {}

  if(buf_size(conn->outgoing) > 0) {
    conn->want_read = false;
    conn->want_write = true;
    return handle_write(conn);
  }
}


static struct Conn* handle_accept(int fd) {
  struct sockaddr_in client_addr = {};
  socklen_t addrlen = sizeof(client_addr);
  int connfd = accept(fd, (sockaddr*)&client_addr, &addrlen);
  if(connfd < 0) {
    msg_errno("accept()");
    return nullptr;
  }

  uint32_t ip = client_addr.sin_addr.s_addr;
  fprintf(stderr, "new client from %u.%u.%u.%u:%u\n",
      ip&255, (ip>>8)&255, (ip>>16)&255, (ip>>24)&255, ntohs(client_addr.sin_port)
  );

  set_fd_non_blocking(connfd);
  
  Conn* client_conn = new Conn();
  client_conn->fd = connfd;
  client_conn->want_read = true;
  client_conn->incoming = create_buf();
  client_conn->outgoing = create_buf();

  return client_conn;
}

static void handle_close(struct Conn* conn) {
  buf_delete(conn->outgoing);
  buf_delete(conn->incoming);
  (void)close(conn->fd);
  delete conn;
}

int main() {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  int val = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(1234);
  addr.sin_addr.s_addr = htonl(0);

  int rv = bind(fd, (const struct sockaddr*)&addr, sizeof(addr));
  if(rv) {die(fd, "bind()");}
  rv = listen(fd, SOMAXCONN);
  if(rv) {die(fd,"listen()");}
  
  std::vector<struct Conn*> fd2conn;
  std::vector<struct pollfd> poll_args;

  while(true) {
    poll_args.clear();
    
    struct pollfd pfd = {fd, POLLIN, 0};
    poll_args.push_back(pfd);

    for(struct Conn *conn: fd2conn) {
      if(!conn) {
        continue;
      }
      struct pollfd pfd = {conn->fd, POLLERR, 0};

      if(conn->want_read) pfd.events|= POLLIN;
      if(conn->want_write) pfd.events|= POLLOUT;

      poll_args.push_back(pfd);
    }

    int rv = poll(poll_args.data(), (nfds_t)poll_args.size(), -1);
    if(rv < 0 && errno == EINTR) {
      continue;
    }
    if(rv < 0) {
      die(fd, "poll()");
    }

    //handling listening events
    if(poll_args[0].revents) {
      if(struct Conn* conn = handle_accept(fd)) {
        if(conn->fd >= fd2conn.size()) {
          fd2conn.resize(conn->fd+1);
        }
        fd2conn[conn->fd] = conn;
      }
    }

    //handling socket events
    for(size_t i = 1; i < poll_args.size(); ++i) {
      struct pollfd pfd = poll_args[i];
      if(pfd.revents == 0) continue;
      struct Conn* conn = fd2conn[pfd.fd];

      if((pfd.revents & POLLERR) || conn->want_close) {
        fd2conn[conn->fd] = NULL;
        handle_close(conn);
        continue;
      }

      if(pfd.revents & POLLIN) {
        assert(conn->want_read == true);
        handle_read(conn);
      }
	  
	    if(pfd.revents & POLLOUT) {
		    assert(conn->want_write == true);
		    handle_write(conn);
	    }
    }
  }
  
  return 0;
}
