#include "model/model_renderer.h"
#include "render/gl.h"
#include <algorithm>
#include <stdexcept>
#include <string>

namespace
{
    constexpr float kFieldOfView = 0.785398f;
    constexpr float kNearPlane = 0.05f;
    constexpr float kClearColor[4] = {0.18f, 0.19f, 0.22f, 1.0f};
    constexpr float kLightDirection[3] = {-0.4f, -0.8f, -0.3f};

    enum Attribute : GLuint
    {
        kPositionAttribute = 0,
        kNormalAttribute = 1,
        kTexCoordAttribute = 2,
    };

    const char* const kVertexShader = R"(
in vec3 aPos;
in vec3 aNrm;
in vec2 aUV;
uniform mat4 uMVP;
uniform mat4 uModel;
uniform float uUVDivisor;
out vec3 vNrm;
out vec3 vWorld;
out vec2 vUV;
void main() {
    vec4 world = uModel * vec4(aPos, 1.0);
    vWorld = world.xyz;
    vUV = aUV / uUVDivisor;
    vNrm = mat3(uModel) * aNrm;
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

    const char* const kFragmentShader = R"(
in vec3 vNrm;
in vec3 vWorld;
in vec2 vUV;
uniform sampler2D uTexture;
uniform bool uTextured;
uniform bool uAlphaCutout;
uniform vec3 uLightDir;
uniform vec3 uViewPos;
uniform vec3 uColor;
out vec4 fragColor;
void main() {
    vec3 n = normalize(vNrm);
    vec3 l = normalize(-uLightDir);
    float diffuseTerm = max(dot(n, l), 0.0);
    vec3 viewDir = normalize(uViewPos - vWorld);
    vec3 halfDir = normalize(l + viewDir);
    float specularTerm = pow(max(dot(n, halfDir), 0.0), 32.0);
    vec4 albedo = uTextured ? texture(uTexture, vUV) : vec4(1.0);
    if (uTextured && uAlphaCutout && albedo.a < 0.3) discard;
    vec3 base = uColor * albedo.rgb;
    vec3 ambient = 0.35 * base;
    vec3 diffuse = 0.65 * diffuseTerm * base;
    vec3 specular = 0.25 * specularTerm * vec3(1.0);
    fragColor = vec4(ambient + diffuse + specular, 1.0);
}
)";

    const char* const kGridVertexShader = R"(
in vec3 aPos;
uniform mat4 uMVP;
out vec3 vPos;
void main() {
    vPos = aPos;
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

    const char* const kGridFragmentShader = R"(
in vec3 vPos;
uniform vec3 uColor;
out vec4 fragColor;
void main() {
    float dist = length(vPos.xz);
    float alpha = 1.0 - smoothstep(6.5, 10.0, dist);
    fragColor = vec4(uColor, alpha);
}
)";

    GLuint BuildShader(GLenum type, const char* body)
    {
        const char* sources[] = {kGlslVersion, "\n", body};
        const GLuint shader = gl3::CreateShader(type);
        gl3::ShaderSource(shader, 3, sources, nullptr);
        gl3::CompileShader(shader);
        GLint compiled = 0;
        gl3::GetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
        if (!compiled)
        {
            char log[512]{};
            gl3::GetShaderInfoLog(shader, sizeof(log), nullptr, log);
            gl3::DeleteShader(shader);
            throw std::runtime_error(std::string("Shader compile: ") + log);
        }
        return shader;
    }

    GLuint UploadBuffer(GLenum target, const void* data, size_t bytes)
    {
        GLuint buffer = 0;
        gl3::GenBuffers(1, &buffer);
        gl3::BindBuffer(target, buffer);
        gl3::BufferData(target, static_cast<gl3::SizeIPtr>(bytes), data, GL_STATIC_DRAW);
        return buffer;
    }

    GLuint UploadAttribute(Attribute attribute, const std::vector<float>& values, GLint components)
    {
        const GLuint buffer = UploadBuffer(GL_ARRAY_BUFFER, values.data(), values.size() * sizeof(float));
        gl3::EnableVertexAttribArray(attribute);
        gl3::VertexAttribPointer(attribute, components, GL_FLOAT, GL_FALSE, 0, nullptr);
        return buffer;
    }
}

