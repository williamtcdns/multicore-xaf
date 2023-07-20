======================================================================

This readme file describes instructions to build TensorFlow Lite For
Micro (TFLM) code for your HiFi core and use it for building and 
running TFLM applications under XAF. It also describes steps to add a 
new TFLM network as XAF component.

======================================================================
Xtensa tools version and Xtensa config requirements
======================================================================

RI-2021.6 Tools (Xplorer 8.0.16)
Xtensa core must be using xclib (required for TFLM compilation)
xt-clang, xt-clang++ compiler (required for TFLM compilation)

======================================================================
Building TFLM code for xtensa target on Linux
======================================================================
Note: TFLM build requires the following GNU tools
    The versions recommended are the ones on which it is tested
    GNU Make v3.82
    git v2.9.5
    wget v1.14
    curl v7.29

This section describes how to build the TFLM library to be used with 
XAF. Note that the TFLM compilation is only supported under Linux.

1.Copy /build/getTFLM.sh from XAF Package to the directory of choice 
  outside XAF Package under Linux environment. 
  This directory is referred to as <BASE_DIR> in the following steps.

2.Set up environment variables to have Xtensa Tools in $PATH and 
  $XTENSA_CORE(with xclib) defined to your HiFi core.

3.Execute getTFLM.sh <target>. This download and builds TFLM library in 
  <BASE_DIR/tensorflow>.  

  $ ./getTFLM.sh hifi3/hifi3z/hifi4/hifi5/fusion_f1

  Following libraries will be created in directory:
  For HiFi5 core:
  <BASE_DIR>/tensorflow/tensorflow/lite/micro/tools/make/gen/xtensa_hifi5_default/lib/
  For other cores: 
  <BASE_DIR>/tensorflow/tensorflow/lite/micro/tools/make/gen/xtensa_fusion_f1_default/lib/ 

  libtensorflow-microlite.a - TFLM Library
  libmicro_speech_frontend.a - Fronend lib for Microspeech Application

4.You can copy <BASE_DIR> directory from Linux to Windows for building 
  XAF Library and testbenches using Xplorer on Windows. In that case, 
  the destination directory on Windows is your new <BASE_DIR>.
  The <BASE_DIR> can be any path of user's choice where it is copied to.

======================================================================
Settings to be done in XWS package on Xplorer for all TFLM testbenches.
======================================================================
1.Changes to compile .cpp files
  Right click on file, select Build Properties, select Target as Common 
  in the new window that opens, select Language tab,and choose C++11 for 
  Language dialect

2.To add include paths, Right click on the testbench, select 
  Build Properties, Choose Target as Common, Under the include tab -> 
  include paths

======================================================================
Steps to run Person_detect application with XAF xws Package on Xplorer
======================================================================

Following are the steps to build and test person detect application 
with XAF xws package.

1.Component files list
	testxa_af_tflm_pd/test/plugins/cadence/tflm_common/tflm-inference-api.cpp
	testxa_af_tflm_pd/test/plugins/cadence/tflm_common/xa-tflm-inference-api.c
	testxa_af_tflm_pd/test/plugins/cadence/tflm_person_detect/person_detect_model_data.c
	testxa_af_tflm_pd/test/plugins/cadence/tflm_person_detect/person-detect-wrapper-api.cpp
		
2. *.cpp files list
	testxa_af_tflm_pd/test/plugins/cadence/tflm_common/tflm-inference-api.cpp
	testxa_af_tflm_pd/test/plugins/cadence/tflm_person_detect/person-detect-wrapper-api.cpp
		
3.test application file
	testxa_af_tflm_pd/test/src/xaf-tflite-person-detect-test.c

4.Add include paths
    Right click on 'testxa_af_tflm_pd' project->Build Properties ->
    Target:CommonTarget -> 'Include Paths' tab
	Add ${workspace_loc:testxa_af_tflm_pd/test/plugins/cadence/tflm_common}
	Add ${workspace_loc:testxa_af_tflm_pd/test/plugins/cadence/tflm_person_detect}
	Add <BASE_DIR>/tensorflow
	Add <BASE_DIR>/tensorflow/tensorflow/lite/micro/tools/make/downloads/flatbuffers/include

5.Add SYMBOLS
    Right click on 'testxa_af_tflm_pd' project->Build Properties ->
    Target:CommonTarget -> 'Symbols' tab
	Add XA_TFLM_PERSON_DETECT symbol with value 1
	Add TF_LITE_STATIC_MEMORY symbol

6.Link the TFLM libraries
    Right click on 'testxa_af_tflm_pd' project->Build Properties ->
    Target:CommonTarget -> 'Libraries' tab
	For hifi5 target, library path is: <BASE_DIR>/tensorflow/tensorflow/lite/micro/tools/make/gen/xtensa_hifi5_default/lib 
	For other targets, library path is: <BASE_DIR>/tensorflow/tensorflow/lite/micro/tools/make/gen/xtensa_fusion_f1_default/lib 
    under Library Search Paths(-L).
	Add tensorflow-microlite under Libraries(-l).

