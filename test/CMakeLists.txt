# This is the project CMakeLists.txt file for the test subproject
cmake_minimum_required(VERSION 3.16)

# ESP-IDF suggests adding ../components/ here but that doesn't work
# currently due to dependencies on certs and bin/dspic.bin which are
# relative to the project dir, so for now, just add the testable
# components to this list...
#
# TODO: Fix above so we can have access to all components
#
set(EXTRA_COMPONENT_DIRS
	"../components/midlts/"
	"../components/utz/"
	"$ENV{IDF_PATH}/tools/unit-test-app/components/test_utils/"
)

set(TEST_COMPONENTS
	"midlts"
	"utz"
	CACHE STRING "List of components to test"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ApolloUnitTest)