ModelRenderer::~ModelRenderer()
{
    Release();
}

void ModelRenderer::EnsureGrid()
{
    if (gridVAO)
        return;

    // Create program
    const GLuint vs = BuildShader(GL_VERTEX_SHADER, kGridVertexShader);
    GLuint fs = 0;
    try { fs = BuildShader(GL_FRAGMENT_SHADER, kGridFragmentShader); }
    catch (...) { gl3::DeleteShader(vs); throw; }
    gridProgram = gl3::CreateProgram();
    gl3::AttachShader(gridProgram, vs);
    gl3::AttachShader(gridProgram, fs);
    gl3::BindAttribLocation(gridProgram, 0, "aPos");
    gl3::LinkProgram(gridProgram);
    gl3::DeleteShader(vs);
    gl3::DeleteShader(fs);
    GLint linked = 0;
    gl3::GetProgramiv(gridProgram, GL_LINK_STATUS, &linked);
    if (!linked)
    {
        char log[512]{};
        gl3::GetProgramInfoLog(gridProgram, sizeof(log), nullptr, log);
        gl3::DeleteProgram(gridProgram);
        gridProgram = 0;
        throw std::runtime_error(std::string("Grid program link: ") + log);
    }

    // Generate grid lines (XZ plane, Y = 0)
    const int halfSize = 10;          // 20 x 20 units
    const float step = 1.0f;
    std::vector<float> vertices;

    for (int i = -halfSize; i <= halfSize; ++i)
    {
        float t = i * step;

        // line parallel to X
        vertices.push_back(-halfSize * step); vertices.push_back(0.0f); vertices.push_back(t);
        vertices.push_back( halfSize * step); vertices.push_back(0.0f); vertices.push_back(t);

        // line parallel to Z
        vertices.push_back(t); vertices.push_back(0.0f); vertices.push_back(-halfSize * step);
        vertices.push_back(t); vertices.push_back(0.0f); vertices.push_back( halfSize * step);
    }

    gridVertexCount = static_cast<int>(vertices.size() / 3);

    gl3::GenVertexArrays(1, &gridVAO);
    gl3::BindVertexArray(gridVAO);
    gridVBO = UploadBuffer(GL_ARRAY_BUFFER, vertices.data(), vertices.size() * sizeof(float));
    gl3::EnableVertexAttribArray(0);
    gl3::VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    gl3::BindVertexArray(0);
}

