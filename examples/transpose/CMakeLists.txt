cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

project(omnitrace-transpose LANGUAGES CXX)

find_package(hip QUIET HINTS ${ROCmVersion_DIR} PATHS ${ROCmVersion_DIR})

find_program(
    HIPCC_EXECUTABLE
    NAMES hipcc
    HINTS ${ROCmVersion_DIR} ${ROCM_PATH} ENV ROCM_PATH /opt/rocm
    PATHS ${ROCmVersion_DIR} ${ROCM_PATH} ENV ROCM_PATH /opt/rocm NO_CACHE)
mark_as_advanced(HIPCC_EXECUTABLE)

if(NOT HIPCC_EXECUTABLE)
    message(AUTHOR_WARNING "hipcc could not be found. Cannot build transpose target")
    return()
endif()

if(NOT CMAKE_CXX_COMPILER_IS_HIPCC AND HIPCC_EXECUTABLE)
    if(CMAKE_CXX_COMPILER STREQUAL HIPCC_EXECUTABLE OR "${CMAKE_CXX_COMPILER}" MATCHES
                                                       "hipcc")
        set(CMAKE_CXX_COMPILER_IS_HIPCC
            1
            CACHE BOOL "HIP compiler")
    endif()
endif()

if((NOT CMAKE_CXX_COMPILER_IS_HIPCC OR (NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang"
                                        AND NOT hip_FOUND))
   AND (NOT COMMAND omnitrace_custom_compilation AND NOT HIPCC_EXECUTABLE))
    message(AUTHOR_WARNING "transpose target could not be built")
    return()
endif()

option(TRANSPOSE_USE_MPI "Enable MPI support in transpose exe" ${TIMEMORY_USE_MPI})

if(TRANSPOSE_USE_MPI)
    find_package(MPI REQUIRED)
endif()

add_executable(transpose transpose.cpp)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang"
   AND NOT CMAKE_CXX_COMPILER_IS_HIPCC
   AND NOT HIPCC_EXECUTABLE)
    target_link_libraries(
        transpose
        PRIVATE $<TARGET_NAME_IF_EXISTS:omnitrace::omnitrace-compile-options>
                $<TARGET_NAME_IF_EXISTS:hip::host> $<TARGET_NAME_IF_EXISTS:hip::device>)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_link_libraries(
        transpose PRIVATE $<TARGET_NAME_IF_EXISTS:omnitrace::omnitrace-compile-options>)
else()
    target_compile_options(transpose PRIVATE -W -Wall)
endif()

if(TRANSPOSE_USE_MPI)
    target_compile_definitions(transpose PRIVATE USE_MPI)
    target_link_libraries(transpose PRIVATE MPI::MPI_C)
endif()

if(NOT CMAKE_CXX_COMPILER_IS_HIPCC AND HIPCC_EXECUTABLE)
    # defined in MacroUtilities.cmake
    omnitrace_custom_compilation(COMPILER ${HIPCC_EXECUTABLE} TARGET transpose)
endif()

if(OMNITRACE_INSTALL_EXAMPLES)
    install(
        TARGETS transpose
        DESTINATION bin
        COMPONENT omnitrace-examples)
endif()
