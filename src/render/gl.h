#pragma once
#include "BaseUtils/BUMath.h"
#include <GLFW/glfw3.h>
#include <cmath>
#include <cstddef>

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_RGBA8
#define GL_RGBA8 0x8058
#endif
#ifndef GL_TEXTURE0
#define GL_TEXTURE0 0x84C0
#endif
#ifndef GL_ACTIVE_TEXTURE
#define GL_ACTIVE_TEXTURE 0x84E0
#endif
#ifndef GL_DEPTH_COMPONENT24
#define GL_DEPTH_COMPONENT24 0x81A6
#endif
#ifndef GL_UNPACK_ROW_LENGTH
#define GL_UNPACK_ROW_LENGTH 0x0CF2
#endif
#ifndef GL_UNPACK_SKIP_ROWS
#define GL_UNPACK_SKIP_ROWS 0x0CF3
#endif
#ifndef GL_UNPACK_SKIP_PIXELS
#define GL_UNPACK_SKIP_PIXELS 0x0CF4
#endif
#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER 0x8892
#endif
#ifndef GL_ELEMENT_ARRAY_BUFFER
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#endif
#ifndef GL_ARRAY_BUFFER_BINDING
#define GL_ARRAY_BUFFER_BINDING 0x8894
#endif
#ifndef GL_PIXEL_UNPACK_BUFFER
#define GL_PIXEL_UNPACK_BUFFER 0x88EC
#endif
#ifndef GL_PIXEL_UNPACK_BUFFER_BINDING
#define GL_PIXEL_UNPACK_BUFFER_BINDING 0x88EF
#endif
#ifndef GL_STATIC_DRAW
#define GL_STATIC_DRAW 0x88E4
#endif
#ifndef GL_FRAGMENT_SHADER
#define GL_FRAGMENT_SHADER 0x8B30
#endif
#ifndef GL_VERTEX_SHADER
#define GL_VERTEX_SHADER 0x8B31
#endif
#ifndef GL_COMPILE_STATUS
#define GL_COMPILE_STATUS 0x8B81
#endif
#ifndef GL_LINK_STATUS
#define GL_LINK_STATUS 0x8B82
#endif
#ifndef GL_CURRENT_PROGRAM
#define GL_CURRENT_PROGRAM 0x8B8D
#endif
#ifndef GL_VERTEX_ARRAY_BINDING
#define GL_VERTEX_ARRAY_BINDING 0x85B5
#endif
#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER 0x8D40
#endif
#ifndef GL_READ_FRAMEBUFFER
#define GL_READ_FRAMEBUFFER 0x8CA8
#endif
#ifndef GL_DRAW_FRAMEBUFFER
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#endif
#ifndef GL_DRAW_FRAMEBUFFER_BINDING
#define GL_DRAW_FRAMEBUFFER_BINDING 0x8CA6
#endif
#ifndef GL_READ_FRAMEBUFFER_BINDING
#define GL_READ_FRAMEBUFFER_BINDING 0x8CAA
#endif
#ifndef GL_FRAMEBUFFER_COMPLETE
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#endif
#ifndef GL_MAX_SAMPLES
#define GL_MAX_SAMPLES 0x8D57
#endif
#ifndef GL_COLOR_ATTACHMENT0
#define GL_COLOR_ATTACHMENT0 0x8CE0
#endif
#ifndef GL_DEPTH_ATTACHMENT
#define GL_DEPTH_ATTACHMENT 0x8D00
#endif
#ifndef GL_RENDERBUFFER
#define GL_RENDERBUFFER 0x8D41
#endif
#ifndef GL_RENDERBUFFER_BINDING
#define GL_RENDERBUFFER_BINDING 0x8CA7
#endif

#ifdef __APPLE__
constexpr const char* kGlslVersion = "#version 150";
#else
constexpr const char* kGlslVersion = "#version 130";
#endif

void ApplyOpenGLContextHints();

