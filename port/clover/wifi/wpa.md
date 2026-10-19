# WPA2-PSK для Wi-Fi MEECHO (WCN3990): что есть, что нужно, что брать

Дата разбора: 2026-10-19. Только чтение; деревья не тронуты. Справочный
sparse-клон NetBSD-current (github.com/NetBSD/src, коммит `b9197611`,
2026-10-19) с каталогами `external/bsd/wpa` и `sys/net80211` лежит во
временном `/tmp/nbsrc-wpa` в WSL — все числа ниже по нему.

## 1. Что есть в деревьях

| Где | Что | Итог |
|---|---|---|
| `~/minix-src/external/bsd` | atf bind blacklist byacc bzip2 dhcp **dhcpcd** … **libpcap** (1.7.2) … **tcpdump** … | **wpa_supplicant/hostapd нет вовсе**, `usr.sbin/wpa*` нет. Слово `wpa_supplicant` встречается только в Makefile dhcpcd |
| `~/minix-src/sys/net80211` | только заголовки: `_ieee80211.h`, `ieee80211.h`, `ieee80211_crypto.h`, `ieee80211_ioctl.h`, `ieee80211_netbsd.h` — 2064 строки | Импортированы коммитом `37274f3cd` «Import additional networking headers» (NetBSD 2015, база FreeBSD 2005). Кода net80211 нет. `SIOCS80211`/`SIOCG80211`, `IEEE80211_IOC_*`, `RTM_IEEE80211` (в `sys/net/route.h`) определены — но их **никто не обслуживает** (см. §2) |
| `~/netbsd-current` | частичный: `external/bsd/{bzip2,fetch,file,less,mdocml,nvi,pkg_install}`, `historical/nawk`, `mpl/mozilla-certdata`, `public-domain/sqlite`, `crypto`, `lib`, `usr.bin`, `usr.sbin` | **ни `external/bsd/wpa`, ни `sys/net80211`, ни `usr.sbin/wpa*` нет** |
| `~/netbsd-src` (2015) | `external/bsd`, `external/historical`, `sys/{arch,lib,sys}` | **тоже нет** ни wpa, ни net80211 (sparse-клон) |
| NetBSD-current (github, 2026-10-19) | `external/bsd/wpa/dist` — hostap **2.11** (`src/common/version.h`: `VERSION_STR "2.11"`, README: © 2002-2024). Импорт по `NetBSD-upgrade`: wpa_supplicant + hostapd из тарболов w1.fi, vendor-ветка `MALINEN`. Reachover — `external/bsd/wpa/bin/{wpa_supplicant,wpa_cli,wpa_passphrase,hostapd,hostapd_cli}` (не `usr.sbin/wpa`) | брать отсюда |
| NetBSD-current `sys/net80211` | 28 файлов, **21 310 строк** .c/.h; `$FreeBSD: … ieee80211.c,v 1.22 2005/08/10 sam` — то есть всё ещё net80211 FreeBSD 2005 года | **802.11n/ac нет вообще** (в .c ни одного `HTCAP`; только номер элемента в `ieee80211.h`). WPA/WPA2 есть |

Лицензии:
- **hostap (wpa_supplicant/hostapd)** — BSD 3-clause («BSD license with
  advertisement clause removed»); GPLv2-вариант снят с 2012-02-11 (`COPYING`,
  `README`). В дерево BSD идёт без оговорок.
- **net80211** — BSD 2-clause (Onoe, Sam Leffler); в 20 из 28 файлов
  дополнительно «Alternatively … GNU GPL v2» — двойная, берётся по BSD.
- **libpcap** (в дереве) — BSD 3-clause.
- Справка `~/wifi-ref/ath10k`, `~/wifi-ref/soc` — Linux, GPL/ISC: как и
  `port/cb2-gmac/ref/`, только номера и раскладки, не текст.

## 2. Бэкенды драйвера в wpa_supplicant

### driver_bsd.c (1771 строка) — то, что собирает NetBSD

Таблица ops (STA-половина): `init2`, `deinit`, `get_bssid`, `get_ssid`,
`set_countermeasures`, `scan2`, `get_scan_results2`, `deauthenticate`,
`associate`, `get_capa`, `set_freq`, `set_key`, `set_ieee8021x`,
`set_generic_elem`.

