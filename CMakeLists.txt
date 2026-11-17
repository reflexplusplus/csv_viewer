cmake_minimum_required(VERSION 3.22)
project(csv_viewer)

# ---- Reflex Path ----
# To override REFLEX_DIR, pass:
#   cmake -B build -DREFLEX_DIR=/absolute/path/to/reflex

if(NOT REFLEX_DIR)
    set(REFLEX_DIR "D:/devt/reflex")
endif()

find_package(Reflex REQUIRED CONFIG
    PATHS "${REFLEX_DIR}/cmake"
    NO_DEFAULT_PATH
    # iOS sets CMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY, which re-roots PATHS
    # onto the SDK sysroot and never searches the real directory.
    NO_CMAKE_FIND_ROOT_PATH
)

# ---- App target ----
reflex_add_app(csv_viewer
    SOURCES
        ${REFLEX_DIR}/src/reflex_ext.cpp
        code/app.cpp
        code/entry.cpp
        code/resources.cpp
        code/view.cpp
    NAME               "csv_viewer"
    VENDOR             "csv_viewer"
    PACKAGE_ID_VENDOR  "csv-viewer"
    PACKAGE_ID_PRODUCT "csv-viewer"
)
