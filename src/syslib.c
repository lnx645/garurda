// ============================================================
// syslib.c — freestanding system library for GarudaLang
//
// Compiled with: gcc -ffreestanding -nostdlib -c src/syslib.c
// Linked statically into every compiled program:
//
//     ld -o prog prog.o syslib.o
//
// Only raw Linux x86-64 syscalls are used; no libc is linked.
// Provides: bump allocator, fs, sys (env/time/sleep),
// TCP+DNS, HTTP client, HTTP one-shot server, SMTP client,
// serial/USB-serial ports.
//
// Author: GalangTeam
// ============================================================

// ---- freestanding-safe headers only ----
typedef unsigned long  u64;
typedef long           s64;
typedef unsigned int   u32;
typedef int            s32;
typedef unsigned short u16;
typedef unsigned char  u8;

// envp captured by _start (see codegen.c)
char** ga_envp;

// ============================================================
// Raw syscalls
// ============================================================
static long sc6(long n, long a, long b, long c, long d, long e, long f) {
    long ret;
    register long r10 __asm__("r10") = d;
    register long r8  __asm__("r8")  = e;
    register long r9  __asm__("r9")  = f;
    __asm__ volatile ("syscall"
                      : "=a"(ret)
                      : "a"(n), "D"(a), "S"(b), "d"(c),
                        "r"(r10), "r"(r8), "r"(r9)
                      : "rcx", "r11", "memory");
    return ret;
}
static long sc3(long n, long a, long b, long c) { return sc6(n,a,b,c,0,0,0); }
static long sc2(long n, long a, long b)         { return sc6(n,a,b,0,0,0,0); }
static long sc1(long n, long a)                 { return sc6(n,a,0,0,0,0,0); }

#define SYS_read      0
#define SYS_write     1
#define SYS_open      2
#define SYS_close     3
#define SYS_brk       12
#define SYS_ioctl     16
#define SYS_nanosleep 35
#define SYS_socket    41
#define SYS_connect   42
#define SYS_accept    43
#define SYS_sendto    44
#define SYS_recvfrom  45
#define SYS_bind      49
#define SYS_listen    50
#define SYS_setsockopt 54
#define SYS_time      201

#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR   2
#define O_CREAT  0100
#define O_TRUNC  01000
#define AF_INET  2
#define SOCK_STREAM 1
#define SOCK_DGRAM  2
#define SOL_SOCKET 1
#define SO_RCVTIMEO 20
#define SO_REUSEADDR 2

// ============================================================
// Tiny libc replacements
// ============================================================
static u64 ga_strlen(const char* s) {
    u64 n = 0;
    while (s[n]) n++;
    return n;
}
static void ga_memcpy(char* d, const char* s, u64 n) {
    for (u64 i = 0; i < n; i++) d[i] = s[i];
}
static int ga_memcmp(const char* a, const char* b, u64 n) {
    for (u64 i = 0; i < n; i++)
        if (a[i] != b[i]) return (int)(u8)a[i] - (int)(u8)b[i];
    return 0;
}
static int ga_strcmp(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(u8)*a - (int)(u8)*b;
}
static char* ga_strcpy(char* d, const char* s) {
    char* p = d;
    while ((*p++ = *s++));
    return d;
}
// find needle in haystack (both with explicit lengths)
static long ga_find(const char* h, u64 hlen, const char* n, u64 nlen) {
    if (nlen == 0 || nlen > hlen) return -1;
    for (u64 i = 0; i + nlen <= hlen; i++)
        if (ga_memcmp(h + i, n, nlen) == 0) return (long)i;
    return -1;
}