Всё это — **ioctl на сокет интерфейса**:
- `SIOCS80211`/`SIOCG80211` (`struct ieee80211req`) с `i_type`:
  `IEEE80211_IOC_SSID, BSSID, CHANNEL, AUTHMODE, PRIVACY, WPA, WPAIE, OPTIE,
  APPIE, WPAKEY, DELKEY, MCASTCIPHER, MCASTKEYLEN, UCASTCIPHERS, KEYMGTALGS,
  RSNCAPS, DROPUNENCRYPTED, COUNTERMEASURES, ROAMING, DEVCAPS, SCAN_REQ,
  SCAN_RESULTS, STA_STATS` и **`IEEE80211_IOC_MLME`** с `im_op =
  IEEE80211_MLME_ASSOC / DEAUTH / DISASSOC / AUTHORIZE / UNAUTHORIZE`;
- плюс `SIOCS80211NWID`, `SIOCG80211BSSID`, `SIOCS80211CHANNEL`,
  `SIOC[SG]IFMEDIA`, `SIOCGIFFLAGS`.
- События — **маршрутный сокет**: `RTM_IEEE80211` с подтипами `ASSOC,
  REASSOC, DISASSOC, JOIN, LEAVE, REJOIN, SCAN, REPLAY, MICHAEL`, плюс
  `RTM_IFINFO`, `RTM_IFANNOUNCE`.
- EAPOL — через `l2_packet_freebsd.c` (BPF через **libpcap**).

`wpa_driver_bsd_associate()` показывает модель целиком: выставить
`AUTHMODE`, `WPAIE` (готовый RSN IE от supplicant), `PRIVACY`, `WPA=2` и
сказать `IEEE80211_MLME_ASSOC` с SSID/BSSID. **Кадры Authentication и
Association Request строит и шлёт ядерный net80211**, он же ждёт ответы,
ведёт состояние (`IEEE80211_S_AUTH → ASSOC → RUN`) и шлёт `RTM_IEEE80211_ASSOC`.
То есть driver_bsd предполагает MLME в ядре; сам supplicant делает только
4-way/group handshake и ставит ключи.

**Для MEECHO этот путь закрыт вдвойне:** (а) net80211 нет; (б) ioctl на
сокет интерфейса уходит в `lwip`, а не в драйвер: `ifconf_ioctl()`
(`minix/net/lwip/ifconf.c`) знает только `SIOC[SG]IF{FLAGS,METRIC,MEDIA,MTU,
CAP,DLT,INDEX}` и `MINIX_SIOCGIFMEDIA`, остальное — `ENOTTY`; в протоколе
`ndev` (`minix/include/minix/com.h`) `NDEV_IOCTL` помечен «(reserved)» и не
реализован ни в `lwip`, ни в `libnetdriver`. `RTM_IEEE80211` lwip не
порождает (только `RTM_IFINFO`/`RTM_IFANNOUNCE`).

### nl80211 для сравнения (driver_nl80211*.c — 14 158 + 4 272 строк, `sme.c` 3629)

- Без `WPA_DRIVER_FLAGS_SME`: `NL80211_CMD_CONNECT` — SME и MLME в
  cfg80211/mac80211 или в прошивке (fullmac), как у driver_bsd.
- С `WPA_DRIVER_FLAGS_SME` (mac80211): SME в supplicant (`sme.c`) —
  выбирает BSS, решает, когда слать `NL80211_CMD_AUTHENTICATE` и
  `NL80211_CMD_ASSOCIATE` (для SAE/FT сам даёт тело auth), **но кадры
  собирает mac80211 (`mlme.c`)**, он же ловит ответы и таймауты.
- EAPOL — с 2.10 по **control port** (`NL80211_CMD_CONTROL_PORT_FRAME`):
  флаги `WPA_DRIVER_FLAGS_CONTROL_PORT` + `WPA_DRIVER_FLAGS2_CONTROL_PORT_RX`,
  op `tx_control_port`, приём событием `EVENT_EAPOL_RX`. Тогда
  `wpas_eapol_needs_l2_packet()` = 0 и l2_packet приём не открывает.

Вывод: **в любом бэкенде кадры auth/assoc строит не wpa_supplicant** (кроме
тела SAE/FT). У WCN3990 (ath10k, WMI-TLV) прошивка это тоже не делает: в
Linux их строит mac80211 и отдаёт `WMI_TLV_MGMT_TX_SEND_CMD`; прошивка
делает скан (`WMI_START_SCAN`, результаты — принятые beacon/probe-resp),
`peer_assoc`, ключи (`vdev_install_key`) и шифрование. Значит **маленький
MLME обязан жить в нашем драйвере** — при любом выборе supplicant'а.

