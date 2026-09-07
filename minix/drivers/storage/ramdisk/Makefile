# Makefile for ramdisk image
.include <bsd.own.mk>

USE_BITCODE:=no

install:

all:

# Add a few defines we are going to use during the image
# creation to determine what features and binaries to include
# in the final image
# ACPI            do we have/include the acpi binary
# RAMDISK_SMALL   is the script called with MKSMALL=yes
# DYNAMIC         does the ramdisk contain dynamic binaries?
RAMDISK_INC_ACPI=	0
RAMDISK_SMALL=		0
RAMDISK_DYNAMIC=	0

RAMDISK_DEFINES= \
	-DRAMDISK_INC_ACPI=${RAMDISK_INC_ACPI} \
	-DRAMDISK_SMALL=${RAMDISK_SMALL} \
	-DRAMDISK_DYNAMIC=${RAMDISK_DYNAMIC}

# The name of the proto file to use
PROTO=			proto

# Common to all architectures
ETC=			system.conf group
EXTRA=			rc
PROTO_FILES=		proto.common.etc
PROTO_FILES+=		proto.common.dynamic
PROGRAMS=		# defined
PROGRAMS+=		fsck_mfs
dir.fsck_mfs:=	minix/commands/fsck.mfs
PROGRAMS+=		grep
dir.grep:=	minix/usr.bin/grep
PROGRAMS+=		input
dir.input:=	minix/servers/input
PROGRAMS+=		loadramdisk
dir.loadramdisk:=	minix/commands/loadramdisk
PROGRAMS+=		mfs
dir.mfs:=	minix/fs/mfs
PROGRAMS+=		mount
dir.mount:=	minix/commands/mount
PROGRAMS+=		procfs
dir.procfs:=	minix/fs/procfs
PROGRAMS+=		minix-service
dir.minix-service:=	minix/commands/minix-service
PROGRAMS+=		sh
dir.sh:=	bin/sh
PROGRAMS+=		sysenv
dir.sysenv:=	minix/commands/sysenv
PROGRAMS+=		umount
dir.umount:=	minix/commands/umount

.if ${MKSMALL} != "yes"
RAMDISK_SMALL=		1
.endif

.if ${MACHINE_ARCH} == "i386"
ETC+=			rs.single
PROGRAMS+=		at_wini
dir.at_wini:=	minix/drivers/storage/at_wini
PROGRAMS+=		floppy
dir.floppy:=	minix/drivers/storage/floppy
PROGRAMS+=		pci
dir.pci:=	minix/drivers/bus/pci
PROGRAMS+=		pckbd
dir.pckbd:=	minix/drivers/hid/pckbd
PROGRAMS+=		cdprobe
dir.cdprobe:=	minix/commands/cdprobe
PROGRAMS+=		pwd_mkdb
dir.pwd_mkdb:=	usr.sbin/pwd_mkdb
PROGRAMS+=		isofs
dir.isofs:=	minix/fs/isofs

.if ${MKSMALL} != "yes"
PROGRAMS+=		ahci
dir.ahci:=	minix/drivers/storage/ahci
PROGRAMS+=		virtio_blk
dir.virtio_blk:= minix/drivers/storage/virtio_blk
PROGRAMS+=		ext2
dir.ext2:=	minix/fs/ext2
.endif

.if ${MKACPI} != "no"
RAMDISK_INC_ACPI= 1
PROGRAMS+=		acpi
dir.acpi:=	minix/drivers/power/acpi
.endif
.endif # ${MACHINE_ARCH} == "i386"

.if ${MACHINE_ARCH} == "earm"
PROGRAMS+=		mmc
dir.mmc:=	minix/drivers/storage/mmc
.endif # ${MACHINE_ARCH} == "earm"

