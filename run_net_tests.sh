#!/bin/sh
# Network tests for GarudaLang system builtins (http, smtp).
# Spins up local servers on 127.0.0.1 — no internet required.
# Usage: ./run_net_tests.sh
set -u
GAR=${GAR:-./bin/gar}
pass=0; fail=0
green() { printf '\033[32m%s\033[0m\n' "$1"; }
red()   { printf '\033[31m%s\033[0m\n' "$1"; }

if [ ! -x "$GAR" ]; then echo "error: $GAR not built" >&2; exit 1; fi
command -v python3 >/dev/null || { echo "error: python3 required"; exit 1; }

TMPD=$(mktemp -d)
trap 'rm -rf "$TMPD"; kill $HTTPD $SMTPD 2>/dev/null' EXIT INT TERM

# --- fixtures ---
echo "hello-net" > "$TMPD/index.txt"
cat > "$TMPD/http.ga" << 'EOF'
fn main() {
    let b: string = http_get("http://127.0.0.1:28931/index.txt");
    print(http_code());
    print(b);
    let p: string = http_post("http://127.0.0.1:28931/index.txt", "data=1");
    print(http_code());
    print(p);
}
EOF
cat > "$TMPD/serve.ga" << 'EOF'
fn main() {
    let req: string = http_serve_once(28932, "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok");
    print(req);
}
EOF
cat > "$TMPD/smtp.ga" << 'EOF'
fn main() {
    let fd: int = smtp_connect("127.0.0.1", 28933);
    print(fd > 0);
    print(smtp_auth(fd, "u", "p"));
    print(smtp_mail(fd, "a@x.com", "b@x.com", "subj", "body"));
    smtp_close(fd);
}
EOF
cat > "$TMPD/fake_smtp.py" << 'EOF'
import socket
def handle(c):
    f = c.makefile('rwb')
    f.write(b"220 fake\r\n"); f.flush()
    while True:
        line = f.readline().decode().strip()
        if not line: break
        cmd = line.split()[0].upper()
        if cmd == "EHLO": f.write(b"250-fake\r\n250 OK\r\n")
        elif cmd == "AUTH":
            f.write(b"334 VXNlcm5hbWU6\r\n"); f.flush(); f.readline()
            f.write(b"334 UGFzc3dvcmQ6\r\n"); f.flush(); f.readline()
            f.write(b"235 OK\r\n")
        elif cmd in ("MAIL","RCPT"): f.write(b"250 OK\r\n")
        elif cmd == "DATA":
            f.write(b"354 End\r\n"); f.flush()
            while f.readline().strip() != b".": pass
            f.write(b"250 queued\r\n")
        elif cmd == "QUIT": f.write(b"221 Bye\r\n"); f.flush(); break
        else: f.write(b"250 OK\r\n")
        f.flush()
    c.close()
s = socket.socket(); s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(("127.0.0.1", 28933)); s.listen(1)
c, _ = s.accept(); handle(c)
EOF

# --- servers ---
cat > "$TMPD/mini_http.py" << 'EOF'
import http.server, socketserver, os
D = os.environ["NET_TMPD"]
class H(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        try:
            with open(D + self.path, "rb") as f: body = f.read()
            self.send_response(200); self.send_header("Content-Length", str(len(body)))
            self.end_headers(); self.wfile.write(body)
        except FileNotFoundError:
            self.send_response(404); self.end_headers()
    def do_POST(self):
        n = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(n)
        self.send_response(200); self.send_header("Content-Length", str(len(body)))
        self.end_headers(); self.wfile.write(b"echo:" + body)
    def log_message(self, *a): pass
socketserver.TCPServer.allow_reuse_address = True
socketserver.TCPServer(("127.0.0.1", 28931), H).serve_forever()
EOF
NET_TMPD="$TMPD" python3 "$TMPD/mini_http.py" >/dev/null 2>&1 & HTTPD=$!
python3 "$TMPD/fake_smtp.py" >/dev/null 2>&1 & SMTPD=$!
sleep 1

# --- 1: http client ---
if "$GAR" build "$TMPD/http.ga" -o "$TMPD/http" >/dev/null 2>&1; then
    out=$(timeout 10 "$TMPD/http" 2>&1)
    # expect: 200 hello-net 200 echo:data=1  (no newlines in output)
    case "$out" in
        200hello-net*200echo:data=1*) green "ok   http client (get/post/code)"; pass=$((pass+1)) ;;
        *) red "FAIL http client: got [$out]"; fail=$((fail+1)) ;;
    esac
else red "FAIL http client (build)"; fail=$((fail+1)); fi

# --- 2: http server (one-shot) ---
if "$GAR" build "$TMPD/serve.ga" -o "$TMPD/serve" >/dev/null 2>&1; then
    "$TMPD/serve" > "$TMPD/req.txt" 2>&1 & SRV=$!
    sleep 1
    resp=$(curl -s --max-time 5 http://127.0.0.1:28932/x)
    wait $SRV 2>/dev/null
    req=$(head -c 60 "$TMPD/req.txt")
    if [ "$resp" = "ok" ]; then
        case "$req" in "GET /x HTTP/1.1"*)
            green "ok   http server (serve_once)"; pass=$((pass+1)) ;;
        *) red "FAIL http server: bad request [$req]"; fail=$((fail+1)) ;; esac
    else red "FAIL http server: bad response [$resp]"; fail=$((fail+1)); fi
else red "FAIL http server (build)"; fail=$((fail+1)); fi

# --- 3: smtp ---
if "$GAR" build "$TMPD/smtp.ga" -o "$TMPD/smtp" >/dev/null 2>&1; then
    out=$(timeout 15 "$TMPD/smtp" 2>&1)
    if [ "$out" = "111" ]; then
        green "ok   smtp (connect/auth/mail)"; pass=$((pass+1))
    else red "FAIL smtp: got [$out] want [111]"; fail=$((fail+1)); fi
else red "FAIL smtp (build)"; fail=$((fail+1)); fi

echo "----"
echo "net: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