namespace gl3
{
    using SizeIPtr = std::ptrdiff_t;

#define PKZGUI_GL3_FUNCTIONS(X) \
    X(GLuint, CreateShader, (GLenum)) \
    X(void, ShaderSource, (GLuint, GLsizei, const char* const*, const GLint*)) \
    X(void, CompileShader, (GLuint)) \
    X(void, GetShaderiv, (GLuint, GLenum, GLint*)) \
    X(void, GetShaderInfoLog, (GLuint, GLsizei, GLsizei*, char*)) \
    X(void, DeleteShader, (GLuint)) \
    X(GLuint, CreateProgram, ()) \
    X(void, AttachShader, (GLuint, GLuint)) \
    X(void, BindAttribLocation, (GLuint, GLuint, const char*)) \
    X(void, LinkProgram, (GLuint)) \
    X(void, GetProgramiv, (GLuint, GLenum, GLint*)) \
    X(void, GetProgramInfoLog, (GLuint, GLsizei, GLsizei*, char*)) \
    X(void, DeleteProgram, (GLuint)) \
    X(void, UseProgram, (GLuint)) \
    X(GLint, GetUniformLocation, (GLuint, const char*)) \
    X(void, UniformMatrix4fv, (GLint, GLsizei, GLboolean, const GLfloat*)) \
    X(void, Uniform1i, (GLint, GLint)) \
    X(void, Uniform1f, (GLint, GLfloat)) \
    X(void, Uniform3f, (GLint, GLfloat, GLfloat, GLfloat)) \
    X(void, ActiveTexture, (GLenum)) \
    X(void, GenBuffers, (GLsizei, GLuint*)) \
    X(void, BindBuffer, (GLenum, GLuint)) \
    X(void, BufferData, (GLenum, SizeIPtr, const void*, GLenum)) \
    X(void, DeleteBuffers, (GLsizei, const GLuint*)) \
    X(void, EnableVertexAttribArray, (GLuint)) \
    X(void, VertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void*)) \
    X(void, GenVertexArrays, (GLsizei, GLuint*)) \
    X(void, BindVertexArray, (GLuint)) \
    X(void, DeleteVertexArrays, (GLsizei, const GLuint*)) \
    X(void, GenFramebuffers, (GLsizei, GLuint*)) \
    X(void, BindFramebuffer, (GLenum, GLuint)) \
    X(void, DeleteFramebuffers, (GLsizei, const GLuint*)) \
    X(void, FramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint)) \
    X(GLenum, CheckFramebufferStatus, (GLenum)) \
    X(void, GenRenderbuffers, (GLsizei, GLuint*)) \
    X(void, BindRenderbuffer, (GLenum, GLuint)) \
    X(void, DeleteRenderbuffers, (GLsizei, const GLuint*)) \
    X(void, RenderbufferStorage, (GLenum, GLenum, GLsizei, GLsizei)) \
    X(void, FramebufferRenderbuffer, (GLenum, GLenum, GLenum, GLuint)) \
    X(void, RenderbufferStorageMultisample, (GLenum, GLsizei, GLenum, GLsizei, GLsizei)) \
    X(void, BlitFramebuffer, (GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum))

#define PKZGUI_GL3_DECLARE(ret, name, args) extern ret(*name) args;
    PKZGUI_GL3_FUNCTIONS(PKZGUI_GL3_DECLARE)
#undef PKZGUI_GL3_DECLARE

    void Load();
    bool IsLoaded();
}

class GLStateGuard
{
public:
    GLStateGuard();
    ~GLStateGuard();
    GLStateGuard(const GLStateGuard&) = delete;
    GLStateGuard& operator=(const GLStateGuard&) = delete;

private:
    GLint drawFramebuffer = 0, readFramebuffer = 0, viewport[4]{};
    GLint program = 0, vertexArray = 0, arrayBuffer = 0, renderbuffer = 0;
    GLint activeTexture = 0, texture = 0;
    GLint depthFunc = 0, cullMode = 0, frontFace = 0;
    GLfloat clearColor[4]{};
    GLdouble clearDepth = 1.0;
    GLboolean depthTest = 0, cullFace = 0, scissorTest = 0, blend = 0, depthMask = 0, colorMask[4]{};
};

class PixelUnpackGuard
{
public:
    PixelUnpackGuard();
    ~PixelUnpackGuard();
    PixelUnpackGuard(const PixelUnpackGuard&) = delete;
    PixelUnpackGuard& operator=(const PixelUnpackGuard&) = delete;

private:
    GLint alignment = 4, rowLength = 0, skipRows = 0, skipPixels = 0, buffer = 0;
};

GLuint CreateRgbaTexture(const unsigned char* rgba, GLsizei width, GLsizei height, GLint wrap);

struct Mat4
{
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

    static Mat4 Perspective(float fovY, float aspect, float zNear, float zFar)
    {
        const float f = 1.0f / std::tan(fovY * 0.5f);
        Mat4 result;
        result.m[0] = f / aspect;
        result.m[5] = f;
        result.m[10] = (zFar + zNear) / (zNear - zFar);
        result.m[11] = -1.0f;
        result.m[14] = (2.0f * zFar * zNear) / (zNear - zFar);
        result.m[15] = 0.0f;
        return result;
    }

    static Mat4 LookAt(const BUVector3& eye, const BUVector3& target, const BUVector3& up)
    {
        BUVector3 forward = target - eye;
        forward.Normalize();
        BUVector3 side = forward.Cross(up);
        side.Normalize();
        const BUVector3 cameraUp = side.Cross(forward);

        Mat4 result;
        result.m[0] = side.x;
        result.m[4] = side.y;
        result.m[8] = side.z;
        result.m[1] = cameraUp.x;
        result.m[5] = cameraUp.y;
        result.m[9] = cameraUp.z;
        result.m[2] = -forward.x;
        result.m[6] = -forward.y;
        result.m[10] = -forward.z;
        result.m[12] = -side.Dot(eye);
        result.m[13] = -cameraUp.Dot(eye);
        result.m[14] = forward.Dot(eye);
        return result;
    }

    Mat4 operator*(const Mat4& other) const
    {
        Mat4 result;
        for (int column = 0; column < 4; ++column)
            for (int row = 0; row < 4; ++row)
            {
                float sum = 0.0f;
                for (int k = 0; k < 4; ++k)
                    sum += m[k * 4 + row] * other.m[column * 4 + k];
                result.m[column * 4 + row] = sum;
            }
        return result;
    }
};
