DESCRIPTION = "Hello World application using cmake and remote source download"
SECTION = "examples"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "git://github.com/kiranuggina/hwcmake;protocol=git;rev=c7f3f78a2929864665c4e6560fc5460ac30b2c60"

S = "${WORKDIR}/git"

inherit cmake

EXTRA_OECMAKE = ""
