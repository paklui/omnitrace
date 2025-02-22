# adds a ctest for executable
function(OMNITRACE_ADD_BIN_TEST)
    cmake_parse_arguments(
        TEST
        "ADD_INVERSE" # options
        "NAME;TARGET;TIMEOUT;WORKING_DIRECTORY" # single value args
        "ARGS;ENVIRONMENT;LABELS;PROPERTIES;PASS_REGEX;FAIL_REGEX;SKIP_REGEX;DEPENDS;COMMAND" # multiple
        # value args
        ${ARGN})

    if(NOT TEST_WORKING_DIRECTORY)
        set(TEST_WORKING_DIRECTORY ${PROJECT_BINARY_DIR})
    endif()
    if(NOT OMNITRACE_DYNINST_API_RT_DIR AND OMNITRACE_DYNINST_API_RT)
        get_filename_component(OMNITRACE_DYNINST_API_RT_DIR "${OMNITRACE_DYNINST_API_RT}"
                               DIRECTORY)
    endif()

    if(OMNITRACE_BUILD_DYNINST)
        set(OMNITRACE_DYNINST_API_RT_DIR
            "${PROJECT_BINARY_DIR}/external/dyninst/dyninstAPI_RT:${PROJECT_BINARY_DIR}/external/dyninst/dyninstAPI"
            )
    endif()

    if(NOT TEST_ENVIRONMENT)
        set(TEST_ENVIRONMENT
            "OMNITRACE_USE_PERFETTO=ON"
            "OMNITRACE_USE_TIMEMORY=ON"
            "OMNITRACE_USE_SAMPLING=ON"
            "OMNITRACE_TIME_OUTPUT=OFF"
            "LD_LIBRARY_PATH=${PROJECT_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR}:${OMNITRACE_DYNINST_API_RT_DIR}:$ENV{LD_LIBRARY_PATH}"
            )
    endif()

    # common
    list(APPEND TEST_ENVIRONMENT "OMNITRACE_CI=ON"
         "OMNITRACE_OUTPUT_PATH=omnitrace-tests-output")
    # copy for inverse
    set(TEST_ENVIRONMENT_INV "${TEST_ENVIRONMENT}")

    # different for regular test and inverse test
    list(APPEND TEST_ENVIRONMENT "OMNITRACE_OUTPUT_PREFIX=${TEST_NAME}/")
    list(APPEND TEST_ENVIRONMENT_INV "OMNITRACE_OUTPUT_PREFIX=${TEST_NAME}-inverse/")

    if(TEST_COMMAND)
        add_test(
            NAME ${TEST_NAME}
            COMMAND ${TEST_COMMAND} ${TEST_ARGS}
            WORKING_DIRECTORY ${TEST_WORKING_DIRECTORY})

        set_tests_properties(
            ${TEST_NAME}
            PROPERTIES ENVIRONMENT
                       "${TEST_ENVIRONMENT}"
                       TIMEOUT
                       ${TEST_TIMEOUT}
                       DEPENDS
                       "${TEST_DEPENDS}"
                       LABELS
                       "omnitrace-bin;${TEST_LABELS}"
                       PASS_REGULAR_EXPRESSION
                       "${TEST_PASS_REGEX}"
                       FAIL_REGULAR_EXPRESSION
                       "${TEST_FAIL_REGEX}"
                       SKIP_REGULAR_EXPRESSION
                       "${TEST_SKIP_REGEX}"
                       ${TEST_PROPERTIES})
        if(TEST_ADD_INVERSE)
            add_test(
                NAME ${TEST_NAME}-inverse
                COMMAND ${TEST_COMMAND} ${TEST_ARGS}
                WORKING_DIRECTORY ${TEST_WORKING_DIRECTORY})

            set_tests_properties(
                ${TEST_NAME}-inverse
                PROPERTIES ENVIRONMENT
                           "${TEST_ENVIRONMENT_INV}"
                           TIMEOUT
                           ${TEST_TIMEOUT}
                           DEPENDS
                           "${TEST_DEPENDS}"
                           LABELS
                           "omnitrace-bin;${TEST_LABELS}"
                           PASS_REGULAR_EXPRESSION
                           "${TEST_FAIL_REGEX}"
                           FAIL_REGULAR_EXPRESSION
                           "${TEST_PASS_REGEX}"
                           SKIP_REGULAR_EXPRESSION
                           "${TEST_SKIP_REGEX}"
                           WILL_FAIL
                           ON
                           ${TEST_PROPERTIES})
        endif()
    elseif(TARGET ${TEST_TARGET})
        add_test(
            NAME ${TEST_NAME}
            COMMAND $<TARGET_FILE:${TEST_TARGET}> ${TEST_ARGS}
            WORKING_DIRECTORY ${TEST_WORKING_DIRECTORY})

        set_tests_properties(
            ${TEST_NAME}
            PROPERTIES ENVIRONMENT
                       "${TEST_ENVIRONMENT}"
                       TIMEOUT
                       ${TEST_TIMEOUT}
                       DEPENDS
                       "${TEST_DEPENDS}"
                       LABELS
                       "omnitrace-bin;${TEST_LABELS}"
                       PASS_REGULAR_EXPRESSION
                       "${TEST_PASS_REGEX}"
                       FAIL_REGULAR_EXPRESSION
                       "${TEST_FAIL_REGEX}"
                       SKIP_REGULAR_EXPRESSION
                       "${TEST_SKIP_REGEX}"
                       ${TEST_PROPERTIES})
        if(TEST_ADD_INVERSE)
            add_test(
                NAME ${TEST_NAME}-inverse
                COMMAND $<TARGET_FILE:${TEST_TARGET}> ${TEST_ARGS}
                WORKING_DIRECTORY ${TEST_WORKING_DIRECTORY})

            set_tests_properties(
                ${TEST_NAME}-inverse
                PROPERTIES ENVIRONMENT
                           "${TEST_ENVIRONMENT_INV}"
                           TIMEOUT
                           ${TEST_TIMEOUT}
                           DEPENDS
                           "${TEST_DEPENDS}"
                           LABELS
                           "omnitrace-bin;${TEST_LABELS}"
                           PASS_REGULAR_EXPRESSION
                           "${TEST_FAIL_REGEX}"
                           FAIL_REGULAR_EXPRESSION
                           "${TEST_PASS_REGEX}"
                           SKIP_REGULAR_EXPRESSION
                           "${TEST_SKIP_REGEX}"
                           WILL_FAIL
                           ON
                           ${TEST_PROPERTIES})
        endif()
    elseif(OMNITRACE_BUILD_TESTING)
        message(FATAL_ERROR "Error! ${TEST_TARGET} does not exist")
    endif()