void ModelRenderer::Render(const std::vector<RZGeometry::Mesh>& meshes, const std::vector<MeshStyle>& styles,
    const OrbitCamera& camera, bool twoSided, int width, int height)
{
    const GLStateGuard savedState;
    EnsureProgram();
    EnsureFramebuffer(width, height);
    EnsureWhiteTexture();
    if (gpuMeshes.empty())
        UploadMeshes(meshes);

    gl3::BindFramebuffer(GL_FRAMEBUFFER, msaaFramebuffer ? msaaFramebuffer : framebuffer);
    glViewport(0, 0, width, height);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glClearColor(kClearColor[0], kClearColor[1], kClearColor[2], kClearColor[3]);
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    if (twoSided)
        glDisable(GL_CULL_FACE);
    else
        glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    const BUVector3 eye = camera.Eye();
    const Mat4 model;
    const Mat4 view = Mat4::LookAt(eye, camera.target, BUVector3(0.0f, 1.0f, 0.0f));
    const Mat4 projection = Mat4::Perspective(kFieldOfView, float(width) / float(height), kNearPlane,
        camera.distance * 20.0f + 10.0f);
    const Mat4 modelViewProjection = projection * (view * model);

    EnsureGrid();
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    gl3::UseProgram(gridProgram);
    gl3::UniformMatrix4fv(gl3::GetUniformLocation(gridProgram, "uMVP"), 1, GL_FALSE, modelViewProjection.m);
    gl3::Uniform3f(gl3::GetUniformLocation(gridProgram, "uColor"), 0.35f, 0.37f, 0.40f);

    gl3::BindVertexArray(gridVAO);
    glDrawArrays(GL_LINES, 0, gridVertexCount);
    gl3::BindVertexArray(0);
    glDisable(GL_BLEND);

    gl3::UseProgram(program);
    gl3::UniformMatrix4fv(Uniform("uMVP"), 1, GL_FALSE, modelViewProjection.m);
    gl3::UniformMatrix4fv(Uniform("uModel"), 1, GL_FALSE, model.m);
    gl3::Uniform3f(Uniform("uLightDir"), kLightDirection[0], kLightDirection[1], kLightDirection[2]);
    gl3::Uniform3f(Uniform("uViewPos"), eye.x, eye.y, eye.z);
    gl3::Uniform1i(Uniform("uTexture"), 0);

    for (size_t i = 0; i < meshes.size(); ++i)
    {
        if (meshes[i].indices.empty())
            continue;
        const MeshStyle& style = styles[i];

        if (style.texture == 0 && !drawNoTex)
            continue;

        gl3::Uniform1f(Uniform("uUVDivisor"), style.uvDivisor);
        gl3::Uniform1i(Uniform("uTextured"), style.texture != 0);
        gl3::Uniform1i(Uniform("uAlphaCutout"), style.alphaCutout);
        gl3::Uniform3f(Uniform("uColor"), style.color[0], style.color[1], style.color[2]);
        glBindTexture(GL_TEXTURE_2D, style.texture ? style.texture : whiteTexture);
        gl3::BindVertexArray(gpuMeshes[i].vertexArray);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(meshes[i].indices.size()), GL_UNSIGNED_INT, nullptr);
    }

    if (msaaFramebuffer)
    {
        gl3::BindFramebuffer(GL_READ_FRAMEBUFFER, msaaFramebuffer);
        gl3::BindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
        gl3::BlitFramebuffer(0, 0, width, height,
            0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }
}

void ModelRenderer::EnsureSkeleton()
{
    if (skeletonLineVAO)
        return;
    gl3::GenVertexArrays(1, &skeletonLineVAO);
    gl3::GenBuffers(1, &skeletonLineVBO);
    gl3::GenVertexArrays(1, &skeletonJointVAO);
    gl3::GenBuffers(1, &skeletonJointVBO);
}