### Что должен реализовать `driver_meecho.c` (WPA2-PSK, STA, без SME-флага)

Канал к драйверу — **не сокет интерфейса**, а символьное устройство самого
Wi-Fi драйвера (например `/dev/wlan0`, libchardriver: ioctl на команды,
`read`/`select` на события). Данные остаются как сейчас: драйвер — обычный
`netdriver` для `lwip`, кадры Ethernet (802.11↔802.3 переводит драйвер).

ops:
- `init2`/`deinit`, `get_mac_addr` (MAC из драйвера), `get_capa`
  (`key_mgmt` WPA2_PSK [+ WPA2 для EAP позже], `enc` CCMP [+ TKIP, BIP],
  `auth` OPEN, `flags` = `CONTROL_PORT`, `flags2` = `CONTROL_PORT_RX`;
  **не** ставить `SME`, `4WAY_HANDSHAKE_*`);
- `scan2` (SSID для активного скана, частоты) и `get_scan_results2`
  (`struct wpa_scan_res` с BSSID, freq, caps, beacon_int, level и **IE
  целиком** — supplicant сверяет RSN IE маяка с тем, что пришёл в M3);
- `associate` (SSID, BSSID, freq, `wpa_ie` = готовый RSN IE от supplicant,
  auth_alg, pairwise/group/key_mgmt) — драйвер сам делает Open System auth,
  Association Request с этим IE, ждёт ответы, `peer_assoc`;
- `deauthenticate` (кадр deauth + сброс пира);
- `get_bssid`, `get_ssid`;
- `set_key` (alg CCMP/TKIP/BIP, key_idx, set_tx, seq, адрес; NONE —
  удалить) → `WMI vdev_install_key`;
- `set_supp_port` (порт 802.1X открыт/закрыт: до него драйвер пропускает
  только EtherType 0x888E);
- `tx_control_port` (EAPOL наружу **через тот же канал, что set_key**);
- события в `wpa_supplicant_event()`: `EVENT_SCAN_RESULTS`, `EVENT_ASSOC`
  (с `req_ies`/`resp_ies`), `EVENT_ASSOC_REJECT`, `EVENT_AUTH_TIMED_OUT`/
  `ASSOC_TIMED_OUT`, `EVENT_DISASSOC`/`EVENT_DEAUTH` (reason),
  `EVENT_EAPOL_RX`, `EVENT_MICHAEL_MIC_FAILURE` (только TKIP),
  `EVENT_INTERFACE_STATUS` по желанию.
  Ориентир по объёму — `driver_openbsd.c` (139 строк, но он почти пуст);
  реалистично 800–1200 строк.

**Почему EAPOL по control port, а не через BPF.** Supplicant шлёт M4 и
сразу зовёт `set_key`. Если M4 идёт `bpf → lwip → ndev → драйвер`, а ключ —
напрямую в драйвер, ключ может встать раньше, чем уйдёт M4, M4 уйдёт
зашифрованным, AP его выбросит (тот же класс гонки известен в Linux, отсюда
control port). Один канал — один порядок.

### l2_packet

- `l2_packet_freebsd.c` (342 строки): **libpcap** (`pcap_open_live`,
  `pcap_compile`/`setfilter` на `ether proto 0x888e`, `pcap_inject`,
  `pcap_next`, `pcap_findalldevs`), `ioctl(BIOCIMMEDIATE)`, `DLT_EN10MB`, а
  свой MAC — **`sysctl {CTL_NET, AF_ROUTE, 0, AF_LINK, NET_RT_IFLIST}`**
  (не `getifaddrs`). NetBSD линкует `-lpcap`.
- У MINIX: `/dev/bpf` у `lwip` умеет `BIOCIMMEDIATE, BIOCSETF, BIOCSDLT,
  BIOCSHDRCMPLT, BIOCPROMISC, …`, запись есть (`bpfdev_write`), DLT у
  ethif — `DLT_EN10MB`; `NET_RT_IFLIST` lwip отвечает (`rtsock.c:1906`);
  `getifaddrs` даёт `AF_LINK`. **Но libpcap для evbarm64 не собрана** — в
  DESTDIR только `pcap.h`, ни `libpcap.a`, ни `tcpdump` (reachover
  `external/bsd/libpcap` 1.7.2 в дереве есть, обмера на aarch64 не было).
  То есть путь, скорее всего, жизнеспособен, но стоит сборки libpcap и
  несёт гонку M4/ключа.
