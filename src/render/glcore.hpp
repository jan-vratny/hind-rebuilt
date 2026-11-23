#pragma once
// The part of OpenGL 3.3 core the game uses, loaded at run time through SDL (no GL library to
// link against, nothing to install, the same on Linux and Windows). A function or constant used
// for the first time is added here: functions to HIND_GL_FUNCTIONS, constants with the values
// of the OpenGL registry (glcorearb.h).
#include <cstddef>
#include <string>

#if defined(_WIN32) && !defined(_WIN64)
#define HIND_GLAPI __stdcall
#else
#define HIND_GLAPI
#endif

using GLenum = unsigned int;
using GLboolean = unsigned char;
using GLbitfield = unsigned int;
using GLint = int;
using GLuint = unsigned int;
using GLsizei = int;
using GLfloat = float;
using GLchar = char;
using GLubyte = unsigned char;
using GLsizeiptr = std::ptrdiff_t;

// X(return type, name without "gl", parameters)
#define HIND_GL_FUNCTIONS(X)                                                                                   \
  X(void, ActiveTexture, (GLenum texture))                                                                     \
  X(void, AttachShader, (GLuint program, GLuint shader))                                                       \
  X(void, BindBuffer, (GLenum target, GLuint buffer))                                                          \
  X(void, BindFramebuffer, (GLenum target, GLuint framebuffer))                                                \
  X(void, BindRenderbuffer, (GLenum target, GLuint renderbuffer))                                              \
  X(void, BindTexture, (GLenum target, GLuint texture))                                                        \
  X(void, BindVertexArray, (GLuint array))                                                                     \
  X(void, BlendFunc, (GLenum sfactor, GLenum dfactor))                                                         \
  X(void, BlitFramebuffer,                                                                                     \
    (GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1,   \
     GLbitfield mask, GLenum filter))                                                                          \
  X(void, BufferData, (GLenum target, GLsizeiptr size, const void* data, GLenum usage))                        \
  X(void, Clear, (GLbitfield mask))                                                                            \
  X(void, ClearColor, (GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha))                               \
  X(void, CompileShader, (GLuint shader))                                                                      \
  X(GLuint, CreateProgram, ())                                                                                 \
  X(GLuint, CreateShader, (GLenum type))                                                                       \
  X(void, DeleteBuffers, (GLsizei n, const GLuint* buffers))                                                   \
  X(void, DeleteFramebuffers, (GLsizei n, const GLuint* framebuffers))                                         \
  X(void, DeleteProgram, (GLuint program))                                                                     \
  X(void, DeleteRenderbuffers, (GLsizei n, const GLuint* renderbuffers))                                       \
  X(void, DeleteShader, (GLuint shader))                                                                       \
  X(void, DeleteTextures, (GLsizei n, const GLuint* textures))                                                 \
  X(void, DeleteVertexArrays, (GLsizei n, const GLuint* arrays))                                               \
  X(void, DepthMask, (GLboolean flag))                                                                         \
  X(void, Disable, (GLenum cap))                                                                               \
  X(void, DrawArrays, (GLenum mode, GLint first, GLsizei count))                                               \
  X(void, Enable, (GLenum cap))                                                                                \
  X(void, EnableVertexAttribArray, (GLuint index))                                                             \
  X(void, FramebufferRenderbuffer,                                                                             \
    (GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer))                        \
  X(void, FramebufferTexture2D, (GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level)) \
  X(void, GenBuffers, (GLsizei n, GLuint* buffers))                                                            \
  X(void, GenFramebuffers, (GLsizei n, GLuint* framebuffers))                                                  \
  X(void, GenRenderbuffers, (GLsizei n, GLuint* renderbuffers))                                                \
  X(void, GenTextures, (GLsizei n, GLuint* textures))                                                          \
  X(void, GenVertexArrays, (GLsizei n, GLuint* arrays))                                                        \
  X(void, GetProgramInfoLog, (GLuint program, GLsizei bufSize, GLsizei* length, GLchar* infoLog))              \
  X(void, GetProgramiv, (GLuint program, GLenum pname, GLint* params))                                         \
  X(void, GetShaderInfoLog, (GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* infoLog))                \
  X(void, GetShaderiv, (GLuint shader, GLenum pname, GLint* params))                                           \
  X(const GLubyte*, GetString, (GLenum name))                                                                  \
  X(GLint, GetUniformLocation, (GLuint program, const GLchar* name))                                           \
  X(void, GetIntegerv, (GLenum pname, GLint* data))                                                            \
  X(void, LinkProgram, (GLuint program))                                                                       \
  X(void, PixelStorei, (GLenum pname, GLint param))                                                            \
  X(void, PolygonOffset, (GLfloat factor, GLfloat units))                                                      \
  X(void, ReadPixels,                                                                                          \
    (GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, void* pixels))               \
  X(void, RenderbufferStorage, (GLenum target, GLenum internalformat, GLsizei width, GLsizei height))          \
  X(void, RenderbufferStorageMultisample,                                                                      \
    (GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height))                    \
  X(void, StencilFunc, (GLenum func, GLint ref, GLuint mask))                                                  \
  X(void, StencilOp, (GLenum sfail, GLenum dpfail, GLenum dppass))                                             \
  X(void, ShaderSource, (GLuint shader, GLsizei count, const GLchar* const* string, const GLint* length))      \
  X(void, TexImage2D,                                                                                          \
    (GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border,            \
     GLenum format, GLenum type, const void* pixels))                                                          \
  X(void, TexParameteri, (GLenum target, GLenum pname, GLint param))                                           \
  X(void, Uniform1f, (GLint location, GLfloat v0))                                                             \
  X(void, Uniform1i, (GLint location, GLint v0))                                                               \
  X(void, Uniform2f, (GLint location, GLfloat v0, GLfloat v1))                                                 \
  X(void, Uniform3fv, (GLint location, GLsizei count, const GLfloat* value))                                   \
  X(void, Uniform4f, (GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3))                         \
  X(void, Uniform4fv, (GLint location, GLsizei count, const GLfloat* value))                                   \
  X(void, UniformMatrix4fv, (GLint location, GLsizei count, GLboolean transpose, const GLfloat* value))        \
  X(void, UseProgram, (GLuint program))                                                                        \
  X(void, VertexAttribPointer,                                                                                 \
    (GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void* pointer))        \
  X(void, Viewport, (GLint x, GLint y, GLsizei width, GLsizei height))                                         \
  X(void, Scissor, (GLint x, GLint y, GLsizei width, GLsizei height))

