# GarudaLang — System Access (v0.3)

Builtin functions for filesystem, OS, network, email, and serial/USB ports.
Implemented in `src/syslib.c`, a freestanding library (raw Linux x86-64
syscalls, no libc) that is linked statically into every compiled program.

Design notes:

- **Strict typing still applies.** Every builtin has a fixed signature;
  wrong arity or argument types are compile-time errors.
- **Strings are NUL-terminated** (`char *`). String literals are emitted
  with `.asciz`.
- **Errors are values, not exceptions.** A failed operation returns an
  empty string, `-1`, or `0` — documented per builtin. (When `Result<T,E>`
  lands, these will migrate to it.)
- **Handles are `int`.** Sockets and serial ports are raw file
  descriptors. `-1` means failure; always check before use.

---

## 1. Filesystem

```ga
// read whole file -> string ("" if unreadable)
let data: string = fs_read("input.txt");

// write file (truncate) -> bytes written, -1 on error
let n: int = fs_write("output.txt", data);
```

## 2. OS / process

```ga
let home: string = sys_env("HOME");   // "" if unset
let now: int = sys_time();            // unix timestamp (seconds)
sys_sleep(500);                       // sleep 500 ms
```

`sys_env` reads the environment captured at process start (`_start`
saves `envp` for the runtime).

## 3. HTTP client (`http://` only)

```ga
let body: string = http_get("http://example.com/");
let code: int = http_code();              // status of last http_* call
let echo: string = http_post("http://api.example.com/submit", "a=1&b=2");
```

- URL form: `http://host[:port][/path]`. Default port 80, default path `/`.
- Handles `Content-Length` and `chunked` responses.
- `https://` is rejected (no TLS in syslib) — returns `""`, code `0`.
  For TLS, front with `stunnel` or a local proxy, same pattern as the
  `galang` smtp example.
- DNS: dotted quads skip resolution; hostnames resolve via a minimal
  DNS client (UDP to 8.8.8.8, 2 s timeout).

## 4. HTTP server (one-shot)

```ga
// Block until ONE connection arrives, reply with `response`,
// return the raw request. Useful for webhooks, callbacks, tests.
let req: string = http_serve_once(8869,
    "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok");
print(req);   // e.g. "GET /hook HTTP/1.1\r\nHost: ...\r\n..."
```

A persistent multi-request server needs callbacks/closures, which the
language does not have yet — hence the honest one-shot primitive.
Loop it from a supervisor process for simple services.

## 5. Email (SMTP)

```ga
let fd: int = smtp_connect("127.0.0.1", 25);   // fd, or -1
if fd >= 0 {
    smtp_auth(fd, "user", "pass");              // 1 ok, 0 fail (AUTH LOGIN)
    smtp_mail(fd, "a@x.com", "b@x.com",         // 1 ok, 0 fail
              "Subject here", "Body text");
    smtp_close(fd);                             // sends QUIT
}
```

- Plain TCP + `AUTH LOGIN`. No TLS — for Gmail/465 use `stunnel`
  (see `galang/examples/smtp-gmail.ga` for the setup pattern).
- `smtp_mail` sends a `text/plain; charset=utf-8` message.

## 6. Serial / USB-serial ports

```ga
let fd: int = serial_open("/dev/ttyUSB0", 9600);  // fd, or -1
serial_write(fd, "AT\r\n");
let line: string = serial_read(fd, 256);          // up to 256 bytes
serial_close(fd);
```

- Raw mode (8N1, no flow control). Baud: 9600/19200/38400/57600/115200
  (anything else falls back to 9600).
- Covers USB-serial adapters (`/dev/ttyUSB*`, `/dev/ttyACM*`), Arduino,
  modems, etc. True USB (non-serial, libusb) is Phase 2.

---

## Phase 2 (not yet implemented)

- TLS for HTTP/SMTP (would need a TLS stack or an `stunnel`-style helper).
- Full HTTP server with routing (needs closures as values).
- Raw USB via libusb (external C dependency — breaks the freestanding model).
- `Result<T, E>` error returns instead of `""` / `-1`.