- `l2_packet_none.c` не годится как есть: `l2_packet_init` не копирует
  `own_addr`, а `wpa_supplicant_update_mac_addr()` берёт свой адрес именно
  из `l2_packet_get_own_addr()` — будет нулевой MAC.
- **Рекомендуемо:** свой `l2_packet_meecho.c` (~60 строк, в `minix/`):
  хранит `own_addr`, приём не открывает (`rx_callback == NULL` при
  control port), `l2_packet_send` → `tx_control_port` драйвера на случай
  pre-auth. Ни pcap, ни BPF не нужны.

## 3. Объём и зависимости сборки

Весь `dist` — 583 949 строк .c/.h; без `src/ap`, eap_server, fst, radius —
444 609. NetBSD-набор `wpa_supplicant` без EAP-методов и без driver_bsd/
wired/l2_packet_freebsd, но с `crypto_openssl.c`, `tls_none.c`, `eap.c`,
`eap_methods.c`, `eap_common.c`: **47 файлов .c, 90 737 строк** (заметная
часть под `#ifdef` и не компилируется). EAP-методы (`src/eap_peer/*.c`) —
ещё 28 303 строки, когда понадобится EAP.

Минимальная конфигурация (по reachover NetBSD, минус лишнее):
- `wpa_supplicant/`: bss, bssid_ignore, config, config_file, ctrl_iface,
  ctrl_iface_unix, eap_register, events, main, notify, op_classes, rrm,
  robust_av, scan, wmm_ac, wpa_supplicant, wpas_glue;
- `src/common`: ctrl_iface_common, hw_features_common, ieee802_11_common,
  ptksa_cache, wpa_common;
- `src/crypto`: crypto_openssl, random, sha1-prf, sha256, sha256-prf,
  tls_none (PSK не нужен TLS; для EAP-TLS/PEAP позже — tls_openssl + `-lssl`);
- `src/rsn_supp`: wpa, wpa_ie, pmksa_cache, preauth;
- `src/eapol_supp/eapol_supp_sm.c`, `src/eap_peer/{eap,eap_methods}.c`,
  `src/eap_common/eap_common.c` (держат дверь к EAP открытой; с
  `IEEE8021X_EAPOL` без единого метода);
- `src/utils`: base64, bitfield, common, config (→ `config_line.c`), eloop,
  os_unix, wpa_debug, wpabuf;
- `src/drivers/driver_common.c`; **`drivers.c` — свой** (см. §5);
- наши: `driver_meecho.c`, `l2_packet_meecho.c`.
- Флаги: `CONFIG_DRIVER_MEECHO` (наш), `CONFIG_CTRL_IFACE`,
  `CONFIG_CTRL_IFACE_UNIX`, `CONFIG_BACKEND_FILE`, `IEEE8021X_EAPOL`,
  `CONFIG_DEBUG_FILE`/`SYSLOG`; **без `CONFIG_ELOOP_KQUEUE`** (его ставит
  NetBSD-овский `bin/Makefile.inc`; kqueue в MEECHO нет) — по умолчанию
  `CONFIG_ELOOP_SELECT`, можно `CONFIG_ELOOP_POLL`.
- Библиотеки: `-lcrypto` (OpenSSL 3.5.7 в дереве; `crypto_openssl.c` 2.11
  знает 3.x, PBKDF2 для PSK — оттуда). wpa_cli и wpa_passphrase — отдельные
  мелкие программы того же reachover.

Места возможных пробелов MINIX, проверенные:
- **ctrl_iface** — `socket(PF_UNIX, SOCK_DGRAM)` + bind на путь
  (`ctrl_iface_unix.c:606,1215`); служба `uds` SOCK_DGRAM поддерживает
  (`minix/net/uds/io.c`). wpa_cli биндит клиентский сокет в `/tmp` —
  проверить на машине.
- **Время** — `os_get_reltime()` берёт `CLOCK_BOOTTIME`, иначе
  `CLOCK_MONOTONIC`; `CLOCK_BOOTTIME` в `sys/sys/time.h` нет, PM отвечает на
  `CLOCK_MONOTONIC` (`servers/pm/time.c`) — годится.
- **Случайность** — `os_get_random()` читает `/dev/urandom`, `random.c` —
  `/dev/random`; на CB2 есть TRNG и пул сеется (ssh этим живёт). На QEMU —
  `virtio-rng`.
- **eloop** — select/poll есть; `poll` у MEECHO отдаёт `POLLNVAL`.
- `os_daemon()` — `fork`; `mlock` не используется.
- Сборка GCC 15: NetBSD-current сам собирает 2.11 современным GCC
  (`COPTS.crypto_openssl.c += -Wno-error=deprecated-declarations`), ждать
  нужно немногих `CC_WNO_*`.
