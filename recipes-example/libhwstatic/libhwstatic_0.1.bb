DESCRIPTION = "An Example for describing Static Library Example"
SECTION = "libs"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://hwstatic1.c	\
	file://hwstatic2.c	\
	file://hwstatic.h	\
	file://hwstatic.pc"

S = "${WORKDIR}"

do_compile(){
	${CC} ${LDFLAGS} -c hwstatic1.c hwstatic2.c
	${AR} -cvq libhwstatic.a *.o
}

do_install(){
	install -d ${D}${includedir}
	install -d ${D}${libdir}
	install -m 0755 hwstatic.h ${D}${includedir}
	install -m 0755 libhwstatic.a ${D}${libdir}
}
