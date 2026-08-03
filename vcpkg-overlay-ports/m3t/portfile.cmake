# M3T lives in the M3T/ subdirectory of the DLR-RM/3DObjectTracking monorepo.
# Upstream publishes no tags or releases, so the port pins a commit
# (master HEAD at the time of writing); version-date matches the commit date.
vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO DLR-RM/3DObjectTracking
    REF f02106186a1e756450226b3b793e309506817c58
    SHA512 968cf4dbb8fc97fc70a4f1d9873c78895a54cb56c313c60760091ecb45ebddbbddb564d13a109388012560d77286ea1b4c1f44024d623dcc2818f2fa3a0ee0d3
    HEAD_REF master
    PATCHES
        0001-M3T-require-CMake-3.10.patch
        0002-M3T-fix-Renderer-referenced_body_ptrs-returning-a-re.patch
        0003-M3T-fix-missing-return-in-Body-get_id.patch
        0004-M3T-make-the-Windows-static-runtime-block-optional.patch
        0005-M3T-accept-newer-Eigen-versions.patch
        0006-M3T-make-TextureModality-optional.patch
        0007-M3T-allow-skipping-the-example-test-and-doc-subdirec.patch
        0008-M3T-resolve-dependencies-in-the-installed-package-co.patch
        0009-M3T-only-link-stdc-fs-where-it-exists.patch
)

vcpkg_check_features(OUT_FEATURE_OPTIONS FEATURE_OPTIONS
    FEATURES
        texture USE_TEXTURE_MODALITY
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}/M3T"
    OPTIONS
        ${FEATURE_OPTIONS}
        -DM3T_WIN32_STATIC=OFF
        -DM3T_BUILD_EXAMPLES=OFF
        -DM3T_BUILD_DOC=OFF
        -DUSE_GTEST=OFF
        -DUSE_AZURE_KINECT=OFF
        -DUSE_REALSENSE=OFF
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME M3T CONFIG_PATH lib/cmake/M3T)

# Upstream installs its bundled third_party/ headers wholesale. Only the
# filesystem shim is referenced by the installed public headers; nanoflann
# and tiny_obj_loader are used exclusively by sources compiled into the
# library (and by examples, which this port does not build).
file(REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/include/nanoflann"
    "${CURRENT_PACKAGES_DIR}/include/tiny_obj_loader"
    "${CURRENT_PACKAGES_DIR}/debug/include"
)

file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
