# Справки по Wi-Fi WCN3990 (к `../hw-wifi.md`)

Разбор протокола ath10k_snoc по исходникам Linux, сделанный 2026-10-19:
номера, раскладки, порядок — словами и таблицами, без текста кода (Linux —
GPL, дерево — BSD). Ссылки `файл:строка` — на ветку `sdm660-mainline`
`72bc4bb` (та же, на которой построено дерево MEECHO для clover).

| Файл | О чём |
|---|---|
| `wlfw.md` | QMI (заголовок, TLV, массивы), сообщения службы wlfw байт в байт, порядок ath10k, hyp-assign памяти MSA (SMC), QRTR для клиента |
| `ce.md` | copy engine: регистры, дескриптор, RRI, теневые регистры, таблицы труб, что должна уметь модель |
| `htc-wmi.md` | файлы прошивки, HTC, HTT при старте, WMI-TLV до READY, карта служб эталона |
| `sta.md` | клиент: интерфейс, скан, подключение, ключи, путь данных, минимальный набор команд |
| `wpa.md` | wpa_supplicant против net80211, что нужно бэкенду, объём, решение |

Пути вида `~/wifi-ref/ath10k/X` — копия для чтения вне дерева; сделать её
заново (в WSL, клон `~/sdm660-linux` частичный, `blob:none`):

```bash
mkdir -p ~/wifi-ref/ath10k && cd ~/sdm660-linux && git show HEAD:drivers/net/wireless/ath/ath10k/snoc.c > ~/wifi-ref/ath10k/snoc.c
```

— и так же для `ce.[ch]`, `qmi*.[ch]`, `htc.[ch]`, `htt*.[ch]`, `hw.[ch]`,
`wmi-tlv.[ch]`, `wmi.h`, `wmi-ops.h`, `core.[ch]`, `mac.c`, `txrx.c`,
`rx_desc.h`, `targaddrs.h`; в `~/wifi-ref/soc/` — `drivers/soc/qcom/
qmi_{interface,encdec}.c`, `include/linux/soc/qcom/qmi.h`,
`include/uapi/linux/qrtr.h`, `net/qrtr/{af_qrtr,ns}.c`.

Поправка к `ce.md`: теневой регистр write index — это `0x32000 + 4·ce_id`
для любого CE; «расхождение с порядком в списке, отданном прошивке», о
котором там сказано, — не противоречие: прошивка сопоставляет по `ce_id`,
а не по номеру в списке.

Помечено «по памяти» — то, что зависит от файлов, которых в копии не было
(`wmi.c`, `debug.c`, `hif.h`): сверить, когда дойдёт до кода. Сумма
`crc32_le(0, …)` из `debug.c` уже сверена с эталоном (`../hw-wifi.md`, §2).
