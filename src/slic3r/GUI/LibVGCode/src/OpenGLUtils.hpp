///|/ Copyright (c) Prusa Research 2023 Enrico Turri @enricoturri1966, Pavel Mikuš @Godrak, Vojtěch Bubník @bubnikv
///|/
///|/ libvgcode is released under the terms of the AGPLv3 or higher
///|/
#ifndef VGCODE_OPENGLUTILS_HPP
#define VGCODE_OPENGLUTILS_HPP

// OpenGL dependencies
#include <GL/glew.h>

namespace libvgcode {
#ifndef NDEBUG
#define HAS_GLSAFE
#endif // NDEBUG

#ifdef HAS_GLSAFE
extern void glAssertRecentCallImpl(const char* file_name, unsigned int line, const char* function_name);
inline void glAssertRecentCall() { glAssertRecentCallImpl(__FILE__, __LINE__, __FUNCTION__); }
#define glsafe(cmd) do { cmd; glAssertRecentCallImpl(__FILE__, __LINE__, __FUNCTION__); } while (false)
#define glcheck() do { glAssertRecentCallImpl(__FILE__, __LINE__, __FUNCTION__); } while (false)
#else
inline void glAssertRecentCall() { }
#define glsafe(cmd) cmd
#define glcheck()
#endif // HAS_GLSAFE

extern bool check_opengl_version();

} // namespace libvgcode

#endif // VGCODE_OPENGLUTILS_HPP