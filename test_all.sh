#!/usr/bin/env bash
# test_all.sh — Full suite (ultra + half-close + poll) avec robustesse Bash
set -euo pipefail

BIN=${BIN:-./webserv}
CONF=${CONF:-conf/all.conf}
HOST=127.0.0.1
PORT_READY=8080
READINESS_URL="http://${HOST}:${PORT_READY}/"
READY_TRIES=60
READY_SLEEP=0.1

PASS=0; FAIL=0
green(){ printf "\033[32m%s\033[0m" "$1"; }
red(){ printf "\033[31m%s\033[0m" "$1"; }
ok(){ echo "[ $(green OK) ] $*"; PASS=$((PASS+1)); }
ko(){ echo "[ $(red KO) ] $*"; FAIL=$((FAIL+1)); }
note(){ printf "\n— %s —\n" "$*"; }

code_get(){ curl -s -o /dev/null -w "%{http_code}" "$1" || echo "ERR"; }
code_get_as_is(){ curl --path-as-is -s -o /dev/null -w "%{http_code}" "$1" || echo "ERR"; }
get_header(){
  local url="$1" h="$2"
  curl -sD - -o /dev/null "$url" \
    | awk -v k="$h" 'BEGIN{IGNORECASE=1} match(tolower($0), "^" tolower(k) ":"){sub("\r","");sub("^[^:]+:[ ]*","");print;exit}'
}
get_header_as_is(){
  local url="$1" h="$2"
  curl --path-as-is -sD - -o /dev/null "$url" \
    | awk -v k="$h" 'BEGIN{IGNORECASE=1} match(tolower($0), "^" tolower(k) ":"){sub("\r","");sub("^[^:]+:[ ]*","");print;exit}'
}
check_eq(){ [[ "$1" == "$2" ]] && ok "$3" || ko "$3  (got: '$1' expected: '$2')"; }
check_in(){ local got="$1" label="$2"; shift 2; for v in "$@"; do [[ "$got" == "$v" ]] && { ok "$label"; return; }; done; ko "$label (got: '$got' expected one of: $*)"; }
code_cmd(){ eval "$1" 2>/dev/null | head -n1 | awk '{print $2}'; }
header_from_cmd(){ local cmd="$1" h="$2"; eval "$cmd" 2>/dev/null | awk -v k="$h" 'BEGIN{IGNORECASE=1} match(tolower($0), "^" tolower(k) ":"){sub("\r","");sub("^[^:]+:[ ]*","");print;exit}'; }
http_raw_status() {
  local host="$1" port="$2" payload="$3" line code
  exec 3<>"/dev/tcp/$host/$port" || { echo ""; return; }
  printf "%s" "$payload" >&3
  IFS=$'\r' read -r line <&3 || line=""
  exec 3>&- 3<&-
  code=$(printf "%s\n" "$line" | awk '{print $2}')
  echo "$code"
}

SRV=""

start_server(){
  note "Démarrage serveur"
  pkill -f "$BIN" 2>/dev/null || true
  "$BIN" "$CONF" >/tmp/webserv.fulltest.out 2>&1 &
  SRV=$!
  for _ in $(seq 1 $READY_TRIES); do
    curl -sI "$READINESS_URL" >/dev/null 2>&1 && { ok "Serveur prêt (pid=$SRV)"; return 0; }
    sleep "$READY_SLEEP"
  done
  ko "Serveur inaccessible sur $READINESS_URL"; return 1
}

stop_server(){
  note "Extinction serveur"
  if [[ -n "${SRV:-}" ]] && ps -p "$SRV" >/dev/null 2>&1; then
    kill "$SRV" 2>/dev/null || true
    wait "$SRV" 2>/dev/null || true
    ok "Serveur arrêté (pid=$SRV)"
  else
    ok "Serveur déjà arrêté"
  fi
}

cleanup_on_exit(){
  stop_server
  note "Résumé global"
  echo -e "✅ $(green $PASS) tests passés"
  if [[ "$FAIL" -gt 0 ]]; then
    echo -e "❌ $(red $FAIL) tests échoués"; exit 1
  else
    echo -e "🎉 Aucun échec — serveur très solide !"; exit 0
  fi
}
trap cleanup_on_exit EXIT INT TERM