7.Additional linker option
    Right click on 'testxa_af_tflm_pd' project->Build Properties ->
    Target:CommonTarget -> 'Addl linker' tab
    Add --gc-sections under Additional linker options.

8.Arguments to run:
	Go to Run, select Run Configurations, 
    NCORES=1: 
        Select Xtensa Single Core Launch -> af_tflm_pd_cycle
        In Arguments tab->under Program Arguments
        Add "-infile:../testxa_af_hostless/test/test_inp/person_data.raw"
        Press Run
    NCORES=2: 
        Select 'MP Launch' -> bmap14_af_tflm_pd_2c
        Simulator Selection tab->under 'Subsystem Launch Options'
        Subsystem is appropriate project (here it is
        'multicore2c/MMap0/BMap14_af_tflm_pd')
        Press Run

======================================================================
Steps to run Capturer + micro_speech application with XAF xws Package on Xplorer
======================================================================

1.Component files list
	testxa_af_tflm_microspeech/test/plugins/cadence/capturer/xa-capturer.c
	testxa_af_tflm_microspeech/test/plugins/cadence/tflm_common/tflm-inference-api.cpp
	testxa_af_tflm_microspeech/test/plugins/cadence/tflm_common/xa-tflm-inference-api.c
	testxa_af_tflm_microspeech/test/plugins/cadence/tflm_microspeech/microspeech_model_data.c
	testxa_af_tflm_microspeech/test/plugins/cadence/tflm_microspeech/microspeech-frontend-wrapper-api.cpp
	testxa_af_tflm_microspeech/test/plugins/cadence/tflm_microspeech/microspeech-inference-wrapper-api.cpp
	testxa_af_tflm_microspeech/test/plugins/cadence/tflm_microspeech/xa-microspeech-frontend.c

2. *.cpp files list
	testxa_af_tflm_microspeech/test/plugins/cadence/tflm_common/tflm-inference-api.cpp
	testxa_af_tflm_microspeech/test/plugins/cadence/tflm_microspeech/microspeech-frontend-wrapper-api.cpp
	testxa_af_tflm_microspeech/test/plugins/cadence/tflm_microspeech/microspeech-inference-wrapper-api.cpp
		
3.test application file
	testxa_af_tflm_microspeech/test/src/xaf-capturer-tflite-microspeech-test.c

4.include paths
	${workspace_loc:testxa_af_tflm_microspeech/test/plugins/cadence/tflm_common}
	${workspace_loc:testxa_af_tflm_microspeech/test/plugins/cadence/tflm_microspeech}
	<BASE_DIR>/tensorflow
	<BASE_DIR>/tensorflow/tensorflow/lite/micro/tools/make/downloads/flatbuffers/include

5.SYMBOLS list
	XA_CAPTURER symbol with value 1
	XA_TFLM_MICROSPEECH symbol with value 1
	TF_LITE_STATIC_MEMORY symbol

6.TFLM libraries list
	For hifi5 target, library path is: <BASE_DIR>/tensorflow/tensorflow/lite/micro/tools/make/gen/xtensa_hifi5_default/lib 
	For other targets, library path is: <BASE_DIR>/tensorflow/tensorflow/lite/micro/tools/make/gen/xtensa_fusion_f1_default/lib 
	libraries: tensorflow-microlite, micro_speech_frontend

7.Additional linker option
    --gc-sections

8.arguments to run:
	(Copy the test/test_inp/capturer_in.pcm file to the 
    execution directory or testxa_af_tflm_microspeech/.. folder in this case
    before running the test)
    NCORES=1: 
        Select Xtensa Single Core Launch -> af_tflm_microspeech_cycle
        In Arguments tab->under Program Arguments
        Add "-outfile:../testxa_af_hostless/test/test_out/out_tflm_microspeech.pcm -samples:0"
        Press Run
    NCORES=2: 
        Select 'MP Launch' -> bmap8_af_tflm_microspeech_2c
        Simulator Selection tab->under 'Subsystem Launch Options'
        Subsystem is appropriate project (here it is
        'multicore2c/MMap0/BMap8_af_tflm_microspeech')
        Press Run

======================================================================
Steps to run Person_detect + micro_speech application with XAF xws Package on Xplorer
======================================================================