void ModelRenderer::RenderSkeleton(const std::vector<BUVector3>& positions, const std::vector<uint32_t>& parents,
    const OrbitCamera& camera, int width, int height)
{
    const GLStateGuard savedState;
    EnsureFramebuffer(width, height);
    EnsureGrid();
    EnsureSkeleton();

    gl3::BindFramebuffer(GL_FRAMEBUFFER, msaaFramebuffer ? msaaFramebuffer : framebuffer);
    glViewport(0, 0, width, height);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glClearColor(kClearColor[0], kClearColor[1], kClearColor[2], kClearColor[3]);
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_CULL_FACE);

    const BUVector3 eye = camera.Eye();
    const Mat4 model;
    const Mat4 view = Mat4::LookAt(eye, camera.target, BUVector3(0.0f, 1.0f, 0.0f));
    const Mat4 projection = Mat4::Perspective(kFieldOfView, float(width) / float(height), kNearPlane,
        camera.distance * 20.0f + 10.0f);
    const Mat4 modelViewProjection = projection * (view * model);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    gl3::UseProgram(gridProgram);
    gl3::UniformMatrix4fv(gl3::GetUniformLocation(gridProgram, "uMVP"), 1, GL_FALSE, modelViewProjection.m);
    gl3::Uniform3f(gl3::GetUniformLocation(gridProgram, "uColor"), 0.35f, 0.37f, 0.40f);

    gl3::BindVertexArray(gridVAO);
    glDrawArrays(GL_LINES, 0, gridVertexCount);
    gl3::BindVertexArray(0);
    glDisable(GL_BLEND);

    std::vector<float> lineVerts;
    lineVerts.reserve(positions.size() * 6);
    for (size_t i = 0; i < positions.size() && i < parents.size(); ++i)
    {
        const uint32_t parent = parents[i];
        if (parent >= positions.size())
            continue;
        const BUVector3& a = positions[i];
        const BUVector3& b = positions[parent];
        lineVerts.push_back(a.x);
        lineVerts.push_back(a.y);
        lineVerts.push_back(a.z);
        lineVerts.push_back(b.x);
        lineVerts.push_back(b.y);
        lineVerts.push_back(b.z);
    }

    if (!lineVerts.empty())
    {
        gl3::BindVertexArray(skeletonLineVAO);
        gl3::BindBuffer(GL_ARRAY_BUFFER, skeletonLineVBO);
        gl3::BufferData(GL_ARRAY_BUFFER, static_cast<gl3::SizeIPtr>(lineVerts.size() * sizeof(float)), lineVerts.data(),
            GL_STATIC_DRAW);
        gl3::EnableVertexAttribArray(0);
        gl3::VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
        gl3::Uniform3f(gl3::GetUniformLocation(gridProgram, "uColor"), 1.0f, 0.15f, 0.15f);
        glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(lineVerts.size() / 3));
        gl3::BindVertexArray(0);
    }

    BUVector3 forward = camera.target - eye;
    forward.Normalize();
    BUVector3 right = forward.Cross(BUVector3(0.0f, 1.0f, 0.0f));
    if (right.Length() < 1e-4f)
        right = forward.Cross(BUVector3(0.0f, 0.0f, 1.0f));
    right.Normalize();
    BUVector3 up = right.Cross(forward);
    up.Normalize();

    const float half = std::max(0.01f, camera.distance * 0.008f);
    std::vector<float> jointVerts;
    jointVerts.reserve(positions.size() * 18);
    for (const BUVector3& p : positions)
    {
        const BUVector3 c0 = p + (right * -half) + (up * -half);
        const BUVector3 c1 = p + (right * half) + (up * -half);
        const BUVector3 c2 = p + (right * half) + (up * half);
        const BUVector3 c3 = p + (right * -half) + (up * half);
        jointVerts.push_back(c0.x); jointVerts.push_back(c0.y); jointVerts.push_back(c0.z);
        jointVerts.push_back(c1.x); jointVerts.push_back(c1.y); jointVerts.push_back(c1.z);
        jointVerts.push_back(c2.x); jointVerts.push_back(c2.y); jointVerts.push_back(c2.z);
        jointVerts.push_back(c0.x); jointVerts.push_back(c0.y); jointVerts.push_back(c0.z);
        jointVerts.push_back(c2.x); jointVerts.push_back(c2.y); jointVerts.push_back(c2.z);
        jointVerts.push_back(c3.x); jointVerts.push_back(c3.y); jointVerts.push_back(c3.z);
    }

    if (!jointVerts.empty())
    {
        gl3::BindVertexArray(skeletonJointVAO);
        gl3::BindBuffer(GL_ARRAY_BUFFER, skeletonJointVBO);
        gl3::BufferData(GL_ARRAY_BUFFER, static_cast<gl3::SizeIPtr>(jointVerts.size() * sizeof(float)), jointVerts.data(),
            GL_STATIC_DRAW);
        gl3::EnableVertexAttribArray(0);
        gl3::VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
        gl3::Uniform3f(gl3::GetUniformLocation(gridProgram, "uColor"), 1.0f, 1.0f, 1.0f);
        glDisable(GL_DEPTH_TEST);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(jointVerts.size() / 3));
        glEnable(GL_DEPTH_TEST);
        gl3::BindVertexArray(0);
    }

    if (msaaFramebuffer)
    {
        gl3::BindFramebuffer(GL_READ_FRAMEBUFFER, msaaFramebuffer);
        gl3::BindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
        gl3::BlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }
}

