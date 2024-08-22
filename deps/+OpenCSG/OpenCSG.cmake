
add_cmake_project(OpenCSG
    EXCLUDE_FROM_ALL ON # No need to build this lib by default. Only used in experiment in sandboxes/opencsg
    # URL https://github.com/floriankirsch/OpenCSG/archive/refs/tags/opencsg-1-4-2-release.zip
    # URL_HASH SHA256=51afe0db79af8386e2027d56d685177135581e0ee82ade9d7f2caff8deab5ec5
    
    # URL https://github.com/floriankirsch/OpenCSG/archive/refs/tags/opencsg-1-5-1-release.zip
    # URL_HASH SHA256=114450e3431189018a8f7cf460db440d5e3062a61b45bd2f91eb2adf0e8cf771

    # 1.6.0 uses Glad instead of GLEW. So it might not work.
    URL https://github.com/floriankirsch/OpenCSG/archive/refs/tags/opencsg-1-6-0-release.zip
    URL_HASH SHA256=a8b4a6a82f897572492b28264b6f78c739d25998ecc84d2db0c15488eef1755e
    PATCH_COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt.in ./CMakeLists.txt
)

set(DEP_OpenCSG_DEPENDS GLEW ZLIB)
