#! /bin/bash

SUBSYS_NAME=multicore2c
NCORES=2
test_args=
BIN=
XTCORE=core0

# This is required only for standalone "default" regression run.
test_num=$3

echo "usage:<this_script> NCORES <2(def)|4> <xt-run xa_af_...test -infile:... -outfile:... -C: -D: ...>"
for i in "$@"
do
  case ${i} in
    "xt-run")
        if [ "${BIN}" == "" ];then
            BIN=${2}
        fi
    ;;
    NCORES)
        NCORES=${2}
        echo "NCORES:$NCORES"
        shift 2
        ;;
    SUBSYS)
        SUBSYS_NAME=${2}
        echo "SUBSYS_NAME:$SUBSYS_NAME"
        shift 2
        ;;
    XTCORE)
        XTCORE=${2}
        echo "XTENSA_CORE:$XTCORE"
        shift 2
        ;;
    *)
        arg=${i}
        if [ "${BIN}" != "${i}" ];then
          if [ -n "$test_args" ];then
            if [ $NCORES == 1 ];then
              test_args="${test_args} ${arg}"
            else
              test_args="${test_args},${arg}"
            fi
          else
              test_args="${arg}"
          fi
        fi
    ;;
  esac
done

#remove everything before the 1st -infile regex
test_args=$(echo ${test_args} | awk --re-interval 'match($0,/-infile|-samples|-outfile/){print substr($0,RSTART)}')

# This is required only for standalone "default" regression run.
re='^[0-9]+$'
if [[ $test_num =~ $re ]] ; then 
    if [ $NCORES == 1 ]; then
        test_args="$test_num $test_args";
    else
        test_args=$test_num,$test_args;
    fi
fi

case $NCORES in
    4)
        CORE_DEFINES="--define=core0_BINARY=${BIN}_core0 \
        --define=core0_BINARY_ARGS=${test_args} \
        --define=core1_BINARY=${BIN}_core1 \
        --define=core2_BINARY=${BIN}_core2 \
        --define=core3_BINARY=${BIN}_core3"

        SUBSYS_NAME=multicore4c
    ;;
    3)
        CORE_DEFINES="--define=core0_BINARY=${BIN}_core0 \
        --define=core0_BINARY_ARGS=${test_args} \
        --define=core1_BINARY=${BIN}_core1 \
        --define=core2_BINARY=${BIN}_core2"

        SUBSYS_NAME=multicore3c
    ;;
    2)
        CORE_DEFINES="--define=core0_BINARY=${BIN}_core0 \
        --define=core0_BINARY_ARGS=${test_args} \
        --define=core1_BINARY=${BIN}_core1"

        SUBSYS_NAME=multicore2c
    ;;
    1)
        CORE_DEFINES="${BIN}_core0"
    ;;
    *)
        echo "NCORES:${NCORES} 1 or 2 or 3 or 4"
    ;;
esac

#export XTSC_TEXTLOGGER_CONFIG_FILE="TextLogger.txt"

if [ $NCORES == 1 ]; then
    cmd="xt-run \
        ${BIN}_${XTCORE}  \
        ${test_args} \
        "
else 
cmd="xtsc-run \
    ${CORE_DEFINES} \
    --define=XTSC_LOG=0 \
    --include=../../xtsc/sysbuilder/xtsc-run/${SUBSYS_NAME}.inc\
    "
fi

echo ${cmd}
${cmd}