ModelRenderer::Texture ModelRenderer::UploadTexture(const std::vector<uint8_t>& rgba, uint32_t width, uint32_t height,
    bool clampToEdge)
{
    const size_t bytes = size_t(width) * height * 4;
    if (!width || !height || width > kMaxTextureSize || height > kMaxTextureSize || rgba.size() != bytes)
        throw std::runtime_error("Model textures must be between 1 and 4096 pixels per side.");
    if (bytes > kTextureBudget - textureBytes)
        throw std::runtime_error("Model textures exceed the 128 MiB preview budget.");
    const GLuint id = CreateRgbaTexture(rgba.data(), GLsizei(width), GLsizei(height),
        clampToEdge ? GL_CLAMP_TO_EDGE : GL_REPEAT);
    textureBytes += bytes;
    return {id, bytes};
}

void ModelRenderer::DeleteTexture(Texture& texture)
{
    if (texture.id)
        glDeleteTextures(1, &texture.id);
    textureBytes -= std::min(textureBytes, texture.bytes);
    texture = {};
}

void ModelRenderer::Release()
{
    if (gl3::IsLoaded())
    {
        for (const GpuMesh& mesh : gpuMeshes)
        {
            gl3::DeleteVertexArrays(1, &mesh.vertexArray);
            const GLuint buffers[] = {mesh.positions, mesh.normals, mesh.texcoords, mesh.indices};
            gl3::DeleteBuffers(4, buffers);
        }
        if (whiteTexture)
            glDeleteTextures(1, &whiteTexture);
        ReleaseFramebuffer();
        if (program)
            gl3::DeleteProgram(program);
        if (gridProgram)
            gl3::DeleteProgram(gridProgram);
        if (gridVAO)
            gl3::DeleteVertexArrays(1, &gridVAO);
        if (gridVBO)
            gl3::DeleteBuffers(1, &gridVBO);
        if (skeletonLineVAO)
            gl3::DeleteVertexArrays(1, &skeletonLineVAO);
        if (skeletonLineVBO)
            gl3::DeleteBuffers(1, &skeletonLineVBO);
        if (skeletonJointVAO)
            gl3::DeleteVertexArrays(1, &skeletonJointVAO);
        if (skeletonJointVBO)
            gl3::DeleteBuffers(1, &skeletonJointVBO);
    }
    gpuMeshes.clear();
    whiteTexture = 0;
    framebuffer = colorTexture = depthBuffer = 0;
    frameWidth = frameHeight = 0;
    program = 0;
    gridProgram = gridVAO = gridVBO = 0;
    gridVertexCount = 0;
    skeletonLineVAO = skeletonLineVBO = skeletonJointVAO = skeletonJointVBO = 0;
    textureBytes = 0;
}

void ModelRenderer::EnsureProgram()
{
    if (program)
        return;
    const GLuint vertexShader = BuildShader(GL_VERTEX_SHADER, kVertexShader);
    GLuint fragmentShader = 0;
    try
    {
        fragmentShader = BuildShader(GL_FRAGMENT_SHADER, kFragmentShader);
    }
    catch (...)
    {
        gl3::DeleteShader(vertexShader);
        throw;
    }

    program = gl3::CreateProgram();
    gl3::AttachShader(program, vertexShader);
    gl3::AttachShader(program, fragmentShader);
    gl3::BindAttribLocation(program, kPositionAttribute, "aPos");
    gl3::BindAttribLocation(program, kNormalAttribute, "aNrm");
    gl3::BindAttribLocation(program, kTexCoordAttribute, "aUV");
    gl3::LinkProgram(program);
    gl3::DeleteShader(vertexShader);
    gl3::DeleteShader(fragmentShader);

    GLint linked = 0;
    gl3::GetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked)
    {
        char log[512]{};
        gl3::GetProgramInfoLog(program, sizeof(log), nullptr, log);
        gl3::DeleteProgram(program);
        program = 0;
        throw std::runtime_error(std::string("Program link: ") + log);
    }
}

