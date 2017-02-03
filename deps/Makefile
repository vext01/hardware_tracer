DIR != pwd

LIBIPT_SRC = ${DIR}/src/processor-trace
LIBIPT_BUILD = ${DIR}/build/libipt
LIBIPT_INST = ${DIR}/inst/libipt

PTDUMP_SRC = ${LIBIPT_SRC}/ptdump
PTDUMP_BUILD = ${DIR}/build/ptdump
PTDUMP_INST = ${DIR}/inst/ptdump

.PHONY: dirs

all: inst/libipt inst/ptdump

dirs:
	mkdir -p build inst

inst/libipt: dirs
	mkdir ${LIBIPT_BUILD}
	cd ${LIBIPT_BUILD} && \
		cmake -DCMAKE_INSTALL_PREFIX:PATH=${LIBIPT_INST} ${LIBIPT_SRC} && \
		make && make install

inst/ptdump: inst/libipt
	mkdir ${PTDUMP_BUILD}
	cd ${PTDUMP_BUILD} && \
		cmake ${PTDUMP_SRC} \
		    -DCMAKE_INSTALL_PREFIX:PATH=${PTDUMP_INST} \
		    -DCMAKE_C_FLAGS=-I${LIBIPT_INST}/include \
		    -DCMAKE_EXE_LINKER_FLAGS="-L${LIBIPT_INST}/lib -Wl,-rpath,${LIBIPT_INST}/lib" && \
		make VERBOSE=1 && \
		install -d ${PTDUMP_INST}/bin && \
		install ${PTDUMP_BUILD}/ptdump ${PTDUMP_INST}/bin/