// ============================================================
// Bump allocator (brk)
// ============================================================
static char* brk_cur = 0;
static void* ga_alloc(u64 size) {
    if (!brk_cur)
        brk_cur = (char*)sc1(SYS_brk, 0);
    size = (size + 15) & ~15UL;
    char* p = brk_cur;
    char* n = (char*)sc1(SYS_brk, (long)(p + size));
    if (n < p + size) return 0;   // out of memory
    brk_cur = p + size;
    return p;
}
static char* ga_dup(const char* s, u64 len) {
    char* p = (char*)ga_alloc(len + 1);
    if (!p) return 0;
    ga_memcpy(p, s, len);
    p[len] = 0;
    return p;
}
static char* ga_dupz(const char* s) { return ga_dup(s, ga_strlen(s)); }

// ============================================================
// Sockets
// ============================================================
struct sockaddr_in {
    u16 sin_family;
    u16 sin_port;      // network byte order
    u32 sin_addr;      // network byte order
    u8  sin_zero[8];
};
struct timeval { long tv_sec; long tv_usec; };

static u16 htons(u16 x) { return (u16)((x << 8) | (x >> 8)); }
static u32 htonl(u32 x) {
    return ((x & 0xff) << 24) | ((x & 0xff00) << 8) |
           ((x >> 8) & 0xff00) | ((x >> 24) & 0xff);
}
// dotted quad -> network-order u32, 0 on failure (note: 0.0.0.0 also 0)
static u32 parse_ipv4(const char* s) {
    u32 parts[4] = {0,0,0,0};
    int pi = 0;
    u32 cur = 0; int digits = 0;
    for (const char* p = s; ; p++) {
        char c = *p;
        if (c >= '0' && c <= '9') { cur = cur * 10 + (u32)(c - '0'); digits++; if (cur > 255) return 0; }
        else if (c == '.' || c == 0) {
            if (!digits || pi >= 4) return 0;
            parts[pi++] = cur; cur = 0; digits = 0;
            if (c == 0) break;
        } else return 0;
    }
    if (pi != 4) return 0;
    return htonl((parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3]);
}

