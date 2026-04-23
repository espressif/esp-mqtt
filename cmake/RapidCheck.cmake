CPMAddPackage(
    NAME RapidCheck
    GITHUB_REPOSITORY emil-e/rapidcheck
    GIT_TAG ff6af6fc683159deb51c543b065eba14dfcf329b
)

add_subdirectory(${RapidCheck_SOURCE_DIR}/extras/catch ${CMAKE_BINARY_DIR}/rapidcheck_catch)

# Treat rapidcheck headers as SYSTEM so their internal uses of deprecated APIs
# (e.g. std::aligned_storage in C++23) do not surface as warnings in our build.
foreach(_rc_target rapidcheck rapidcheck_catch)
    if(TARGET ${_rc_target})
        get_target_property(_rc_includes ${_rc_target} INTERFACE_INCLUDE_DIRECTORIES)
        if(_rc_includes)
            set_target_properties(${_rc_target} PROPERTIES
                INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_rc_includes}")
        endif()
    endif()
endforeach()
