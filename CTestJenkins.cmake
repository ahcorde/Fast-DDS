set(CTEST_SOURCE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
set(CTEST_BINARY_DIRECTORY "${CTEST_SOURCE_DIRECTORY}/build/${JENKINS_GENERATOR}/${JENKINS_BUILD_CONFIGURATION}")
set(CTEST_TEST_TIMEOUT 60)

site_name(CTEST_SITE)
set(CTEST_BUILD_NAME "${JENKINS_BUILD_NAME}")
set(CTEST_CMAKE_GENERATOR "${JENKINS_GENERATOR}")
set(CTEST_BUILD_OPTIONS "${JENKINS_BUILD_OPTIONS}")
set(CTEST_BUILD_CONFIGURATION "${JENKINS_BUILD_CONFIGURATION}")

set(CTEST_COVERAGE_CXX_FLAGS "-DCMAKE_CXX_FLAGS:STRING=-fprofile-arcs -ftest-coverage")
set(CTEST_COVERAGE_EXE_LD_FLAGS "-DCMAKE_EXE_LINKER_FLAGS:STRING=-fprofile-arcs -ftest-coverage")
set(CTEST_COVERAGE_SHARED_LD_FLAGS "-DCMAKE_SHARED_LINKER_FLAGS:STRING=-fprofile-arcs -ftest-coverage")

ctest_empty_binary_directory(${CTEST_BINARY_DIRECTORY})

if(UNIX)
    find_program(CTEST_COVERAGE_COMMAND NAMES gcov)
    find_program(CTEST_MEMORYCHECK_COMMAND NAMES valgrind)
endif()

set(CTEST_CONFIGURE_COMMAND "${CMAKE_COMMAND}")
set(CTEST_CONFIGURE_COMMAND "${CTEST_CONFIGURE_COMMAND} ${CTEST_BUILD_OPTIONS}")
if(CTEST_COVERAGE_COMMAND)
    set(CTEST_CONFIGURE_COMMAND "${CTEST_CONFIGURE_COMMAND} ${CTEST_COVERAGE_CXX_FLAGS}")
    set(CTEST_CONFIGURE_COMMAND "${CTEST_CONFIGURE_COMMAND} ${CTEST_COVERAGE_EXE_LD_FLAGS}")
    set(CTEST_CONFIGURE_COMMAND "${CTEST_CONFIGURE_COMMAND} ${CTEST_COVERAGE_SHARED_LD_FLAGS}")
endif()
set(CTEST_CONFIGURE_COMMAND "${CTEST_CONFIGURE_COMMAND} -G \"${CTEST_CMAKE_GENERATOR}\"")
set(CTEST_CONFIGURE_COMMAND "${CTEST_CONFIGURE_COMMAND} \"${CTEST_SOURCE_DIRECTORY}\"")
set(CTEST_CONFIGURATION_TYPE ${CTEST_BUILD_CONFIGURATION})

ctest_start("${JENKINS_DASHBOARD}" QUIET)
ctest_configure(OPTIONS -DCMAKE_BUILD_TYPE=${CTEST_BUILD_CONFIGURATION} RETURN_VALUE CONFIGURING_RET_VALUE QUIET)
ctest_build(RETURN_VALUE BUILDING_RET_VALUE QUIET)
ctest_test(QUIET)
ctest_submit(QUIET)
if(CTEST_COVERAGE_COMMAND)
    ctest_coverage(QUIET)
endif()
if(CTEST_MEMORYCHECK_COMMAND)
    ctest_memcheck(QUIET)
endif()
ctest_submit(QUIET)

if(NOT CONFIGURING_RET_VALUE AND NOT BUILDING_RET_VALUE)
    message(0)
else()
    message(255)
endif()
