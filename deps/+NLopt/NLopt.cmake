add_cmake_project(NLopt
  # URL "https://github.com/stevengj/nlopt/archive/v2.5.0.tar.gz"
  # URL_HASH SHA256=c6dd7a5701fff8ad5ebb45a3dc8e757e61d52658de3918e38bab233e7fd3b4ae
  URL "https://github.com/stevengj/nlopt/archive/refs/tags/v2.8.0.zip"
  URL_HASH SHA256=50645cb77fca572ba8204453c3eb02c2643f1ece627651d147dfa71dfa9d205c
  CMAKE_ARGS
    -DNLOPT_PYTHON:BOOL=OFF
    -DNLOPT_OCTAVE:BOOL=OFF
    -DNLOPT_MATLAB:BOOL=OFF
    -DNLOPT_GUILE:BOOL=OFF
    -DNLOPT_SWIG:BOOL=OFF
    -DNLOPT_TESTS:BOOL=OFF
)
