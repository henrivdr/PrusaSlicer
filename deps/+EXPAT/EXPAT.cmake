add_cmake_project(EXPAT
  # URL https://github.com/libexpat/libexpat/archive/refs/tags/R_2_4_3.zip
  # URL_HASH SHA256=8851e199d763dc785277d6d414ed3e70ff683915158b51b8d8781df0e3af950a
  URL https://github.com/libexpat/libexpat/archive/refs/tags/R_2_6_2.zip
  URL_HASH SHA256=9cddaf9abdac4cb3308c24fea13219a7879c0ad9a8e6797240b9cf4770b337b4
  SOURCE_SUBDIR expat
  CMAKE_ARGS
    -DEXPAT_BUILD_TOOLS:BOOL=OFF
    -DEXPAT_BUILD_EXAMPLES:BOOL=OFF
    -DEXPAT_BUILD_TESTS:BOOL=OFF
    -DEXPAT_BUILD_DOCS=OFF
    -DEXPAT_BUILD_PKGCONFIG=OFF
)