prepare_fixtures(){
  note "Préparation des fixtures"
  mkdir -p www/static/img www/errors www/uploads www/form_uploads www/trash www/noindex
  mkdir -p www/redirect_site www/get_only www/form_site www/siteA www/siteB www/siteB/static
  echo "ROOT INDEX" > www/index.html
  echo "<h1>Static index</h1>" > www/static/index.html
  printf '%s\n' 'pixel' > 'www/static/img/pixel.txt'
  printf '%s\n' 'with space' > 'www/static/img/with space.txt'
  printf '%s\n' 'plus sign'  > 'www/static/img/plus+sign.txt'
  printf '%s\n' 'weird'      > 'www/static/img/a&b"c<d>.txt'
  printf '%s\n' 'café'       > 'www/static/img/café.txt'
  rm -f www/noindex/index.html || true
  printf '%s\n' '<svg xmlns="http://www.w3.org/2000/svg"></svg>' > www/static/img/icon.svg
  printf '\x00\x00\x01\x00' > www/static/img/favicon.ico
  printf '%s' 'RIFFxxxxWEBP' > www/static/img/test.webp
  printf '%s\n' 'JS' > www/static/app.js
  printf '%s\n' 'CSS' > www/static/style.css
  printf '%s\n' '%PDF-1.4' > www/static/doc.pdf
  printf '%s' '(\x00asm' > www/static/mod.wasm
  cat > www/errors/400.html <<EOF
<!DOCTYPE html><meta charset="utf-8"><title>400</title><h1>400 Bad Request</h1>
EOF
  cat > www/errors/403.html <<EOF
<!DOCTYPE html><meta charset="utf-8"><title>403</title><h1>403 Forbidden</h1>
EOF
  cat > www/errors/404.html <<EOF
<!DOCTYPE html><meta charset="utf-8"><title>404</title><h1>404 Not Found</h1>
EOF
  cat > www/errors/405.html <<EOF
<!DOCTYPE html><meta charset="utf-8"><title>405</title><h1>405 Method Not Allowed</h1>
EOF
  cat > www/errors/413.html <<EOF
<!DOCTYPE html><meta charset="utf-8"><title>413</title><h1>413 Payload Too Large</h1>
EOF
  cat > www/errors/500.html <<EOF
<!DOCTYPE html><meta charset="utf-8"><title>500</title><h1>500 Internal Server Error</h1>
EOF
  cat > www/errors/501.html <<EOF
<!DOCTYPE html><meta charset="utf-8"><title>501</title><h1>501 Not Implemented</h1>
EOF
  cat > www/errors/505.html <<EOF
<!DOCTYPE html><meta charset="utf-8"><title>505</title><h1>505 HTTP Version Not Supported</h1>
EOF
  echo "<h1>New site</h1>" > www/redirect_site/index.html
  echo "<h1>GET ONLY</h1>" > www/get_only/index.html
  echo "<h1>Form GET/POST</h1>" > www/form_site/index.html
  printf '%s\n' 'trashme' > 'www/trash/trash.txt'
  echo "<h1>Site A</h1>" > www/siteA/index.html
  echo "<h1>Site B</h1>" > www/siteB/index.html
  echo "<h1>Site B static</h1>" > www/siteB/static/index.html
  echo "upload small" > /tmp/testfile.txt
  echo "foo" > /tmp/another.txt
  dd if=/dev/zero of=/tmp/big_3m.bin bs=1M count=3 status=none
  mkdir -p www/static
  [ -f www/static/big.bin ] || dd if=/dev/zero of=www/static/big.bin bs=1M count=3 status=none
  ok "Fixtures prêtes"
}

