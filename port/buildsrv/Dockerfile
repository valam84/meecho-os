# Сборочное окружение пакетов MEECHO на сервере сборки.
#
# Та же Ubuntu, что в WSL рабочей машины (26.04): тулчейн
# aarch64-elf64-minix собирается из её пакетов gcc-15-source и
# binutils-source (port/build-xtools.sh), а собранный в WSL тулчейн
# слинкован с её glibc 2.43 - на хосте сервера (24.04, glibc 2.39) он не
# запустится. Контейнер, а не установка на хост: сервер общий, на нём
# живут чужие службы.
#
# Пользователь внутри - minix с uid/gid владельца каталога сборки на
# хосте, домашний каталог /home/minix смонтирован из него: пути
# port/pkg/env.sh и build-xtools.sh (~/xtools-aarch64, ~/dest-evbarm64,
# ~/obj-evbarm64/pkg) остаются ровно такими, как в WSL.
FROM ubuntu:26.04

ARG UID=1001
ARG GID=1001

RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y \
	--no-install-recommends \
	build-essential ca-certificates curl git python3 file \
	xz-utils bzip2 zstd rsync bison flex gawk pkg-config \
	gcc-15-source binutils-source \
	libgmp-dev libmpfr-dev libmpc-dev libisl-dev libzstd-dev zlib1g-dev \
	&& rm -rf /var/lib/apt/lists/*

# В образах Ubuntu с 23.04 есть пользователь ubuntu с uid 1000 - убрать,
# чтобы uid владельца каталога сборки был свободен при любом значении.
RUN (userdel -r ubuntu 2>/dev/null || true) \
	&& groupadd -g "$GID" minix \
	&& useradd -m -u "$UID" -g "$GID" -s /bin/bash minix

USER minix
WORKDIR /home/minix
