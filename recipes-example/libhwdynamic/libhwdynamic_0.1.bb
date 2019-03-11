DESCRIPTION = "A Simple Dynamic Shared Library Example"
SECTION = "libs"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://hwdynamic1.c	\
	file://hwdynamic2.c	\
	file://hwdynamic.h	\
	file://hwdynamic.pc"

S = "${WORKDIR}"

do_compile(){
	${CC} ${LDFLAGS} -fPIC -g -c hwdynamic1.c hwdynamic2.c
	${CC} ${LDFLAGS} -shared -fPIC -Wl,-soname,libhwdynamic.so.1 -o libhwdynamic.so.1.0 *.c
}

do_install(){
	install -d ${D}${includedir}
	install -d ${D}${libdir}
	install -m 0755 hwdynamic.h ${D}${includedir}
	install -m 0755 libhwdynamic.so.1.0 ${D}${libdir}
	ln -s libhwdynamic.so.1.0 ${D}/${libdir}/libhwdynamic.so.1
	ln -s libhwdynamic.so.1 ${D}/${libdir}/libhwdynamic.so
}
