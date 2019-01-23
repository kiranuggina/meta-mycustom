DESCRIPTION = "A Simple Example for testing Scull Driver"
SECTION = "examples"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://test.c"

S = "${WORKDIR}"

do_compile(){
	${CC} ${LDFLAGS} -o testscull test.c
}

do_install(){
	install -d ${D}${bindir}
	install -d ${D}${datadir}/kiran
	install -m 0755 testscull ${D}${bindir}
	install -m 0755 testscull ${D}${datadir}/kiran
}

FILES_${PN} += "${datadir}/kiran"
