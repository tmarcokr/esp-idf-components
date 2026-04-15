cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "../../components" "../../components/audio")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(sd_audio_player_example)
