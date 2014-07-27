PROG=uhidvkbdd
SRCS=uhidvkbdd.c uhid.c
MAN=uhidvkbdd.8

LDADD+= -lusbhid

SCRIPTS=rc.sh
SCRIPTSNAME_rc.sh=uhidvkbdd
SCRIPTSDIR_rc.sh=${PREFIX}/etc/rc.d

FILES=devd.conf
FILESNAME_devd.conf=uhidvkbdd.conf
FILESDIR_devd.conf=${PREFIX}/etc/devd

CLEANFILES+= rc.sh devd.conf
rc.sh devd.conf: ${@:S/$/.in/}
	sed 's|%%PREFIX%%|${PREFIX}|g' $> >$@

PREFIX?=/usr/local
BINDIR=${PREFIX}/sbin
MANDIR=${PREFIX}/man/man

.include <bsd.prog.mk>
