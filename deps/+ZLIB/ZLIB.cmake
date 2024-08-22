add_cmake_project(ZLIB  
  URL https://github.com/madler/zlib/releases/download/v1.3/zlib13.zip
  URL_HASH SHA256=c561d09347f674f0d72692e7c75d9898919326c532aab7f8c07bb43b07efeb38
  CMAKE_ARGS
    -DSKIP_INSTALL_FILES=ON         # Prevent installation of man pages et al.
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
)

