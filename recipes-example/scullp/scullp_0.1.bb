DESCRIPTION = "A Simple Scull Driver Example"
SECTION = "drivers"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

inherit module

SRC_URI = "file://scullp.c \
	file://scull.h \
	file://Makefile \
"

S = "${WORKDIR}"

#do_install(){
#	install -d ${D}${datadir}/kiran
#	install -m 0755 scullp.ko ${D}${datadir}/kiran
#}

#FILES_${PN} = "${datadir}/kiran"
