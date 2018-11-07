DESCRIPTION = "A Simple Example with Application with Dynamic Shared Library Linking." 
SECTION = "examples"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

DEPENDS = "libhwdynamic"

SRC_URI = "file://hellodynamic.c"

S = "${WORKDIR}"

do_compile() {
	${CC} ${LDFLAGS} -o hellodynamic hellodynamic.c -lhwdynamic
}

do_install() {
	install -d ${D}${bindir}
	install -m 0755 hellodynamic ${D}${bindir}
}
