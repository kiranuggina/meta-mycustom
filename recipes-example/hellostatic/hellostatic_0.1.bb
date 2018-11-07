DESCRIPTION = "A Simple Example with Application with Static Library Linking." 
SECTION = "examples"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

DEPENDS = "libhwstatic"

SRC_URI = "file://hellostatic.c"

S = "${WORKDIR}"

do_compile() {
	${CC} ${LDFLAGS} -o hellostatic hellostatic.c -lhwstatic
}

do_install() {
	install -d ${D}${bindir}
	install -m 0755 hellostatic ${D}${bindir}
}