void ModelRenderer::EnsureFramebuffer(int width, int height)
{
    width = std::max(width, 1);
    height = std::max(height, 1);
    if (HasFrame(width, height))
        return;

    ReleaseFramebuffer();
    GLint maxSamples = 0;
    glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
    if (maxSamples >= 2)
    {
        gl3::GenFramebuffers(1, &msaaFramebuffer);
        gl3::BindFramebuffer(GL_FRAMEBUFFER, msaaFramebuffer);
        gl3::GenRenderbuffers(1, &msaaColorBuffer);
        gl3::GenRenderbuffers(1, &msaaDepthBuffer);
        bool complete = false;
        for (int samples = msaaSamples; samples >= 2; samples /= 2)
        {
            if (samples > maxSamples)
                continue;
            gl3::BindRenderbuffer(GL_RENDERBUFFER, msaaColorBuffer);
            gl3::RenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, width, height);
            gl3::FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msaaColorBuffer);
            gl3::BindRenderbuffer(GL_RENDERBUFFER, msaaDepthBuffer);
            gl3::RenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH_COMPONENT24, width, height);
            gl3::FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, msaaDepthBuffer);
            complete = gl3::CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
            if (complete)
                break;
        }
        if (!complete)
            ReleaseMultisampleFramebuffer();
    }

    gl3::GenFramebuffers(1, &framebuffer);
    gl3::BindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glGenTextures(1, &colorTexture);
    glBindTexture(GL_TEXTURE_2D, colorTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl3::FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTexture, 0);
    if (!msaaFramebuffer)
    {
        gl3::GenRenderbuffers(1, &depthBuffer);
        gl3::BindRenderbuffer(GL_RENDERBUFFER, depthBuffer);
        gl3::RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
        gl3::FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthBuffer);
    }
    if (gl3::CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        ReleaseFramebuffer();
        throw std::runtime_error("Incomplete framebuffer for model preview.");
    }
    frameWidth = width;
    frameHeight = height;
}

void ModelRenderer::EnsureWhiteTexture()
{
    if (whiteTexture)
        return;
    const unsigned char white[4] = {255, 255, 255, 255};
    whiteTexture = CreateRgbaTexture(white, 1, 1, GL_REPEAT);
}

void ModelRenderer::UploadMeshes(const std::vector<RZGeometry::Mesh>& meshes)
{
    gpuMeshes.reserve(meshes.size());
    for (const RZGeometry::Mesh& mesh : meshes)
    {
        GpuMesh gpu;
        gl3::GenVertexArrays(1, &gpu.vertexArray);
        gl3::BindVertexArray(gpu.vertexArray);
        gpu.positions = UploadAttribute(kPositionAttribute, mesh.positions, 3);
        gpu.normals = UploadAttribute(kNormalAttribute, mesh.normals, 3);
        gpu.texcoords = UploadAttribute(kTexCoordAttribute, mesh.texcoords, 2);
        gpu.indices = UploadBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.indices.data(), mesh.indices.size() * sizeof(uint32_t));
        gpuMeshes.push_back(gpu);
    }
}

void ModelRenderer::ReleaseMultisampleFramebuffer()
{
    if (msaaColorBuffer)
        gl3::DeleteRenderbuffers(1, &msaaColorBuffer);
    if (msaaDepthBuffer)
        gl3::DeleteRenderbuffers(1, &msaaDepthBuffer);
    if (msaaFramebuffer)
        gl3::DeleteFramebuffers(1, &msaaFramebuffer);
    msaaColorBuffer = msaaDepthBuffer = msaaFramebuffer = 0;
}

void ModelRenderer::ReleaseFramebuffer()
{
    ReleaseMultisampleFramebuffer();
    if (colorTexture)
        glDeleteTextures(1, &colorTexture);
    if (depthBuffer)
        gl3::DeleteRenderbuffers(1, &depthBuffer);
    if (framebuffer)
        gl3::DeleteFramebuffers(1, &framebuffer);
    colorTexture = depthBuffer = framebuffer = 0;
    frameWidth = frameHeight = 0;
}

int ModelRenderer::Uniform(const char* name) const
{
    return gl3::GetUniformLocation(program, name);
}