// Minimal DNS: A record via 8.8.8.8 (2s timeout). Returns net-order u32, 0 on failure.
static u32 dns_resolve(const char* host) {
    u32 ip = parse_ipv4(host);
    if (ip) return ip;

    long sock = sc3(SYS_socket, AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return 0;
    struct timeval tv; tv.tv_sec = 2; tv.tv_usec = 0;
    sc6(SYS_setsockopt, sock, SOL_SOCKET, SO_RCVTIMEO, (long)&tv, sizeof(tv), 0);

    u8* q = (u8*)ga_alloc(512);
    if (!q) { sc1(SYS_close, sock); return 0; }
    q[0]=0x12; q[1]=0x34; q[2]=0x01; q[3]=0x00;
    q[4]=0; q[5]=1; q[6]=0; q[7]=0; q[8]=0; q[9]=0; q[10]=0; q[11]=0;
    int qi = 12;
    const char* p = host;
    while (*p) {
        const char* dot = p;
        while (*dot && *dot != '.') dot++;
        int len = (int)(dot - p);
        if (len == 0 || len > 63 || qi + len + 1 >= 500) { sc1(SYS_close, sock); return 0; }
        q[qi++] = (u8)len;
        for (int i = 0; i < len; i++) q[qi++] = (u8)p[i];
        p = *dot ? dot + 1 : dot;
    }
    q[qi++]=0; q[qi++]=0; q[qi++]=1; q[qi++]=0; q[qi++]=1; // QTYPE=A QCLASS=IN

    struct sockaddr_in dst;
    dst.sin_family = AF_INET; dst.sin_port = htons(53);
    dst.sin_addr = htonl(0x08080808u); // 8.8.8.8
    for (int i = 0; i < 8; i++) dst.sin_zero[i] = 0;
    if (sc6(SYS_sendto, sock, (long)q, qi, 0, (long)&dst, sizeof(dst)) < 0) {
        sc1(SYS_close, sock); return 0;
    }
    u8* r = (u8*)ga_alloc(512);
    long n = sc6(SYS_recvfrom, sock, (long)r, 512, 0, 0, 0);
    sc1(SYS_close, sock);
    if (n < 12) return 0;
    int ancount = (r[6] << 8) | r[7];
    if (!ancount) return 0;
    int i = 12;
    // skip question section
    while (i < n && r[i]) i += r[i] + 1;
    i += 5; // null + qtype + qclass
    // walk answers, take first A record
    for (int a = 0; a < ancount && i + 10 < n; a++) {
        int namelen = (r[i] & 0xC0) == 0xC0 ? 2 : 0;
        if (!namelen) { // uncompressed name: skip labels
            while (i < n && r[i]) i += r[i] + 1;
            i += 1;
        } else i += 2;
        if (i + 10 > n) break;
        int type = (r[i] << 8) | r[i+1];
        int rdlen = (r[i+8] << 8) | r[i+9];
        i += 10;
        if (type == 1 && rdlen == 4 && i + 4 <= n) {
            u32 addr = ((u32)r[i] << 24) | ((u32)r[i+1] << 16) |
                       ((u32)r[i+2] << 8) | (u32)r[i+3];
            return htonl(addr);
        }
        i += rdlen;
    }
    return 0;
}

// TCP connect to host:port. Returns fd or -1.
static long tcp_connect(const char* host, int port) {
    u32 ip = dns_resolve(host);
    if (!ip && ga_strcmp(host, "0.0.0.0") != 0) return -1;
    long sock = sc3(SYS_socket, AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;
    struct sockaddr_in a;
    a.sin_family = AF_INET; a.sin_port = htons((u16)port); a.sin_addr = ip;
    for (int i = 0; i < 8; i++) a.sin_zero[i] = 0;
    if (sc3(SYS_connect, sock, (long)&a, sizeof(a)) < 0) {
        sc1(SYS_close, sock); return -1;
    }
    return sock;
}
static long tcp_send_all(long fd, const char* buf, u64 len) {
    u64 sent = 0;
    while (sent < len) {
        long n = sc3(SYS_write, fd, (long)(buf + sent), (long)(len - sent));
        if (n <= 0) return -1;
        sent += (u64)n;
    }
    return (long)sent;
}
// read until EOF or max; returns malloc'd buffer (NUL-terminated), *out_len set
static char* tcp_recv_all(long fd, u64* out_len) {
    u64 cap = 8192, len = 0;
    char* buf = (char*)ga_alloc(cap);
    if (!buf) return 0;
    for (;;) {
        if (len == cap) break;
        long n = sc3(SYS_read, fd, (long)(buf + len), (long)(cap - len));
        if (n <= 0) break;
        len += (u64)n;
    }
    buf[len] = 0;
    if (out_len) *out_len = len;
    return buf;
}

// ============================================================
// Filesystem
// ============================================================
char* ga_fs_read(const char* path) {
    long fd = sc3(SYS_open, (long)path, O_RDONLY, 0);
    if (fd < 0) return ga_dup("", 0);
    u64 cap = 8192, len = 0;
    char* buf = (char*)ga_alloc(cap);
    if (!buf) { sc1(SYS_close, fd); return ga_dup("", 0); }
    for (;;) {
        if (len + 4096 > cap) { // grow by copying (bump allocator has no realloc)
            u64 ncap = cap * 2;
            char* nb = (char*)ga_alloc(ncap);
            if (!nb) break;
            ga_memcpy(nb, buf, len);
            buf = nb; cap = ncap;
        }
        long n = sc3(SYS_read, fd, (long)(buf + len), 4096);
        if (n <= 0) break;
        len += (u64)n;
    }
    sc1(SYS_close, fd);
    buf[len] = 0;
    return buf;
}
s64 ga_fs_write(const char* path, const char* data) {
    long fd = sc3(SYS_open, (long)path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;
    u64 len = ga_strlen(data);
    long w = tcp_send_all(fd, data, len);
    sc1(SYS_close, fd);
    return w < 0 ? -1 : (s64)w;
}

// ============================================================
// sys: env / time / sleep
// ============================================================
char* ga_sys_env(const char* name) {
    if (!ga_envp) return ga_dup("", 0);
    u64 nl = ga_strlen(name);
    for (char** e = ga_envp; *e; e++) {
        if (ga_memcmp(*e, name, nl) == 0 && (*e)[nl] == '=')
            return ga_dupz(*e + nl + 1);
    }
    return ga_dup("", 0);
}
s64 ga_sys_time(void) {
    return (s64)sc2(SYS_time, 0, 0);
}
void ga_sys_sleep(s64 ms) {
    if (ms <= 0) return;
    struct { long tv_sec; long tv_nsec; } ts;
    ts.tv_sec = ms / 1000; ts.tv_nsec = (ms % 1000) * 1000000L;
    sc2(SYS_nanosleep, (long)&ts, 0);
}

// ============================================================
// HTTP client (http:// only, no TLS)
// ============================================================
static s64 ga_http_last_code = 0;
s64 ga_http_code(void) { return ga_http_last_code; }

// parse "http://host[:port][/path]"; returns 0 ok. out_host/out_path malloc'd.
static int parse_url(const char* url, char** out_host, int* out_port, char** out_path) {
    const char* p = url;
    if (ga_memcmp(p, "http://", 7) == 0) p += 7;
    else return -1; // only http:// supported (no TLS in syslib)
    const char* slash = p;
    while (*slash && *slash != '/') slash++;
    const char* colon = p;
    while (colon < slash && *colon != ':') colon++;
    u64 hostlen = (u64)(colon < slash ? colon - p : slash - p);
    *out_host = ga_dup(p, hostlen);
    *out_port = 80;
    if (colon < slash) {
        int port = 0;
        for (const char* q = colon + 1; q < slash; q++) {
            if (*q < '0' || *q > '9') return -1;
            port = port * 10 + (*q - '0');
        }
        *out_port = port;
    }
    *out_path = ga_dupz(*slash ? slash : "/");
    return (*out_host && *out_path) ? 0 : -1;
}
static void u64_to_str(u64 v, char* out) {
    char tmp[24]; int i = 0;
    if (!v) tmp[i++] = '0';
    while (v) { tmp[i++] = (char)('0' + v % 10); v /= 10; }
    int j = 0;
    while (i--) out[j++] = tmp[i];
    out[j] = 0;
}
// dechunk; returns new malloc'd buffer, sets *out_len
static char* dechunk(const char* src, u64 srclen, u64* out_len) {
    u64 cap = srclen + 1, len = 0;
    char* out = (char*)ga_alloc(cap);
    if (!out) return 0;
    u64 i = 0;
    while (i < srclen) {
        u64 chunk = 0;
        while (i < srclen && src[i] != '\r' && src[i] != '\n') {
            char c = src[i++];
            chunk = chunk * 16 + (u64)(c <= '9' ? c - '0' : (c & 0x5F) - 'A' + 10);
        }
        while (i < srclen && (src[i] == '\r' || src[i] == '\n')) i++;
        if (!chunk) break;
        if (i + chunk > srclen) chunk = srclen - i;
        ga_memcpy(out + len, src + i, chunk);
        len += chunk; i += chunk;
        while (i < srclen && (src[i] == '\r' || src[i] == '\n')) i++;
    }
    out[len] = 0;
    if (out_len) *out_len = len;
    return out;
}
// core: method GET/POST, returns malloc'd body ("" on error)
static char* http_do(const char* method, const char* url, const char* body, u64 bodylen) {
    ga_http_last_code = 0;
    char* host; int port; char* path;
    if (parse_url(url, &host, &port, &path) != 0) return ga_dup("", 0);
    long fd = tcp_connect(host, port);
    if (fd < 0) return ga_dup("", 0);

    char* req = (char*)ga_alloc(4096 + bodylen);
    if (!req) { sc1(SYS_close, fd); return ga_dup("", 0); }
    char* w = req;
    w += 0; // build request
    // METHOD path HTTP/1.1\r\nHost: host\r\nConnection: close\r\n
    for (const char* q = method; *q; q++) *w++ = *q;
    *w++ = ' '; for (const char* q = path; *q; q++) *w++ = *q;
    const char* h1 = " HTTP/1.1\r\nHost: ";
    for (const char* q = h1; *q; q++) *w++ = *q;
    for (const char* q = host; *q; q++) *w++ = *q;
    const char* h2 = "\r\nConnection: close\r\n";
    for (const char* q = h2; *q; q++) *w++ = *q;
    if (body && bodylen) {
        const char* h3 = "Content-Length: ";
        for (const char* q = h3; *q; q++) *w++ = *q;
        char num[24]; u64_to_str(bodylen, num);
        for (const char* q = num; *q; q++) *w++ = *q;
        *w++ = '\r'; *w++ = '\n';
    }
    *w++ = '\r'; *w++ = '\n';
    u64 reqlen = (u64)(w - req);
    if (tcp_send_all(fd, req, reqlen) < 0) { sc1(SYS_close, fd); return ga_dup("", 0); }
    if (body && bodylen)
        if (tcp_send_all(fd, body, bodylen) < 0) { sc1(SYS_close, fd); return ga_dup("", 0); }

    u64 resplen;
    char* resp = tcp_recv_all(fd, &resplen);
    sc1(SYS_close, fd);
    if (!resp || !resplen) return ga_dup("", 0);

    // status line: HTTP/1.x CODE
    long hdr_end = ga_find(resp, resplen, "\r\n\r\n", 4);
    if (hdr_end < 0) return ga_dup("", 0);
    int code = 0;
    const char* sp = resp;
    while (*sp && *sp != ' ') sp++;
    if (*sp == ' ') { sp++; while (*sp == ' ') sp++; while (*sp >= '0' && *sp <= '9') code = code * 10 + (*sp++ - '0'); }
    ga_http_last_code = code;

    const char* body_start = resp + hdr_end + 4;
    u64 body_len = resplen - (u64)(hdr_end + 4);
    // chunked?
    if (ga_find(resp, (u64)hdr_end, "chunked", 7) >= 0 ||
        ga_find(resp, (u64)hdr_end, "Chunked", 7) >= 0) {
        u64 dl;
        char* d = dechunk(body_start, body_len, &dl);
        return d ? d : ga_dup("", 0);
    }
    return ga_dup(body_start, body_len);
}
char* ga_http_get(const char* url) { return http_do("GET", url, 0, 0); }
char* ga_http_post(const char* url, const char* body) {
    return http_do("POST", url, body, ga_strlen(body));
}

// ============================================================
// HTTP one-shot server: accept 1 connection, return request,
// reply with `response`. Blocking.
// ============================================================
char* ga_http_serve_once(s64 port, const char* response) {
    long srv = sc3(SYS_socket, AF_INET, SOCK_STREAM, 0);
    if (srv < 0) return ga_dup("", 0);
    int one = 1;
    sc6(SYS_setsockopt, srv, SOL_SOCKET, SO_REUSEADDR, (long)&one, 4, 0);
    struct sockaddr_in a;
    a.sin_family = AF_INET; a.sin_port = htons((u16)port); a.sin_addr = htonl(0);
    for (int i = 0; i < 8; i++) a.sin_zero[i] = 0;
    if (sc3(SYS_bind, srv, (long)&a, sizeof(a)) < 0 ||
        sc2(SYS_listen, srv, 1) < 0) {
        sc1(SYS_close, srv); return ga_dup("", 0);
    }
    long fd = sc3(SYS_accept, srv, 0, 0);
    sc1(SYS_close, srv);
    if (fd < 0) return ga_dup("", 0);
    // read request headers (up to 16KB)
    u64 cap = 16384, len = 0;
    char* req = (char*)ga_alloc(cap);
    if (req) {
        while (len < cap - 1) {
            long n = sc3(SYS_read, fd, (long)(req + len), 4096);
            if (n <= 0) break;
            len += (u64)n;
            req[len] = 0;
            if (ga_find(req, len, "\r\n\r\n", 4) >= 0) break;
        }
        req[len] = 0;
    }
    u64 rlen = ga_strlen(response);
    tcp_send_all(fd, response, rlen);
    sc1(SYS_close, fd);
    return req ? req : ga_dup("", 0);
}

// ============================================================
// SMTP client (plain TCP + AUTH LOGIN; use stunnel for TLS)
// ============================================================
static int smtp_expect(long fd, const char* code) {
    char buf[1024];
    u64 len = 0;
    // Read lines until one ends with \r\n and has ' ' as 4th char
    // (SMTP multi-line replies use "250-..." continuation).
    for (;;) {
        u64 line_start = len;
        int got_line = 0;
        while (len < sizeof(buf) - 1) {
            long n = sc3(SYS_read, fd, (long)(buf + len), 1);
            if (n <= 0) return 0;
            len += (u64)n;
            if (len >= 2 && buf[len-2] == '\r' && buf[len-1] == '\n') {
                got_line = 1;
                break;
            }
        }
        if (!got_line) return 0;
        buf[len] = 0;
        // last line of reply? "XYZ " (space after code)
        if (len - line_start >= 4 && buf[line_start+3] == ' ')
            return ga_memcmp(buf + line_start, code, 3) == 0;
        // else continuation line "XYZ-": keep reading
    }
}
static int smtp_cmd(long fd, const char* cmd, const char* expect) {
    if (tcp_send_all(fd, cmd, ga_strlen(cmd)) < 0) return 0;
    return smtp_expect(fd, expect);
}
// base64 encode (for AUTH LOGIN)
static void b64_enc(const char* in, u64 len, char* out) {
    static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    u64 i = 0, j = 0;
    while (i < len) {
        u32 a = i < len ? (u8)in[i++] : 0;
        u32 b = i < len ? (u8)in[i++] : 0;
        u32 c = i < len ? (u8)in[i++] : 0;
        u32 n = (a << 16) | (b << 8) | c;
        out[j++] = T[(n >> 18) & 63]; out[j++] = T[(n >> 12) & 63];
        out[j++] = T[(n >> 6) & 63];  out[j++] = T[n & 63];
    }
    u64 mod = len % 3;
    if (mod) { out[j-1] = '='; if (mod == 1) out[j-2] = '='; }
    out[j] = 0;
}
s64 ga_smtp_connect(const char* host, s64 port) {
    long fd = tcp_connect(host, (int)port);
    if (fd < 0) return -1;
    if (!smtp_expect(fd, "220")) { sc1(SYS_close, fd); return -1; }
    return (s64)fd;
}
s64 ga_smtp_auth(s64 fd, const char* user, const char* pass) {
    char* cmd = (char*)ga_alloc(512);
    if (!cmd) return 0;
    ga_strcpy(cmd, "EHLO garurda\r\n");
    if (!smtp_cmd(fd, cmd, "250")) return 0;
    ga_strcpy(cmd, "AUTH LOGIN\r\n");
    if (!smtp_cmd(fd, cmd, "334")) return 0;
    char* b = (char*)ga_alloc(512);
    b64_enc(user, ga_strlen(user), b);
    ga_strcpy(cmd, b); ga_strcpy(cmd + ga_strlen(cmd), "\r\n");
    if (!smtp_cmd(fd, cmd, "334")) return 0;
    b64_enc(pass, ga_strlen(pass), b);
    ga_strcpy(cmd, b); ga_strcpy(cmd + ga_strlen(cmd), "\r\n");
    return smtp_cmd(fd, cmd, "235") ? 1 : 0;
}
s64 ga_smtp_mail(s64 fd, const char* from, const char* to,
                 const char* subject, const char* body) {
    char* cmd = (char*)ga_alloc(1024);
    if (!cmd) return 0;
    ga_strcpy(cmd, "MAIL FROM:<"); ga_strcpy(cmd + ga_strlen(cmd), from);
    ga_strcpy(cmd + ga_strlen(cmd), ">\r\n");
    if (!smtp_cmd(fd, cmd, "250")) return 0;
    ga_strcpy(cmd, "RCPT TO:<"); ga_strcpy(cmd + ga_strlen(cmd), to);
    ga_strcpy(cmd + ga_strlen(cmd), ">\r\n");
    if (!smtp_cmd(fd, cmd, "250") && !smtp_cmd(fd, cmd, "251")) return 0;
    ga_strcpy(cmd, "DATA\r\n");
    if (!smtp_cmd(fd, cmd, "354")) return 0;
    // headers + body + <CR><LF>.<CR><LF>
    u64 bl = ga_strlen(body);
    char* msg = (char*)ga_alloc(1024 + bl + 16);
    if (!msg) return 0;
    char* w = msg;
    const char* parts[] = {"From: ", from, "\r\nTo: ", to, "\r\nSubject: ", subject,
                           "\r\nContent-Type: text/plain; charset=utf-8\r\n\r\n", 0};
    for (int i = 0; parts[i]; i++) {
        ga_strcpy(w, parts[i]); w += ga_strlen(parts[i]);
    }
    ga_memcpy(w, body, bl); w += bl;
    ga_strcpy(w, "\r\n.\r\n");
    if (tcp_send_all(fd, msg, (u64)(w - msg) + 5) < 0) return 0;
    return smtp_expect(fd, "250") ? 1 : 0;
}
void ga_smtp_close(s64 fd) {
    tcp_send_all(fd, "QUIT\r\n", 6);
    sc1(SYS_close, fd);
}

// ============================================================
// Serial / USB-serial ports (termios via ioctl)
// ============================================================
struct termios_g {
    u32 c_iflag, c_oflag, c_cflag, c_lflag;
    u8  c_line;
    u8  c_cc[32];
    u32 c_ispeed, c_ospeed;
};
#define TCGETS 0x5401
#define TCSETS 0x5402
static u32 baud_const(s64 baud) {
    switch (baud) {
        case 9600:   return 13;   // B9600
        case 19200:  return 14;
        case 38400:  return 15;
        case 57600:  return 4097;
        case 115200: return 4098;
        default:     return 13;
    }
}
s64 ga_serial_open(const char* path, s64 baud) {
    long fd = sc3(SYS_open, (long)path, O_RDWR, 0);
    if (fd < 0) return -1;
    struct termios_g t;
    if (sc3(SYS_ioctl, fd, TCGETS, (long)&t) < 0) { sc1(SYS_close, fd); return -1; }
    u32 b = baud_const(baud);
    t.c_iflag = 0; t.c_oflag = 0; t.c_lflag = 0;   // raw mode
    t.c_cflag = b | 060 | 0200 | 040;               // baud | CREAD | CS8 | HUPCL-ish
    t.c_cflag &= ~0100000u;                          // ~PARENB
    t.c_cc[6] = 1;  // VMIN=1
    t.c_cc[5] = 0;  // VTIME=0
    // note: struct layout has ispeed/ospeed; set via c_cflag baud is enough on Linux
    (void)t.c_ispeed; (void)t.c_ospeed;
    if (sc3(SYS_ioctl, fd, TCSETS, (long)&t) < 0) { sc1(SYS_close, fd); return -1; }
    return (s64)fd;
}
char* ga_serial_read(s64 fd, s64 maxlen) {
    if (maxlen <= 0 || maxlen > 65536) maxlen = 1024;
    char* buf = (char*)ga_alloc((u64)maxlen + 1);
    if (!buf) return ga_dup("", 0);
    long n = sc3(SYS_read, fd, (long)buf, (long)maxlen);
    if (n < 0) n = 0;
    buf[n] = 0;
    return buf;
}
s64 ga_serial_write(s64 fd, const char* data) {
    long w = tcp_send_all(fd, data, ga_strlen(data));
    return w < 0 ? -1 : (s64)w;
}
void ga_serial_close(s64 fd) { sc1(SYS_close, fd); }