endfunction()

omnitrace_add_bin_test(
    NAME omnitrace-exe-help
    TARGET omnitrace-exe
    ARGS --help
    LABELS omnitrace-exe
    TIMEOUT 45
    PASS_REGEX
        ".*\\\[omnitrace\\\] Usage:.*\\\[DEBUG OPTIONS\\\].*\\\[MODE OPTIONS\\\].*\\\[LIBRARY OPTIONS\\\].*\\\[SYMBOL SELECTION OPTIONS\\\].*\\\[RUNTIME OPTIONS\\\].*\\\[GRANULARITY OPTIONS\\\].*\\\[DYNINST OPTIONS\\\].*"
    )

omnitrace_add_bin_test(
    NAME omnitrace-exe-simulate-ls
    TARGET omnitrace-exe
    ARGS --simulate
         --print-format
         json
         txt
         xml
         -v
         2
         --all-functions
         --
         ls
    LABELS "simulate"
    TIMEOUT 120)

omnitrace_add_bin_test(
    NAME omnitrace-exe-simulate-ls-check
    DEPENDS omnitrace-exe-simulate-ls
    COMMAND ls omnitrace-tests-output/omnitrace-exe-simulate-ls/instrumentation
    TIMEOUT 60
    PASS_REGEX
        ".*available.json.*available.txt.*available.xml.*excluded.json.*excluded.txt.*excluded.xml.*instrumented.json.*instrumented.txt.*instrumented.xml.*overlapping.json.*overlapping.txt.*overlapping.xml.*"
    )

omnitrace_add_bin_test(
    ADD_INVERSE
    NAME omnitrace-exe-simulate-lib
    TARGET omnitrace-exe
    ARGS --print-available functions -v 2 -- $<TARGET_FILE:omnitrace-user-library>
    LABELS "simulate"
    TIMEOUT 120
    PASS_REGEX
        "\\\[omnitrace\\\]\\\[exe\\\] Runtime instrumentation is not possible!(.*)\n(.*)\\\[omnitrace\\\]\\\[exe\\\] Switching to binary rewrite mode and assuming '--simulate --all-functions'"
    )

omnitrace_add_bin_test(
    NAME omnitrace-avail-help
    TARGET omnitrace-avail
    ARGS --help
    LABELS "omnitrace-avail"
    TIMEOUT 45
    PASS_REGEX
        ".*\\\[omnitrace-avail\\\] Usage:.*\\\[CATEGORIES\\\].*\\\[VIEW OPTIONS\\\].*\\\[COLUMN OPTIONS\\\].*\\\[WIDTH OPTIONS\\\].*\\\[OUTPUT OPTIONS\\\].*"
    )

omnitrace_add_bin_test(
    NAME omnitrace-avail-all
    TARGET omnitrace-avail
    ARGS --all
    LABELS "omnitrace-avail"
    TIMEOUT 45)