.if ${MACHINE_ARCH} == "aarch64"
# On aarch64 this image is not a stepping stone to a disk: it is the root
# filesystem the system runs on, mounted by VFS from the memory driver
# (bootramdisk=1).  So it has to be able to offer a login, which the other
# architectures get from the real root they switch to.
ETC+=			gettytab
ETC+=			motd
EXTRA+=			ttys
PROGRAMS+=		getty
dir.getty:=	libexec/getty
PROGRAMS+=		login
dir.login:=	usr.bin/login
# The shell that login leads to has nothing to look at without these two.
PROGRAMS+=		cat
dir.cat:=	bin/cat
PROGRAMS+=		ls
dir.ls:=	bin/ls
# And no way to stop the machine without these. Not a convenience: the
# kernel's shutdown path - stopping the secondary cores among it - is only
# reachable through them, so without them it is code that cannot be run.
PROGRAMS+=		reboot
dir.reboot:=	sbin/reboot
PROGRAMS+=		shutdown
dir.shutdown:=	sbin/shutdown
# The disk drivers, for a root that is not this image.  Two machines and two
# parts: on QEMU's virt the disk is a virtio device, a "virtio,mmio" node of
# the device tree; on the CB2 it is the eMMC behind an SD host controller.
# Each driver looks for its own node and fails when there is none, so rc can
# simply try both and let the tree decide.
PROGRAMS+=		virtio_blk
dir.virtio_blk:= minix/drivers/storage/virtio_blk
PROGRAMS+=		sdmmc
dir.sdmmc:=	minix/drivers/storage/mmc
# The real-time clock: the driver that finds it in the device tree, the
# command that copies it into the system time, and date for the machine
# that has no clock, so that the time never stays at the epoch.
PROGRAMS+=		readclock.drv
dir.readclock.drv:= minix/drivers/clock/readclock
PROGRAMS+=		readclock
dir.readclock:=	minix/commands/readclock
PROGRAMS+=		date
dir.date:=	bin/date
# The way to ask a disk for a flush or a discard from the shell, which is
# how the two are exercised on a machine that has nothing else asking yet.
PROGRAMS+=		diskctl
dir.diskctl:=	minix/usr.sbin/diskctl
# Enough of a tool set to make and unmake a directory tree, which is what
# a file system is for and what its directory code needs to be tried with.
PROGRAMS+=		mkdir
dir.mkdir:=	bin/mkdir
PROGRAMS+=		rm
dir.rm:=	bin/rm
PROGRAMS+=		rmdir
dir.rmdir:=	bin/rmdir
PROGRAMS+=		ln
dir.ln:=	bin/ln
PROGRAMS+=		mv
dir.mv:=	bin/mv
PROGRAMS+=		cp
dir.cp:=	bin/cp
PROGRAMS+=		sync
dir.sync:=	bin/sync
# A filesystem that is only ever read can be sized to its contents exactly,
# and that is what mkfs.mfs does by default.  This one is written to - utmpx,
# wtmpx, lastlogx, /tmp - so it needs free inodes and free blocks, or the
# first login says "Out of i-nodes" three times.
MKFSMFS_FLAGS=		-x 25
.endif # ${MACHINE_ARCH} == "aarch64"

.if ${LDSTATIC} == "-dynamic"
RAMDISK_DYNAMIC=	 1
PROGRAMS+=		ld.elf_so
PROG_LIBS+=		libc
PROG_MINIXLIBS+=	libterminfo
.endif

CPPFLAGS+= -I${NETBSDSRCDIR}/servers

