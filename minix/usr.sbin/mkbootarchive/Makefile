PROG=	mkbootarchive
MAN=	mkbootarchive.8

# The format lives in one header, shared with the kernel that reads it. This
# program is built twice - for the target here, and as a host tool by
# tools/mkbootarchive - and pointing at the source tree is the one spelling
# of the include that works in both.
CPPFLAGS+=	-I${NETBSDSRCDIR}/minix/include/arch/aarch64/include

.include <bsd.prog.mk>