omnitrace_add_bin_test(
    NAME omnitrace-avail-all-csv
    TARGET omnitrace-avail
    ARGS --all --csv --csv-separator "#"
    LABELS "omnitrace-avail"
    TIMEOUT 45
    PASS_REGEX
        "COMPONENT#AVAILABLE#VALUE_TYPE#STRING_IDS#FILENAME#DESCRIPTION#CATEGORY#.*ENVIRONMENT VARIABLE#VALUE#DATA TYPE#DESCRIPTION#CATEGORIES#.*HARDWARE COUNTER#AVAILABLE#DESCRIPTION#"
    )

omnitrace_add_bin_test(
    NAME omnitrace-avail-filter-wall-clock-available
    TARGET omnitrace-avail
    ARGS -r wall_clock -C --available
    LABELS "omnitrace-avail"
    TIMEOUT 45
    PASS_REGEX
        "\\\|[-]+\\\|\n\\\|[ ]+COMPONENT[ ]+\\\|\n\\\|[-]+\\\|\n\\\| (wall_clock)[ ]+\\\|\n\\\| (sampling_wall_clock)[ ]+\\\|\n\\\|[-]+\\\|"
    )

omnitrace_add_bin_test(
    NAME omnitrace-avail-category-filter-omnitrace
    TARGET omnitrace-avail
    ARGS --categories settings::omnitrace --brief
    LABELS "omnitrace-avail"
    TIMEOUT 45
    PASS_REGEX "OMNITRACE_(SETTINGS_DESC|OUTPUT_FILE|OUTPUT_PREFIX)"
    FAIL_REGEX
        "OMNITRACE_(ADD_SECONDARY|SCIENTIFIC|PRECISION|MEMORY_PRECISION|TIMING_PRECISION)"
    )

omnitrace_add_bin_test(
    NAME omnitrace-avail-category-filter-timemory
    TARGET omnitrace-avail
    ARGS --categories settings::timemory --brief --advanced
    LABELS "omnitrace-avail"
    TIMEOUT 45
    PASS_REGEX
        "OMNITRACE_(ADD_SECONDARY|SCIENTIFIC|PRECISION|MEMORY_PRECISION|TIMING_PRECISION)"
    FAIL_REGEX "OMNITRACE_(SETTINGS_DESC|OUTPUT_FILE)")

omnitrace_add_bin_test(
    NAME omnitrace-avail-regex-negation
    TARGET omnitrace-avail
    ARGS -R
         omnitrace
         ~timemory
         ~critical_trace
         -r
         _P
         ~PERFETTO
         ~PROCESS_SAMPLING
         --csv
         --brief
         --advanced
    LABELS "omnitrace-avail"
    TIMEOUT 45
    PASS_REGEX
        "ENVIRONMENT VARIABLE,[ \n]+OMNITRACE_THREAD_POOL_SIZE,[ \n]+OMNITRACE_USE_PID,[ \n]+"
    FAIL_REGEX "OMNITRACE_USE_PERFETTO")

string(REPLACE "+" "\\\+" _AVAIL_CFG_PATH
               "${PROJECT_BINARY_DIR}/omnitrace-tests-output/omnitrace-avail/omnitrace-")
omnitrace_add_bin_test(
    NAME omnitrace-avail-write-config
    TARGET omnitrace-avail
    ARGS -G %env{PWD}%/omnitrace-tests-output/omnitrace-avail/omnitrace-test.cfg -F txt
         json xml --force --all
    TIMEOUT 45
    LABELS "omnitrace-avail"
    PASS_REGEX
        "Outputting JSON configuration file '${_AVAIL_CFG_PATH}test\\\.json'(.*)Outputting XML configuration file '${_AVAIL_CFG_PATH}test\\\.xml'(.*)Outputting text configuration file '${_AVAIL_CFG_PATH}test\\\.cfg'(.*)"
    )

omnitrace_add_bin_test(
    NAME omnitrace-avail-write-config-tweak
    TARGET omnitrace-avail
    ARGS -G %env{PWD}%/omnitrace-tests-output/omnitrace-avail/omnitrace-tweak.cfg -F txt
         json xml --force
    TIMEOUT 45
    LABELS "omnitrace-avail"
    ENVIRONMENT "OMNITRACE_USE_PERFETTO=OFF;OMNITRACE_USE_TIMEMORY=ON"
    PASS_REGEX
        "Outputting JSON configuration file '${_AVAIL_CFG_PATH}tweak\\\.json'(.*)Outputting XML configuration file '${_AVAIL_CFG_PATH}tweak\\\.xml'(.*)Outputting text configuration file '${_AVAIL_CFG_PATH}tweak\\\.cfg'(.*)"
    )
