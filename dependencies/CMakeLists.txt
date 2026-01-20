cpmaddpackage(
        NAME serious-meta-library
        GIT_REPOSITORY "git@github.com:UsatiyNyan/serious-meta-library.git"
        GIT_TAG 1.1.0)

sl_target_link_system_libraries(${PROJECT_NAME}
        PUBLIC
        sl::meta
)