1.Component files list
	testxa_af_tflm_microspeech_pd/test/plugins/cadence/capturer/xa-capturer.c
	testxa_af_tflm_microspeech_pd/test/plugins/cadence/tflm_common/tflm-inference-api.cpp
	testxa_af_tflm_microspeech_pd/test/plugins/cadence/tflm_common/xa-tflm-inference-api.c
	testxa_af_tflm_microspeech_pd/test/plugins/cadence/tflm_microspeech/microspeech_model_data.c
	testxa_af_tflm_microspeech_pd/test/plugins/cadence/tflm_microspeech/microspeech-frontend-wrapper-api.cpp
	testxa_af_tflm_microspeech_pd/test/plugins/cadence/tflm_microspeech/microspeech-inference-wrapper-api.cpp
	testxa_af_tflm_microspeech_pd/test/plugins/cadence/tflm_microspeech/xa-microspeech-frontend.c
	testxa_af_tflm_microspeech_pd/test/plugins/cadence/tflm_person_detect/person_detect_model_data.c
	testxa_af_tflm_microspeech_pd/test/plugins/cadence/tflm_person_detect/person-detect-wrapper-api.cpp
		
2. *.cpp files list
	testxa_af_tflm_microspeech_pd/test/plugins/cadence/tflm_common/tflm-inference-api.cpp
	testxa_af_tflm_microspeech_pd/test/plugins/cadence/tflm_microspeech/microspeech-frontend-wrapper-api.cpp
	testxa_af_tflm_microspeech_pd/test/plugins/cadence/tflm_microspeech/microspeech-inference-wrapper-api.cpp
	testxa_af_tflm_microspeech_pd/test/plugins/cadence/tflm_person_detect/person-detect-wrapper-api.cpp
		
3.test application file
	testxa_af_tflm_microspeech_pd/test/src/xaf-person-detect-microspeech-test.c

4.include paths
	${workspace_loc:testxa_af_tflm_microspeech_pd/test/plugins/cadence/tflm_common}
	${workspace_loc:testxa_af_tflm_microspeech_pd/test/plugins/cadence/tflm_microspeech}
	${workspace_loc:testxa_af_tflm_microspeech_pd/test/plugins/cadence/tflm_person_detect}		
	<BASE_DIR>/tensorflow
	<BASE_DIR>/tensorflow/tensorflow/lite/micro/tools/make/downloads/flatbuffers/include

5.SYMBOLS list
	XA_CAPTURER symbol with value 1
	XA_TFLM_MICROSPEECH symbol with value 1
	XA_TFLM_PERSON_DETECT symbol with value 1
	TF_LITE_STATIC_MEMORY symbol

6.TFLM libraries list
	For hifi5 target, library path is: <BASE_DIR>/tensorflow/tensorflow/lite/micro/tools/make/gen/xtensa_hifi5_default/lib 
	For other targets, library path is: <BASE_DIR>/tensorflow/tensorflow/lite/micro/tools/make/gen/xtensa_fusion_f1_default/lib 
    libraries: tensorflow-microlite and micro_speech_frontend

7.Additional linker option
    --gc-sections

8.arguments to run:
	(Copy the test/test_inp/capturer_in.pcm file to the 
    execution directory or testxa_af_tflm_microspeech_pd folder in this case
    before running the test)
    NCORES=1: 
        Select Xtensa Single Core Launch -> af_tflm_microspeech_pd_cycle
        In Arguments tab->under Program Arguments
        Add "-infile:../testxa_af_hostless/test/test_inp/person_data.raw -outfile:../testxa_af_hostless/test/test_out/out_tflm_microspeech.pcm -samples:0"
        Press Run
    NCORES=2:
        Select 'MP Launch' -> bmap13_af_tflm_microspeech_pd_2c
        Simulator Selection tab->under 'Subsystem Launch Options'
        Subsystem is appropriate project (here it is
        'multicore2c/MMap0/BMap13_af_tflm_microspeech_pd')
        Press Run

======================================================================
Steps to add new TFLM network as XAF component
======================================================================

1. Add file for the new TFLM network in plugins directory as below:

    test/plugins/cadence/tflm_<ntwrk>/
    ├── <ntwrk>_model_data.c
    ├── <ntwrk>_model_data.h
    └── <ntwrk>-wrapper-api.cpp

   Model data files should contain network model in flatbuffer format.
   Wrapper file mainly contains network spec initialization code and
   person-detect-wrapper-api.cpp/ microspeech-inference-wrapper-api.cpp
   should be used as template for new network wrapper.

2. Add entry for new plugin in xf_component_id list in 
    test/plugins/xa-factory.c as below:

   { "post-proc/<ntwrk>_inference", xa_audio_codec_factory, xa_<ntwrk>_inference },

3. Now, XAF component for the new TFLM network can be created, 
   connected to other components and executed from XAF application.

======================================================================
Steps for Multicore RUN (NCORES>1)
======================================================================
Refer to Chapter 4 in "HiFi-AF-Hostless-ProgrammersGuide" to create 
multiple test projects according to number of cores in the project and 
build the binaries. Refer Step #8 above with NCORES=2 for example 
RUN launches.
======================================================================
