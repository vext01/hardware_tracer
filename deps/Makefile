DIR != pwd
PATCHES = ${DIR}/patches

LIBIPT_SRC = ${DIR}/src/processor-trace
LIBIPT_BUILD = ${DIR}/build/libipt
LIBIPT_INST = ${DIR}/inst/libipt

.PHONY: dirs

all: inst/libipt

dirs:
	mkdir -p build inst

inst/libipt: dirs
	mkdir ${LIBIPT_BUILD}
	cd ${LIBIPT_BUILD} && \
		cmake -DCMAKE_INSTALL_PREFIX:PATH=${LIBIPT_INST} -DPTDUMP=ON \
		${LIBIPT_SRC} && make && make install
	install -d ${LIBIPT_INST}/bin && \
	install ${LIBIPT_BUILD}/bin/ptdump ${LIBIPT_INST}/bin/
