cpmaddpackage(
        NAME serious-meta-library
        GIT_REPOSITORY "https://github.com/UsatiyNyan/serious-meta-library.git"
        GIT_TAG 1.3.0
        GIT_SHALLOW TRUE)

sl_target_link_system_libraries(${PROJECT_NAME}
        PUBLIC
        sl::meta
)
