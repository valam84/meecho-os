# Разработка MEECHO на macOS

Нативное окружение проверено на Mac с Apple Silicon. Оно собирает отдельное
тестовое ядро `bringup` и запускает хостовые тесты. Полная сборка MEECHO
с пользовательскими программами пока использует Linux-скрипты из BUILDING.md;
их адаптация для macOS в эту настройку не входит.

## Инструменты

```sh
xcode-select --install  # только если инструменты Xcode ещё не установлены
brew install aarch64-elf-gcc qemu coreutils
```

Проверенные версии: GCC 16.2.0, binutils 2.47, QEMU 11.1.1,
Apple Clang/LLDB из Xcode. Компилятор `aarch64-elf-gcc` предназначен для
freestanding-ядра; он не заменяет `aarch64-elf64-minix-gcc` для пользовательской
части полной системы.

## Работа

Из корня репозитория:

```sh
bash port/macos-dev.sh build
bash port/macos-dev.sh test
bash port/macos-dev.sh run
bash port/macos-dev.sh run-el2
```

Выйти из QEMU: `Ctrl-A`, затем `x`. Тестовое ядро выполняет проверки и
замеры, затем печатает `nothing left to run, halting.`; оболочки входа в нём нет.
После этого QEMU остаётся запущенным до ручного выхода.

В VS Code доступны такие же задачи через **Terminal → Run Task**;
`Cmd-Shift-B` собирает тестовое ядро.

Результат: `minix/kernel/arch/aarch64/bringup/kernel.elf`.
Команды `disasm` и `clean` показывают дизассемблированный код и очищают сборку.

## Отладка

В первом терминале:

```sh
bash port/macos-dev.sh debug
```

Во втором, из корня проекта:

```sh
xcrun lldb minix/kernel/arch/aarch64/bringup/kernel.elf
```

Команды LLDB:

```text
gdb-remote 127.0.0.1:1234
breakpoint set --name kernel_main
continue
register read
```

Символы ядра имеют виртуальные адреса. До включения MMU процессор работает
по физическим адресам, поэтому на самом раннем старте используйте регистры
и дизассемблирование по текущему PC.

## Проверено

- Нативная сборка тестового ядра с `-Werror`.
- Запуск через QEMU с входом EL1 и EL2 до завершения тестовой программы.
- `cachectl`: 4 658 316 проверок, без ошибок.
- `dwmac`: проверки выводов и MAC-адреса проходят.
- `sdmmc`: 69 проверок карты и 50 проверок блочного слоя, без ошибок.
- Signal-frame FP/SIMD ABI и изменённые пути ядра компилируются целевым GCC.
- LLDB подключается к QEMU и останавливает ядро на точке останова `kernel_main`.

Репозиторий скачан с `--depth=1`, ветка `aarch64`. Если понадобится полная
история: `git fetch --unshallow`.