# LSC We have to take care of not erasing the source file, so never add EXTRA
# to CLEANFILES
CLEANFILES += ${PROGRAMS} ${SCRIPTS} ${ETC} image image.c
CLEANFILES += proto.gen proto.dev proto.dev.mtree
CLEANFILES += ${PROG_LIBEXEC}
CLEANFILES += ${.OBJDIR}/etc/*

#############################################################
# LSC Below this point the rules should not be modified
#############################################################

# Tool to bootstrap the password db
TOOL_PWD_MKDB?= ${NETBSDSRCDIR}/usr.sbin/pwd_mkdb/pwd_mkdb

# Remove "drivers/storage/ramdisk" component from path
PROGROOT:= ${.OBJDIR:S,/minix/drivers/storage/ramdisk,,}

# Generate dependencies rules for config files
.for etc in ${ETC}
etc/${etc}: ${NETBSDSRCDIR}/etc/${etc}
	mkdir -p ${.OBJDIR}/etc
	${INSTALL} $> $@
.endfor

# LSC Force the regeneration of the proto file as it seems sometimes 
# they are not copied over as needed.
# LSC ramdisk rc file != /etc/rc
# BJG if ${.CURDIR} == ${.OBJDIR}, we are building in-tree and install
# shouldn't try to install the originals over the originals. Otherwise
# we are building out-of-tree and the contents should be copied
# unconditionally in case the contents have changed after the first copy.
.if ${.CURDIR} != ${.OBJDIR}
.for file in ${EXTRA} ${PROTO} ${PROTO_FILES}
CLEANFILES += ${.OBJDIR}/${file}
${file}: ${NETBSDSRCDIR}/minix/drivers/storage/ramdisk/${file} .PHONY
	${INSTALL} $> $@
.endfor
.endif

# Generate dependencies rules for dynamic libraries, if needed
.for lib in ${PROG_LIBS}
PROGRAMS+= ${lib}.so.0
CLEANFILES += ${lib}.so.0
${lib}.so.0:${PROGROOT}/lib/${lib}/${lib}.so.0
	${INSTALL} $> $@

# BJG - Don't invoke parallel Makes
#${PROGROOT}/lib/${lib}/${lib}.so.0:
#	${MAKE} -C ${NETBSDSRCDIR}/lib/${lib} all
.endfor

.for lib in ${PROG_MINIXLIBS}
PROGRAMS+= ${lib}.so.0
CLEANFILES += ${lib}.so.0
${lib}.so.0:${PROGROOT}/minix/lib/${lib}/${lib}.so.0
	${INSTALL} $> $@

# BJG - Don't invoke parallel Makes
#${PROGROOT}/minix/lib/${lib}/${lib}.so.0:
#	${MAKE} -C ${NETBSDSRCDIR}/minix/lib/${lib} all
.endfor

# Generate dependencies rules for binaries
.for prog in ${PROGRAMS}
${prog}: ${PROGROOT}/${dir.${prog}}/${prog}
	${INSTALL} $> $@

# BJG - don't invoke parallel Makes
#${PROGROOT}/${dir.${prog}}/${prog}:
#	${MAKE} -C ${NETBSDSRCDIR}/${dir.${prog}} all
.endfor # prog

realall image:	proto.gen ${ETC:C/^/etc\//} ${EXTRA} \
		etc/master.passwd etc/pwd.db etc/spwd.db etc/passwd etc/group
	${_MKMSG_CREATE} "Generating ramdisk image"
	${TOOL_MKFSMFS} ${MKFSMFS_FLAGS} image proto.gen || { rm -f image; false; }

etc/pwd.db etc/spwd.db etc/passwd: etc/master.passwd

etc/master.passwd: ${NETBSDSRCDIR}/etc/master.passwd
	mkdir -p ${.OBJDIR}/etc
	${INSTALL} $> $@
	${TOOL_PWD_MKDB} -V 0 -p -d . etc/master.passwd

proto.dev.mtree:
	@echo ". type=dir uname=root gname=wheel mode=0755" \
		>${.TARGET}.tmp && \
	echo "./dev type=dir uname=root gname=wheel mode=0755" \
		>>${.TARGET}.tmp && \
	${NETBSDSRCDIR}/minix/commands/MAKEDEV/MAKEDEV.sh -m -r \
		>> ${.TARGET}.tmp && mv ${.TARGET}.tmp ${.TARGET}

# We have to remove the two first entries of the generated proto file, as
# well as the two last ones (closing $).
# Do not forget to double $ so that make doesn't try to expand it.
proto.dev: proto.dev.mtree etc/pwd.db etc/spwd.db etc/passwd etc/group
	${TOOL_MTREE} -f ${.TARGET}.mtree -N ${.OBJDIR}/etc -C -K device | \
		${TOOL_TOPROTO} | ${TOOL_SED} -e '1,4d' | \
		${TOOL_SED} -e '$$d' |${TOOL_SED} -e '$$d' > ${.TARGET}.tmp
	grep console ${.TARGET}.tmp	# sanity check; is there a console entry?
	mv ${.TARGET}.tmp ${.TARGET}

proto.gen: ${PROTO} ${PROTO_FILES} proto.dev ${PROGRAMS}
	${STRIP} ${PROGRAMS}
	# We are using the c preprocessor to generate proto.gen
	# used in the mkfs tool.
	${TOOL_CAT} ${PROTO} | ${CC} \
	    	${RAMDISK_DEFINES} \
		-E - | grep -v "^$$" | grep -v "#" >${.TARGET}

.include <minix.service.mk>
