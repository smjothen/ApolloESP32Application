#
# This is a project Makefile. It is assumed the directory this Makefile resides in is a
# project subdirectory.
#

PROJECT_NAME := Apollo

#EXTRA_COMPONENT_DIRS = $(IDF_PATH)/examples/common_components/protocol_examples_common
#EXTRA_COMPONENT_DIRS = $(IDF_PATH)/components/esp_wifi
COMPONENT_ADD_INCLUDEDIRS := components/include

include $(IDF_PATH)/make/project.mk

