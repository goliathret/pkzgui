#include "render/gl.h"
#include <stdexcept>
#include <string>

void ApplyOpenGLContextHints()
{
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
#ifdef __APPLE__
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#else
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#endif
}

namespace gl3
{
#define PKZGUI_GL3_DEFINE(ret, name, args) ret(*name) args = nullptr;
    PKZGUI_GL3_FUNCTIONS(PKZGUI_GL3_DEFINE)
#undef PKZGUI_GL3_DEFINE

    namespace
    {
        bool loaded = false;

        template<typename Function>
        void Resolve(Function& function, const char* name)
        {
            function = reinterpret_cast<Function>(glfwGetProcAddress(name));
            if (!function)
                throw std::runtime_error(std::string("Missing OpenGL entry point: ") + name);
        }
    }

    void Load()
    {
        if (loaded)
            return;
#define PKZGUI_GL3_RESOLVE(ret, name, args) Resolve(name, "gl" #name);
        PKZGUI_GL3_FUNCTIONS(PKZGUI_GL3_RESOLVE)
#undef PKZGUI_GL3_RESOLVE
        loaded = true;
    }

    bool IsLoaded()
    {
        return loaded;
    }
}

namespace
{
    void SetEnabled(GLenum capability, GLboolean enabled)
    {
        if (enabled)
            glEnable(capability);
        else
            glDisable(capability);
    }
}

GLStateGuard::GLStateGuard()
{
    gl3::Load();
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFramebuffer);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFramebuffer);
    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vertexArray);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &arrayBuffer);
    glGetIntegerv(GL_RENDERBUFFER_BINDING, &renderbuffer);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
    gl3::ActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
    glGetIntegerv(GL_DEPTH_FUNC, &depthFunc);
    glGetIntegerv(GL_CULL_FACE_MODE, &cullMode);
    glGetIntegerv(GL_FRONT_FACE, &frontFace);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, clearColor);
    glGetDoublev(GL_DEPTH_CLEAR_VALUE, &clearDepth);
    depthTest = glIsEnabled(GL_DEPTH_TEST);
    cullFace = glIsEnabled(GL_CULL_FACE);
    scissorTest = glIsEnabled(GL_SCISSOR_TEST);
    blend = glIsEnabled(GL_BLEND);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
    glGetBooleanv(GL_COLOR_WRITEMASK, colorMask);
}

GLStateGuard::~GLStateGuard()
{
    gl3::BindFramebuffer(GL_DRAW_FRAMEBUFFER, drawFramebuffer);
    gl3::BindFramebuffer(GL_READ_FRAMEBUFFER, readFramebuffer);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    gl3::UseProgram(program);
    gl3::BindVertexArray(vertexArray);
    gl3::BindBuffer(GL_ARRAY_BUFFER, arrayBuffer);
    gl3::BindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
    gl3::ActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    gl3::ActiveTexture(activeTexture);
    glDepthFunc(depthFunc);
    glCullFace(cullMode);
    glFrontFace(frontFace);
    glClearColor(clearColor[0], clearColor[1], clearColor[2], clearColor[3]);
    glClearDepth(clearDepth);
    glDepthMask(depthMask);
    glColorMask(colorMask[0], colorMask[1], colorMask[2], colorMask[3]);
    SetEnabled(GL_DEPTH_TEST, depthTest);
    SetEnabled(GL_CULL_FACE, cullFace);
    SetEnabled(GL_SCISSOR_TEST, scissorTest);
    SetEnabled(GL_BLEND, blend);
}

PixelUnpackGuard::PixelUnpackGuard()
{
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
    glGetIntegerv(GL_UNPACK_ROW_LENGTH, &rowLength);
    glGetIntegerv(GL_UNPACK_SKIP_ROWS, &skipRows);
    glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &skipPixels);
    glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &buffer);
    gl3::BindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
}

PixelUnpackGuard::~PixelUnpackGuard()
{
    glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, rowLength);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, skipRows);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, skipPixels);
    gl3::BindBuffer(GL_PIXEL_UNPACK_BUFFER, buffer);
}

GLuint CreateRgbaTexture(const unsigned char* rgba, GLsizei width, GLsizei height, GLint wrap)
{
    const GLStateGuard state;
    const PixelUnpackGuard unpack;
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
    GLint uploadedWidth = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &uploadedWidth);
    if (uploadedWidth != width)
    {
        glDeleteTextures(1, &texture);
        throw std::runtime_error("Could not allocate the texture on the GPU.");
    }
    return texture;
}