// The function pointers live in a namespace of their own (so they never clash with the symbols of
// a system GL library) and are called as usual, glEnable(...).
namespace hindgl {
#define HIND_GL_DECLARE(ret, name, params) extern ret(HIND_GLAPI* gl##name) params;
HIND_GL_FUNCTIONS(HIND_GL_DECLARE)
#undef HIND_GL_DECLARE

// Loads every function with `getProcAddress` (SDL_GL_GetProcAddress) once a context is current.
// Returns false and names the first missing function in `missing`.
bool load(void* (*getProcAddress)(const char*), std::string& missing);
}  // namespace hindgl
using namespace hindgl;

constexpr GLboolean GL_FALSE = 0;
constexpr GLboolean GL_TRUE = 1;
constexpr GLenum GL_ZERO = 0;
constexpr GLenum GL_STENCIL_BUFFER_BIT = 0x0400;
constexpr GLenum GL_EQUAL = 0x0202;
constexpr GLenum GL_STENCIL_TEST = 0x0B90;
constexpr GLenum GL_KEEP = 0x1E00;
constexpr GLenum GL_INCR = 0x1E02;
constexpr GLenum GL_DEPTH24_STENCIL8 = 0x88F0;
constexpr GLenum GL_DEPTH_STENCIL_ATTACHMENT = 0x821A;
constexpr GLenum GL_ONE = 1;
constexpr GLenum GL_POINTS = 0x0000;
constexpr GLenum GL_TRIANGLES = 0x0004;
constexpr GLbitfield GL_DEPTH_BUFFER_BIT = 0x0100;
constexpr GLbitfield GL_COLOR_BUFFER_BIT = 0x4000;
constexpr GLenum GL_SRC_ALPHA = 0x0302;
constexpr GLenum GL_ONE_MINUS_SRC_ALPHA = 0x0303;
constexpr GLenum GL_DST_COLOR = 0x0306;
constexpr GLenum GL_CULL_FACE = 0x0B44;
constexpr GLenum GL_DEPTH_TEST = 0x0B71;
constexpr GLenum GL_BLEND = 0x0BE2;
constexpr GLenum GL_SCISSOR_TEST = 0x0C11;
constexpr GLenum GL_UNPACK_ALIGNMENT = 0x0CF5;
constexpr GLenum GL_PACK_ALIGNMENT = 0x0D05;
constexpr GLenum GL_TEXTURE_2D = 0x0DE1;
constexpr GLenum GL_UNSIGNED_BYTE = 0x1401;
constexpr GLenum GL_FLOAT = 0x1406;
constexpr GLenum GL_RGB = 0x1907;
constexpr GLenum GL_RGBA = 0x1908;
constexpr GLenum GL_RENDERER = 0x1F01;
constexpr GLenum GL_VERSION = 0x1F02;
constexpr GLenum GL_NEAREST = 0x2600;
constexpr GLenum GL_LINEAR = 0x2601;
constexpr GLenum GL_TEXTURE_MAG_FILTER = 0x2800;
constexpr GLenum GL_TEXTURE_MIN_FILTER = 0x2801;
constexpr GLenum GL_TEXTURE_WRAP_S = 0x2802;
constexpr GLenum GL_TEXTURE_WRAP_T = 0x2803;
constexpr GLenum GL_POLYGON_OFFSET_FILL = 0x8037;
constexpr GLenum GL_RGB8 = 0x8051;
constexpr GLenum GL_RGBA8 = 0x8058;
constexpr GLenum GL_MULTISAMPLE = 0x809D;
constexpr GLenum GL_SAMPLES = 0x80A9;
constexpr GLenum GL_DEPTH_COMPONENT24 = 0x81A6;
constexpr GLenum GL_PROGRAM_POINT_SIZE = 0x8642;
constexpr GLenum GL_CLAMP_TO_EDGE = 0x812F;
constexpr GLenum GL_TEXTURE0 = 0x84C0;
constexpr GLenum GL_STREAM_DRAW = 0x88E0;
constexpr GLenum GL_STATIC_DRAW = 0x88E4;
constexpr GLenum GL_ARRAY_BUFFER = 0x8892;
constexpr GLenum GL_FRAGMENT_SHADER = 0x8B30;
constexpr GLenum GL_VERTEX_SHADER = 0x8B31;
constexpr GLenum GL_COMPILE_STATUS = 0x8B81;
constexpr GLenum GL_LINK_STATUS = 0x8B82;
constexpr GLenum GL_READ_FRAMEBUFFER = 0x8CA8;
constexpr GLenum GL_DRAW_FRAMEBUFFER = 0x8CA9;
constexpr GLenum GL_COLOR_ATTACHMENT0 = 0x8CE0;
constexpr GLenum GL_DEPTH_ATTACHMENT = 0x8D00;
constexpr GLenum GL_FRAMEBUFFER = 0x8D40;
constexpr GLenum GL_FRAMEBUFFER_BINDING = 0x8CA6;
constexpr GLenum GL_RENDERBUFFER = 0x8D41;
