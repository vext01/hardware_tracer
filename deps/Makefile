DIR != pwd
INST_DIR = ${DIR}/inst
PYTHON=python2.7

PROCESSOR_TRACE_REPO = https://github.com/01org/processor-trace.git
PROCESSOR_TRACE_V = 157be85a957188d22c2802d2067366c0ed9f1264
PROCESSOR_TRACE_SOURCE = processor-trace

XED_REPO = https://github.com/intelxed/xed
XED_V = 8922c2d21ec7110f0ccf33bbdad146d93222918b
XED_SOURCE = xed

MBUILD_REPO = https://github.com/intelxed/mbuild
MBUILD_V = 9eefb36a01167e56c4d58a924fbaf5ed9726bca8
MBUILD_SOURCE = mbuild

all: ${INST_DIR}/bin/ptdump

${INST_DIR}:
	install -d ${INST_DIR}/bin

#
# Fetch targets
#
${PROCESSOR_TRACE_SOURCE}:
	git clone ${PROCESSOR_TRACE_REPO}
	cd ${PROCESSOR_TRACE_SOURCE} && git checkout ${PROCESSOR_TRACE_V}

${XED_SOURCE}:
	git clone ${XED_REPO}
	cd ${XED_SOURCE} && git checkout ${XED_V}

${MBUILD_SOURCE}:
	git clone ${MBUILD_REPO}
	cd ${MBUILD_SOURCE} && git checkout ${MBUILD_V}

#
# Build targets
#
${PROCESSOR_TRACE_SOURCE}/bin/ptdump: ${PROCESSOR_TRACE_SOURCE} ${XED_SOURCE}/obj/libxed.so
	cd ${PROCESSOR_TRACE_SOURCE} && \
		env CFLAGS"=-I${DIR}/${XED_SOURCE}/include/public/xed -I${DIR}/${XED_SOURCE}/obj -Wno-error -g" \
		LDFLAGS="-L${DIR}/${XED_SOURCE}/obj -Wl,-rpath=${DIR}/${XED_SOURCE}/obj" \
		cmake -DCMAKE_INSTALL_PREFIX:PATH=${INST_DIR} \
		-DPTDUMP=ON -DPTXED=ON -DSIDEBAND=ON -DFEATURE_ELF=ON -DDEVBUILD=ON . && ${MAKE}

${XED_SOURCE}/obj/libxed.so: ${XED_SOURCE} ${MBUILD_SOURCE}
	cd ${XED_SOURCE} && ${PYTHON} mfile.py --shared

#
# Install targets
#
${INST_DIR}/bin/ptdump: ${INST_DIR} ${PROCESSOR_TRACE_SOURCE}/bin/ptdump
	cd ${PROCESSOR_TRACE_SOURCE} && ${MAKE} install
	install ${PROCESSOR_TRACE_SOURCE}/bin/ptdump ${INST_DIR}/bin/
	install ${PROCESSOR_TRACE_SOURCE}/bin/ptxed ${INST_DIR}/bin/

clean:
	rm -rf ${INST_DIR} ${PROCESSOR_TRACE_SOURCE} ${XED_SOURCE} ${MBUILD_SOURCE}