- dhcpcd 6.9.3 менять не надо: он увидит линк через `netdriver_link()`
  после `set_supp_port(1)`.

## 4. Альтернатива: порт net80211

NetBSD-current `sys/net80211` (21 310 строк; крупные:
`ieee80211_input.c` 3330, `ieee80211_ioctl.c` 2879, `ieee80211_node.c`
2569, `ieee80211_output.c` 2166, `ieee80211_proto.c` 1158, `ieee80211.c`
1115, `ieee80211_crypto_tkip.c` 951). Тянет: `struct mbuf` и `m_*` — 154
места в 9 файлах; `struct ifnet`/`ifp->` — 73 в 8; `sysctl` ядра — 99 в 9;
`ifmedia`, `IFQ_`, `splnet`/`mutex_`/`kmem_`, `softint`, `bpf_mtap`,
`rt_ieee80211msg`. Под это в процессе драйвера пришлось бы написать
эмуляцию mbuf, ifnet/ifmedia, таймеров, spl, sysctl — и всё равно провести
ioctl `SIOCS80211` и `RTM_IEEE80211` через `lwip` (сейчас `ENOTTY` и
`NDEV_IOCTL` «reserved»), то есть менять протокол `ndev`, `libnetdriver` и
`lwip`. И в итоге получить стек **2005 года без 802.11n/ac**: WCN3990
(11ac) работал бы на легаси-скоростях. Программная криптография
(`crypto_ccmp/tkip`) не нужна — шифрует прошивка.

Свой MLME в драйвере для WPA2-PSK STA: разбор beacon/probe-resp в таблицу
BSS (IE храним целиком), Open System auth, Assoc Request (SSID, rates, RSN
IE от supplicant, HT/VHT capabilities — их и так надо собрать для
`peer_assoc` WMI), разбор auth/assoc response, таймауты и повторы,
deauth/disassoc в обе стороны, порт 802.1X, перевод 802.11↔802.3. Оценка
**1–2 тыс. строк** нашего кода, и половина из них (rates, HT/VHT, peer
assoc) нужна драйверу ath10k при любом раскладе.

## 5. Рекомендация

**Импортировать hostap 2.11 из NetBSD-current как есть и написать к нему
свой бэкенд; net80211 не портировать.**

1. `external/bsd/wpa/dist` — нетронутая копия NetBSD-current (hostap 2.11,
   BSD-3), `external/bsd/wpa/bin/{wpa_supplicant,wpa_cli,wpa_passphrase}` —
   reachover NetBSD, переписанный под MEECHO: без `driver_bsd.c`,
   `driver_wired*.c`, `l2_packet_freebsd.c`, `-lpcap`, `CONFIG_ELOOP_KQUEUE`,
   EAP-методов (флаг на потом), с `-lcrypto`; диагностики — `CC_WNO_*` по
   файлу с комментарием. hostapd не собирать.
2. Наш код — в `minix/` (например `minix/usr.sbin/wpa/` или
   `minix/lib/libwpameecho/`): `driver_meecho.c`, `l2_packet_meecho.c` и
   **свой `drivers.c`** (таблица `wpa_drivers[]` с `wpa_driver_meecho_ops`)
   — dist-овский `drivers.c` перечисляет бэкенды по `#ifdef` и без правки
   нового не примет, а 53 строки таблицы проще держать своими, чем
   латать импорт. Reachover берёт их через `.PATH`. Объявление
   `extern const struct wpa_driver_ops wpa_driver_meecho_ops;` — в нашем
   файле, `driver.h` не трогаем.
3. Драйвер Wi-Fi (служба) = `netdriver` для данных + символьное
   устройство управления (scan/associate/set_key/control port/события) +
   маленький MLME. `lwip`, `ndev` и маршрутный сокет не меняются.
4. EAPOL — по control port (`tx_control_port` + `EVENT_EAPOL_RX`): нет
   зависимости от libpcap/BPF и нет гонки M4 и установки ключа.

Почему так: wpa_supplicant — та часть, где ошибки стоят безопасности
(4-way, group rekey, replay, PMKSA, позже SAE/EAP), её берём готовой и
обновляемой; net80211 не даёт ни 11n/ac, ни работающего пути ioctl в
архитектуре MEECHO, а кадры auth/assoc драйверу WCN3990 всё равно строить
самому — Linux тоже не отдаёт это прошивке.
