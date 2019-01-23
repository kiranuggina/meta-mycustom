DESCRIPTION = "A Simple Hiredis Example Application Example"
SECTION = "examples"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"
DEPENDS = "hiredis"

SRC_URI = "file://hiredisexamp.c"

S = "${WORKDIR}"

do_compile(){
	${CC} ${LDFLAGS} -o hiredisexamp hiredisexamp.c -lhiredis
}

do_install(){
	install -d ${D}${bindir}
	install -m 0755 hiredisexamp ${D}${bindir}
}
