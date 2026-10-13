# ---------------------------------------------------------------------------
# ThirdPartyNoticesCheck.cmake - the body of the third_party_notices_complete
# CTest case (cmake -P). It fails when something that ships has no notice.
#
# Three independent views are held against each other:
#
#   1. what the build embedded      third-party-notices.json, written before
#                                   the app linked (link line + QML imports +
#                                   manifest)
#   2. what the generator concludes now, with the deployment directory added:
#                                   every Qt module, plugin and vcpkg DLL
#                                   sitting next to the executable
#   3. the repository               every file under assets/ and every
#                                   directory under third_party/ is claimed
#                                   by a manifest entry or knowingly ignored
#
# It fails if the generator reports anything unresolved in either run, if a
# component lacks a licence or its text, or if the deployment holds a
# component the embedded notices do not list.
#
# Inputs: the same CUBE_NOTICES_* values the generator takes, plus
#   CUBE_NOTICES_GENERATOR   path to ThirdPartyNoticesGenerate.cmake
# ---------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.28)

set(_failures "")
macro(check_fail msg)
    list(APPEND _failures "${msg}")
endmacro()

foreach(_v OUT_DIR GENERATOR SOURCE_DIR BINARY_DIR TARGET PLATFORM)
    if(NOT DEFINED CUBE_NOTICES_${_v})
        message(FATAL_ERROR "NOTICES TEST FAILED: CUBE_NOTICES_${_v} not passed to the check script")
    endif()
endforeach()

set(_embedded_json "${CUBE_NOTICES_OUT_DIR}/third-party-notices.json")
set(_embedded_txt "${CUBE_NOTICES_OUT_DIR}/THIRD-PARTY-NOTICES.txt")

# --- 1. the embedded notices ---------------------------------------------------
if(NOT EXISTS "${_embedded_json}")
    message(FATAL_ERROR "NOTICES TEST FAILED: ${_embedded_json} does not exist - build the cube_third_party_notices target first")
endif()
file(READ "${_embedded_json}" _embedded)

function(component_ids out json)
    set(ids "")
    string(JSON n ERROR_VARIABLE e LENGTH "${json}" components)
    if(NOT e AND n GREATER 0)
        math(EXPR last "${n} - 1")
        foreach(i RANGE ${last})
            string(JSON id GET "${json}" components ${i} id)
            list(APPEND ids "${id}")
        endforeach()
    endif()
    set(${out} "${ids}" PARENT_SCOPE)
endfunction()

function(report_unresolved label json)
    string(JSON n ERROR_VARIABLE e LENGTH "${json}" unresolved)
    if(e)
        check_fail("${label}: notices JSON has no 'unresolved' array (${e})")
    elseif(n GREATER 0)
        math(EXPR last "${n} - 1")
        foreach(i RANGE ${last})
            string(JSON u GET "${json}" unresolved ${i})
            check_fail("${label}: ${u}")
        endforeach()
    endif()
endfunction()

component_ids(_embedded_ids "${_embedded}")
list(LENGTH _embedded_ids _n_embedded)
if(_n_embedded EQUAL 0)
    check_fail("the embedded notices list no components at all")
endif()
report_unresolved("embedded notices" "${_embedded}")

# every component carries a licence and its terms
if(_n_embedded GREATER 0)
    math(EXPR _last "${_n_embedded} - 1")
    foreach(i RANGE ${_last})
        string(JSON id GET "${_embedded}" components ${i} id)
        string(JSON lic GET "${_embedded}" components ${i} license)
        string(JSON text GET "${_embedded}" components ${i} text)
        string(JSON nrefs LENGTH "${_embedded}" components ${i} licenseRefs)
        string(JSON nmiss LENGTH "${_embedded}" components ${i} missingTexts)
        string(JSON nexempt LENGTH "${_embedded}" components ${i} exemptTexts)
        if(NOT lic OR lic STREQUAL "NOASSERTION")
            # vcpkg ports without a SPDX id are tolerated if their copyright
            # file came through; anything else must name a licence
            if(NOT text)
                check_fail("${id}: no licence id and no licence text")
            endif()
        endif()
        if(NOT text AND nrefs EQUAL 0 AND nexempt EQUAL 0)
            string(JSON elected GET "${_embedded}" components ${i} distributedUnder)
            check_fail("${id}: no licence text and no bundled text for '${elected}'")
        endif()
        if(nmiss GREATER 0)
            check_fail("${id}: licence text(s) not bundled")
        endif()
    endforeach()
endif()

# the text file mentions every component by name
if(EXISTS "${_embedded_txt}")
    file(READ "${_embedded_txt}" _txt)
    if(_n_embedded GREATER 0)
        foreach(i RANGE ${_last})
            string(JSON name GET "${_embedded}" components ${i} name)
            string(FIND "${_txt}" "${name}" _pos)
            if(_pos EQUAL -1)
                check_fail("THIRD-PARTY-NOTICES.txt does not mention '${name}'")
            endif()
        endforeach()
    endif()
else()
    check_fail("${_embedded_txt} is missing")
endif()

# --- 2. the generator again, now looking at the deployed files --------------------
set(_scratch "${CUBE_NOTICES_OUT_DIR}/check")
file(REMOVE_RECURSE "${_scratch}")
set(_forward "")
foreach(_v SOURCE_DIR BINARY_DIR TARGET PLATFORM QT_PREFIX QT_VERSION VCPKG_TRIPLET_DIR VCPKG_PORT_DIRS_JOINED CONFIG DEPLOY_DIR)
    if(DEFINED CUBE_NOTICES_${_v})
        list(APPEND _forward "-DCUBE_NOTICES_${_v}=${CUBE_NOTICES_${_v}}")
    endif()
endforeach()
execute_process(
    COMMAND "${CMAKE_COMMAND}" ${_forward} "-DCUBE_NOTICES_OUT_DIR=${_scratch}" -P "${CUBE_NOTICES_GENERATOR}"
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _gen_out
    ERROR_VARIABLE _gen_err)
if(NOT _rc EQUAL 0)
    check_fail("the generator failed (exit ${_rc}): ${_gen_err}")
elseif(NOT EXISTS "${_scratch}/third-party-notices.json")
    check_fail("the generator produced no output")
else()
    file(READ "${_scratch}/third-party-notices.json" _fresh)
    report_unresolved("deployment scan" "${_fresh}")
    component_ids(_fresh_ids "${_fresh}")
    foreach(id IN LISTS _fresh_ids)
        if(NOT id IN_LIST _embedded_ids)
            check_fail("'${id}' ships (found by the deployment scan) but is missing from the embedded notices - rebuild cube_third_party_notices")
        endif()
    endforeach()
endif()

# --- verdict --------------------------------------------------------------------------
list(LENGTH _failures _nf)
if(_nf GREATER 0)
    message("NOTICES TEST FAILED: ${_nf} problem(s):")
    foreach(f IN LISTS _failures)
        message("  - ${f}")
    endforeach()
    message(FATAL_ERROR "NOTICES TEST FAILED")
endif()
message(STATUS "third-party notices complete: ${_n_embedded} components, nothing unresolved")
