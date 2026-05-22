# ProjectUnity patch for Qt Advanced Docking System 4.5.0.
# ADS 4.5.0 version resource generation asks Git for the root project version.
# In a non-Git workspace this produces invalid Windows RC version fields.

set(_VERSIONING_MODULE_DIR "${CMAKE_CURRENT_LIST_DIR}" CACHE INTERNAL "Versioning module directory")

set(_ads_version_source "${ADS_VERSION}")
if(NOT _ads_version_source)
    set(_ads_version_source "4.5.0")
endif()

string(REGEX MATCH "^([0-9]+)\\.([0-9]+)\\.([0-9]+)" _ads_version_match "${_ads_version_source}")
if(_ads_version_match)
    set(PROJECT_VERSION_MAJOR "${CMAKE_MATCH_1}")
    set(PROJECT_VERSION_MINOR "${CMAKE_MATCH_2}")
    set(PROJECT_VERSION_PATCH "${CMAKE_MATCH_3}")
else()
    set(PROJECT_VERSION_MAJOR "4")
    set(PROJECT_VERSION_MINOR "5")
    set(PROJECT_VERSION_PATCH "0")
endif()

set(PROJECT_VERSION_STRING
    "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}"
)
set(PROJECT_GIT_HASH "")
set(PROJECT_GIT_HASH_SHORT "")

set(PROJECT_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}" PARENT_SCOPE)
set(PROJECT_VERSION_MINOR "${PROJECT_VERSION_MINOR}" PARENT_SCOPE)
set(PROJECT_VERSION_PATCH "${PROJECT_VERSION_PATCH}" PARENT_SCOPE)
set(PROJECT_VERSION_STRING "${PROJECT_VERSION_STRING}" PARENT_SCOPE)
set(PROJECT_GIT_HASH "${PROJECT_GIT_HASH}" PARENT_SCOPE)
set(PROJECT_GIT_HASH_SHORT "${PROJECT_GIT_HASH_SHORT}" PARENT_SCOPE)
set(PROJECT_AUTO_VERSION "${PROJECT_VERSION_STRING}" PARENT_SCOPE)

function(add_windows_version_resources target)
    if(NOT WIN32)
        return()
    endif()

    if(NOT TARGET "${target}")
        message(FATAL_ERROR "add_windows_version_resources: target '${target}' not found.")
    endif()

    get_filename_component(_rc_in
        "${_VERSIONING_MODULE_DIR}/FileVersionInfo.rc.in"
        ABSOLUTE
    )

    if(NOT EXISTS "${_rc_in}")
        message(FATAL_ERROR "FileVersionInfo.rc.in missing at: ${_rc_in}")
    endif()

    set(_rc_out "${CMAKE_CURRENT_BINARY_DIR}/${target}_version.rc")
    get_filename_component(_rc_out "${_rc_out}" ABSOLUTE)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    configure_file("${_rc_in}" "${_rc_out}" @ONLY)
    target_sources(${target} PRIVATE "${_rc_out}")
endfunction()
