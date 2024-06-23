cpmaddpackage(
        NAME serious-meta-library
        GIT_REPOSITORY "git@github.com:UsatiyNyan/serious-meta-library.git"
        GIT_TAG main)
cpmaddpackage(
        NAME libassert
        GIT_REPOSITORY "git@github.com:jeremy-rifkin/libassert.git"
        GIT_TAG v2.0.1)

sl_target_link_system_libraries(${PROJECT_NAME}
        INTERFACE
        sl::meta
        libassert::assert
)

