DIR != pwd
INST_DIR = ${DIR}/inst


PROCESSOR_TRACE_REPO = https://github.com/01org/processor-trace.git
PROCESSOR_TRACE_V = 157be85a957188d22c2802d2067366c0ed9f1264
PROCESSOR_TRACE_SOURCE = ${DIR}/processor-trace

all: ${INST_DIR}/bin/ptdump

${INST_DIR}:
	install -d ${INST_DIR}/bin

#
# Fetch targets
#
${PROCESSOR_TRACE_SOURCE}:
	git clone ${PROCESSOR_TRACE_REPO}
	cd ${PROCESSOR_TRACE_SOURCE} && git checkout ${PROCESSOR_TRACE_V}

#
# Build targets
#
# -DPTXED=ON
${PROCESSOR_TRACE_SOURCE}/bin/ptdump: ${PROCESSOR_TRACE_SOURCE}
	cd ${PROCESSOR_TRACE_SOURCE} && \
		cmake -DCMAKE_INSTALL_PREFIX:PATH=${INST_DIR} -DPTDUMP=ON . && ${MAKE}

#
# Install targets
#
${INST_DIR}/bin/ptdump: ${INST_DIR} ${PROCESSOR_TRACE_SOURCE}/bin/ptdump
	cd ${PROCESSOR_TRACE_SOURCE} && ${MAKE} install
	install ${PROCESSOR_TRACE_SOURCE}/bin/ptdump ${INST_DIR}/bin/

clean:
	rm -rf ${INST_DIR} ${PROCESSOR_TRACE_SOURCE}
