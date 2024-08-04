cpmaddpackage(
        NAME serious-meta-library
        GIT_REPOSITORY "git@github.com:UsatiyNyan/serious-meta-library.git"
        GIT_TAG main)

sl_target_link_system_libraries(${PROJECT_NAME}
        PUBLIC
        sl::meta
)