tests_ultra(){
  note "=== 8080 baseline / sécurité / encodage ==="
  check_eq "$(code_get http://127.0.0.1:8080/)" 200 "GET / -> 200"
  check_eq "$(code_get http://127.0.0.1:8080/static/)" 200 "GET /static/ -> 200"
  check_eq "$(code_get http://127.0.0.1:8080/static/img/)" 200 "GET /static/img/ -> 200"
  check_eq "$(code_get http://127.0.0.1:8080/noindex/)" 403 "GET /noindex/ -> 403"
  check_eq "$(code_get_as_is http://127.0.0.1:8080/static/%2e%2e/index.html)" 403 "Traversal encodé -> 403"
  c="$(code_get_as_is http://127.0.0.1:8080/../../etc/passwd)"; [[ "$c" == "403" || "$c" == "404" ]] && ok "Traversal brut -> $c" || ko "Traversal brut -> (got $c expected 403|404)"
  check_eq "$(code_get_as_is http://127.0.0.1:8080//static//img//)" 200 "Double slashes -> 200"
  code=$(code_get_as_is http://127.0.0.1:8080/static/./img/); [[ "$code" == "200" || "$code" == "403" ]] && ok "Dot segment -> 200|403 (got $code)" || ko "Dot segment -> 200|403 (got $code)"
  check_eq "$(code_get "http://127.0.0.1:8080/static/img/with%20space.txt")" 200 "Espace encodé -> 200"
  check_eq "$(code_get "http://127.0.0.1:8080/static/img/plus+sign.txt")" 200 "Plus intact -> 200"
  check_eq "$(code_get "http://127.0.0.1:8080/static/img/a%26b%22c%3Cd%3E.txt")" 200 "Noms dangereux -> 200"
  check_eq "$(code_get "http://127.0.0.1:8080/static/img/caf%C3%A9.txt")" 200 "UTF-8 (é) -> 200"
  check_eq "$(code_get_as_is http://127.0.0.1:8080/static/%ZZ/img/)" 400 "Percent invalide ZZ -> 400"
  check_eq "$(code_get_as_is http://127.0.0.1:8080/static/%)" 400 "Percent tronqué -> 400"
  check_eq "$(code_get http://127.0.0.1:8080/static/does-not-exist.txt)" 404 "404 statique -> 404"
  ct="$(get_header http://127.0.0.1:8080/static/does-not-exist.txt 'Content-Type')"
  check_in "$ct" "Content-Type 404 statique" "text/html" "text/plain"
  check_eq "$(code_get http://127.0.0.1:8080/static/img/plus%2Fsign.txt)" 404 "Nom avec %2F -> 404"

  note "=== MIME types ==="
  mime_cases=(
    "text/css|http://127.0.0.1:8080/static/style.css"
    "application/javascript|http://127.0.0.1:8080/static/app.js"
    "image/svg+xml|http://127.0.0.1:8080/static/img/icon.svg"
    "image/x-icon|http://127.0.0.1:8080/static/img/favicon.ico"
    "image/webp|http://127.0.0.1:8080/static/img/test.webp"
    "application/pdf|http://127.0.0.1:8080/static/doc.pdf"
    "application/wasm|http://127.0.0.1:8080/static/mod.wasm"
  )
  for CASE in "${mime_cases[@]}"; do
    IFS='|' read -r expect url <<< "$CASE"
    got="$(get_header "$url" Content-Type)"
    check_eq "$got" "$expect" "MIME $url"
  done

  note "=== 8081 upload / delete / sanitize ==="
  code=$(code_cmd 'curl -i -X POST -F "file=@/tmp/testfile.txt" http://127.0.0.1:8081/upload')
  check_eq "$code" 201 "POST /upload (simple)"
  ct_up="$(get_header http://127.0.0.1:8081/uploads/testfile.txt 'Content-Type')"
  check_eq "$ct_up" "text/plain" "Content-Type uploadé (text/plain)"
  code=$(code_cmd 'curl -i -X POST -F "file=@/tmp/testfile.txt" -F "file=@/tmp/testfile.txt;filename=bad name.txt" http://127.0.0.1:8081/upload')
  check_eq "$code" 201 "POST /upload (multi)"
  check_eq "$(code_get http://127.0.0.1:8081/uploads/bad_name.txt)" 200 "sanitize nom -> bad_name.txt dispo"
  check_eq "$(code_get http://127.0.0.1:8081/uploads/)" 200 "GET /uploads/ -> 200"
  check_eq "$(code_get http://127.0.0.1:8081/uploads/testfile.txt)" 200 "fichier uploadé présent -> 200"
  code=$(code_cmd 'curl -i -X DELETE http://127.0.0.1:8081/delete/testfile.txt'); check_eq "$code" 204 "DELETE fichier existant -> 204"
  code=$(code_cmd 'curl -i -X DELETE http://127.0.0.1:8081/delete/nope.txt'); check_eq "$code" 404 "DELETE inconnu -> 404"
  mkdir -p www/uploads/tmpdir
  code=$(code_cmd 'curl -i -X DELETE http://127.0.0.1:8081/delete/tmpdir'); check_eq "$code" 403 "DELETE dossier -> 403"
  code=$(code_cmd 'curl -i -X DELETE "http://127.0.0.1:8081/delete/../index.html"'); [[ "$code" == "403" || "$code" == "404" ]] && ok 'DELETE ../ -> 403|404' || ko "DELETE ../ -> (got $code expected 403|404)"
  code=$(code_cmd 'curl -i -X DELETE "http://127.0.0.1:8081/delete/%2e%2e/index.html"'); check_eq "$code" 403 "DELETE %2e%2e -> 403"
  cmd='curl -i -X DELETE http://127.0.0.1:8081/upload'
  code=$(code_cmd "$cmd"); check_eq "$code" 405 "DELETE /upload -> 405"
  allow=$(header_from_cmd "$cmd" "Allow")
  if [[ "$allow" == "GET, POST" || "$allow" == "GET, POST"* ]]; then ok "Allow sur 405 (upload)"; else ko "Allow sur 405 (upload) (got: '$allow')"; fi
  code=$(http_raw_status 127.0.0.1 8081 $'POST /upload HTTP/1.1\r\nHost: 127.0.0.1:8081\r\nContent-Length: 7\r\n\r\nNopeRaw'); check_eq "$code" 400 "POST raw sans multipart -> 400"

  note "=== 8082 redirects ==="
  check_eq "$(code_get http://127.0.0.1:8082/old)" 301 "GET /old -> 301"
  loc=$(get_header http://127.0.0.1:8082/old Location); check_eq "$loc" "http://127.0.0.1:8082/new" "Location /old"
  check_eq "$(code_get http://127.0.0.1:8082/temp)" 302 "GET /temp -> 302"
  loc=$(get_header http://127.0.0.1:8082/temp Location); check_eq "$loc" "http://127.0.0.1:8082/new" "Location /temp"
  check_eq "$(code_get http://127.0.0.1:8082/new)" 200 "GET /new -> 200"

  note "=== 8083 methods & limits ==="
  check_eq "$(code_get http://127.0.0.1:8083/get-only/)" 200 "GET /get-only -> 200"
  cmd405='curl -i -X POST http://127.0.0.1:8083/get-only/'
  code=$(code_cmd "$cmd405"); check_eq "$code" 405 "POST /get-only -> 405"
  allow=$(header_from_cmd "$cmd405" "Allow"); check_eq "$allow" "GET" "Allow sur 405 (get-only)"
  check_eq "$(code_get http://127.0.0.1:8083/form/)" 200 "GET /form -> 200"
  code=$(code_cmd 'curl -i -X POST -F "file=@/tmp/testfile.txt" http://127.0.0.1:8083/form/'); check_eq "$code" 201 "POST /form multipart -> 201"
  code=$(code_cmd 'curl -i -X POST -F "file=@/tmp/big_3m.bin" http://127.0.0.1:8083/form/'); check_eq "$code" 413 "POST /form >2M -> 413"
  code=$(code_cmd 'curl -i -X DELETE http://127.0.0.1:8083/trash/trash.txt'); check_eq "$code" 204 "DELETE /trash/trash.txt -> 204"

  note "=== 8080 autres méthodes ==="
  check_eq "$(code_cmd 'curl -i -X HEAD http://127.0.0.1:8080/')" 405 "HEAD -> 405"
  check_eq "$(code_cmd 'curl -i -X PUT http://127.0.0.1:8080/')" 405 "PUT -> 405"
  check_eq "$(code_cmd 'curl -i -X PATCH http://127.0.0.1:8080/')" 405 "PATCH -> 405"

  note "=== Raw HTTP edge-cases (/dev/tcp) ==="
  check_eq "$(http_raw_status 127.0.0.1 8080 $'GET / HTTP/1.1\r\nConnection: close\r\n\r\n')" 400 "HTTP/1.1 sans Host -> 400"
  check_eq "$(http_raw_status 127.0.0.1 8080 $'GET / HTTP/1.0\r\n\r\n')" 505 "HTTP/1.0 -> 505"
  check_eq "$(http_raw_status 127.0.0.1 8080 $'GET / HTTP/2.0\r\n\r\n')" 505 "HTTP/2.0 -> 505"

note "=== Transfer-Encoding: chunked (anti-hang) ==="
if python3 - <<'PY'
import socket, sys
host, port = "127.0.0.1", 8081
req = (
    "POST /upload HTTP/1.1\r\n"
    "Host: 127.0.0.1:8081\r\n"
    "Transfer-Encoding: chunked\r\n"
    "Content-Type: multipart/form-data; boundary=xx\r\n"
    "\r\n"
    "4\r\n"
    "test\r\n"
    "0\r\n"
    "\r\n"
).encode("ascii")
s = socket.socket()
s.settimeout(2.0)  # timeout anti-hang
s.connect((host, port))
s.sendall(req)
# lire uniquement la première ligne du status
buf = b""
while b"\r\n" not in buf:
    b = s.recv(1)
    if not b:
        break
    buf += b
s.close()
line = buf.split(b"\r\n",1)[0] if buf else b""
ok = (b" 501 " in line or line.startswith(b"HTTP/1.1 501"))
sys.exit(0 if ok else 1)
PY
then ok "POST chunked -> 501"
else ko "POST chunked n'a pas renvoyé 501 (ou pas de status line)"
fi

  note "=== En-têtes & Connection ==="
  conn="$(get_header http://127.0.0.1:8080/ 'Connection')";   check_eq "$conn" "close" "Connection: close sur /"
  conn2="$(get_header http://127.0.0.1:8080/static/img/ 'Connection')"; check_eq "$conn2" "close" "Connection: close sur /static/img/"

  note "=== Content-Length cohérente ==="
  hdr_len=$(curl -sD - http://127.0.0.1:8080/static/img/pixel.txt -o /dev/null | awk '/^Content-Length:/ {gsub("\r",""); print $2; exit}')
  body_len=$(curl -s http://127.0.0.1:8080/static/img/pixel.txt | wc -c | awk '{print $1}')
  check_eq "$hdr_len" "$body_len" "Content-Length = taille body pour pixel.txt"

  note "=== Méthodes/Headers non implémentés (tolérance) ==="
  rng_code=$(code_cmd 'curl -i -H "Range: bytes=0-3" http://127.0.0.1:8080/static/img/pixel.txt'); check_eq "$rng_code" 200 "Range ignoré -> 200 (pas 206)"
  st=$(http_raw_status 127.0.0.1 8080 $'GET / HTTP/1.1\r\nhost: 127.0.0.1:8080\r\n\r\n'); check_eq "$st" 200 "Host insensible à la casse"
  st=$(http_raw_status 127.0.0.1 8080 $'GET / HTTP/1.1\r\nHost: \r\n\r\n'); check_eq "$st" 400 "Host vide -> 400"
  st=$(http_raw_status 127.0.0.1 8081 $'POST /upload HTTP/1.1\r\nHost: 127.0.0.1:8081\r\nContent-Length: xyz\r\n\r\n'); check_eq "$st" 400 "Content-Length non numérique -> 400 (pas multipart)"

  note "=== Double-encodage & normalisation stricte ==="
  c_raw="$(code_get_as_is 'http://127.0.0.1:8080/static/%252e%252e/index.html')"
  c="$(printf "%s" "$c_raw" | tr -cd '0-9')"
  if [[ "$c" == "403" || "$c" == "404" ]]; then
    ok "Double-encodage traversal -> $c"
  else
    ko "Double-encodage traversal (got $c_raw expected 403|404)"
    printf "debug(hex) c_raw: "; printf "%s" "$c_raw" | od -An -t x1; echo
  fi
  printf '%s\n' 'hello' > www/static/hello.txt
  check_eq "$(code_get_as_is 'http://127.0.0.1:8080//static//hello.txt')" 200 "Double slashes fichier -> 200"
  check_eq "$(code_get 'http://127.0.0.1:8080/static/img/plus+sign.txt')" 200 "Plus non transformé -> 200"
  c00="$(code_get_as_is 'http://127.0.0.1:8080/static/img/with%0000space.txt')"
  [[ "$(printf "%s" "$c00" | tr -cd '0-9')" =~ ^403$|^404$ ]] && ok "NUL byte dans path -> 403|404 (got $c00)" || ko "NUL byte path (got $c00 expected 403|404)"
  check_eq "$(code_get 'http://127.0.0.1:8080/static/img/plus%252Fsign.txt')" 404 "Nom avec %252F -> 404"

  note "=== Autoindex: href/texte ==="
  html=$(curl -s http://127.0.0.1:8080/static/img/)
  echo "$html" | grep -q '/static/img/with%20space.txt' && ok "Autoindex href: espace encodé" || ko "Autoindex href espace NON encodé"
  echo "$html" | grep -q '/static/img/plus%2Bsign.txt' && ok "Autoindex href: + encodé en %2B" || ko "Autoindex href + NON encodé"
  echo "$html" | grep -q 'a&amp;b&quot;c&lt;d&gt;.txt' && ok "Autoindex texte échappé (& \" < >)" || ko "Autoindex texte NON échappé"

  note "=== Upload: sanitization avancée ==="
  code=$(code_cmd 'curl -i -X POST -F "file=@/tmp/another.txt;filename=weird*name?.txt" http://127.0.0.1:8081/upload'); check_eq "$code" 201 "Upload nom chelou -> 201"
  check_eq "$(code_get http://127.0.0.1:8081/uploads/weird_name_.txt)" 200 "Sanitize -> weird_name_.txt accessible"
  LONGFN=$(printf 'a%.0s' {1..300}).txt
  code=$(code_cmd "curl -i -X POST -F \"file=@/tmp/another.txt;filename=$LONGFN\" http://127.0.0.1:8081/upload"); check_eq "$code" 201 "Upload nom très long -> 201"
  auto=$(curl -s http://127.0.0.1:8081/uploads/); echo "$auto" | grep -q '\.txt' && ok "Sanitize long -> extension conservée" || ko "Sanitize long -> extension non trouvée"
  code=$(code_cmd 'curl -i -X POST -F "file=@/tmp/another.txt;filename=.hidden.txt" http://127.0.0.1:8081/upload'); check_eq "$code" 201 "Upload .hidden.txt -> 201"
  auto=$(curl -s http://127.0.0.1:8081/uploads/); echo "$auto" | grep -q 'upload_' && ok "Sanitize leading dot -> upload_* présent" || ko "Sanitize leading dot -> upload_* non trouvé"
  code=$(code_cmd 'curl -i -X POST -F "file=@/tmp/another.txt;filename=../../escape.txt" http://127.0.0.1:8081/upload'); check_eq "$code" 201 "Upload filename traversal -> 201 (neutralisé)"
  check_eq "$(code_get http://127.0.0.1:8081/uploads/../../escape.txt)" 404 "Access traversal upload impossible"

  note "=== DELETE: variations & sécurité ==="
  echo "to-delete" > www/uploads/once.txt
  check_eq "$(code_cmd 'curl -i -X DELETE http://127.0.0.1:8081/delete/once.txt')" 204 "DELETE /delete/once.txt -> 204"
  code=$(code_cmd 'curl -i -X DELETE "http://127.0.0.1:8081/delete/../form_site/index.html"')
  [[ "$code" == "403" || "$code" == "404" ]] && ok 'DELETE rebond hors store -> 403|404' || ko "DELETE rebond hors store (got $code)"

  note "=== Symlink escape (doit être bloqué) ==="
  ln -s /etc/hosts www/static/img/leak.txt 2>/dev/null || true
  csym="$(code_get http://127.0.0.1:8080/static/img/leak.txt)"
  [[ "$csym" == "403" || "$csym" == "404" ]] && ok "Symlink hors racine bloqué -> 403|404" || ko "Symlink hors racine (got $csym)"

  note "=== Robustesse supplémentaires ==="
  dd if=/dev/zero of=www/static/big.bin bs=1M count=3 status=none
  check_eq "$(code_get http://127.0.0.1:8080/static/big.bin)" 200 "GET gros fichier statique 3M -> 200"

  note "=== Rafales rapides ==="
  ok_count=0; ko_count=0
  for _ in $(seq 1 30); do
    c="$(code_get http://127.0.0.1:8080/static/img/)"
    if [[ "$c" == "200" ]]; then ok_count=$((ok_count+1)); else ko_count=$((ko_count+1)); fi
  done
  [[ "$ko_count" -eq 0 ]] && ok "30 GET consécutifs OK" || ko "Rafale: $ko_count échecs / 30"
}

test_halfclose(){
  note "Half-close client (shutdown write) — le serveur doit QUAND MÊME envoyer la réponse"
  if python3 - <<'PY'
import socket, time, sys
HOST, PORT = "127.0.0.1", 8080
req = ( "GET /static/big.bin HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Connection: keep-alive\r\n"
        "\r\n").encode("ascii")
s = socket.socket(); s.settimeout(5); s.connect((HOST, PORT))
s.sendall(req); s.shutdown(socket.SHUT_WR)
buf = bytearray(); deadline = time.time() + 3.0
while time.time() < deadline:
    try: b = s.recv(64)
    except Exception: break
    if not b: break
    buf += b; time.sleep(0.01)
s.close()
ok = buf.startswith(b"HTTP/1.1 200 OK\r\n")
head_end = buf.find(b"\r\n\r\n")
body_ok = (head_end > 0 and len(buf) - (head_end+4) > 4096)
sys.exit(0 if (ok and body_ok) else 1)
PY
  then ok "Réponse envoyée malgré POLLHUP (POLLOUT prioritaire)"
  else ko "Half-close: pas de 200/flux corps suffisant"
  fi
}

test_poll_useful(){
  note "Mesure latence GET rapide sous charge mixte (fairness poll)"
  SLOW_RUNTIME=8; SLOW_READERS=25; SLOW_SENDERS=25
  python3 - <<PY &
import socket, threading, time
HOST, PORT = "127.0.0.1", 8080
RUNTIME, READERS, SENDERS = 8, 25, 25
def slow_reader(i):
    try:
        s = socket.socket(); s.settimeout(2); s.connect((HOST, PORT))
        s.sendall(b"GET /static/big.bin HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: keep-alive\r\n\r\n")
        end = time.time() + RUNTIME
        while time.time() < end:
            try:
                b = s.recv(1)
                if not b: break
            except Exception:
                break
            time.sleep(0.002)
        s.close()
    except Exception:
        pass
def slow_sender(i):
    try:
        s = socket.socket(); s.settimeout(2); s.connect((HOST, PORT))
        head = [b"GET /static/big.bin HTTP/1.1\r\n", b"Host: 127.0.0.1\r\n", b"Connection: keep-alive\r\n", b"\r\n"]
        end = time.time() + RUNTIME
        for line in head:
            for b in line:
                s.send(bytes([b])); time.sleep(0.002)
            if time.time() >= end: break
        time.sleep(0.5); s.close()
    except Exception:
        pass
threads=[]
for i in range(READERS):
    t=threading.Thread(target=slow_reader,args=(i,),daemon=True); t.start(); threads.append(t)
for i in range(SENDERS):
    t=threading.Thread(target=slow_sender,args=(i,),daemon=True); t.start(); threads.append(t)
time.sleep(RUNTIME)
PY
  SLOWPID=$!
  sleep 0.5
  t=$(curl -s -o /dev/null -w '%{time_total}' "http://$HOST:8080/static/")
  echo "latency=${t}s (budget 0.70s)"
  awk -v t="$t" -v budget="0.70" 'BEGIN{ exit (t<=budget)?0:1 }' \
    && ok "GET rapide servi dans le budget (fair, non bloqué)" \
    || ko "GET rapide trop lent sous charge (t=${t}s > 0.70s) — boucle poll probablement non équitable"
  wait "$SLOWPID"
}

# -------- Main --------
prepare_fixtures
start_server
tests_ultra
test_halfclose
test_poll_useful
# fin : trap fait le résumé/exit
