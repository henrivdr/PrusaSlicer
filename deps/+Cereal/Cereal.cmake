add_cmake_project(Cereal
    # URL "https://github.com/USCiLab/cereal/archive/refs/tags/v1.3.0.zip"
    # URL_HASH SHA256=71642cb54658e98c8f07a0f0d08bf9766f1c3771496936f6014169d3726d9657
    URL "https://github.com/USCiLab/cereal/archive/refs/tags/v1.3.2.zip"
    URL_HASH SHA256=e72c3fa8fe3d531247773e346e6824a4744cc6472a25cf9b30599cd52146e2ae
    CMAKE_ARGS
        -DJUST_INSTALL_CEREAL=ON
        -DSKIP_PERFORMANCE_COMPARISON=ON
        -DBUILD_TESTS=OFF
)