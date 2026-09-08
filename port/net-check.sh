#!/bin/bash
# Диагностика исходящей связности из WSL.
# Проверяет, что именно недоступно: GitHub, весь интернет или отдельные сети.

for entry in \
	"140.82.121.3:443:github.com" \
	"140.82.121.4:443:github.com (другой IP)" \
	"185.199.108.133:443:raw.githubusercontent.com" \
	"1.1.1.1:443:Cloudflare DNS" \
	"8.8.8.8:443:Google DNS" \
	"172.66.152.176:443:archive.ubuntu.com" \
	"91.189.91.83:80:archive.ubuntu.com (прямой)" \
	; do
	host="${entry%%:*}"
	rest="${entry#*:}"
	port="${rest%%:*}"
	name="${rest#*:}"

	if timeout 8 bash -c "cat < /dev/null > /dev/tcp/${host}/${port}" 2>/dev/null; then
		printf "  OK    %-20s %-5s  %s\n" "${host}" "${port}" "${name}"
	else
		printf "  FAIL  %-20s %-5s  %s\n" "${host}" "${port}" "${name}"
	fi
done

echo
echo "=== HTTPS через curl (с таймаутом) ==="
curl -s -o /dev/null -w "github.com      -> %{http_code} за %{time_total}s\n" \
	--max-time 15 https://github.com/ 2>&1 || echo "github.com      -> провал"
curl -s -o /dev/null -w "archive.ubuntu  -> %{http_code} за %{time_total}s\n" \
	--max-time 15 http://archive.ubuntu.com/ 2>&1 || echo "archive.ubuntu  -> провал"
