#include "model/model_preview.h"
#include "platform/platform.h"
#include "render/gl.h"
#include "texture/texture.h"
#include "BaseUtils/BUPng.h"
#include "imgui.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <set>
#include <stdexcept>

namespace
{
    constexpr GLenum kAttachmentObjectName = 0x8CD1;

    void Check(bool ok, const char* message)
    {
        if (!ok)
            throw std::runtime_error(message);
    }

    template<typename Function>
    bool Throws(Function&& function)
    {
        try
        {
            function();
        }
        catch (const std::exception&)
        {
            return true;
        }
        return false;
    }

    void PutBE32(std::vector<uint8_t>& bytes, size_t at, uint32_t value)
    {
        for (int i = 0; i < 4; ++i)
            bytes[at + i] = uint8_t(value >> (24 - 8 * i));
    }

    CMChunk Geometry(const std::string& name, uint32_t vertexCount = 3)
    {
        auto resource = CMChunk::Container(Gen_PostLoadData, 1);
        resource.AddChild(CMChunkResourceHeader::Build(0, kCMResourceType_Geometry, 0, 0, 0, 0, name));
        auto lod = CMChunk::Container(Geo_GeomLOD, 1);
        auto descriptor = CMChunk::Leaf(Geo_GeomMesh, 1, {});
        for (uint32_t value : {0u, 0u, 20u, 0u, vertexCount, 0u, 3u, 0u, 0u, 0u, 0u, 0u})
            descriptor.Append(value);
        auto vertices = CMChunk::Leaf(GenSub_VertexList, 1, {});
        for (uint32_t i = 0; i < vertexCount; ++i)
        {
            vertices.Append(i == 0 ? -1.0f : (i == 1 ? 1.0f : 0.0f));
            vertices.Append(i == 2 ? 1.0f : 0.0f);
            vertices.Append(0.0f);
            vertices.Append(0x7FC00000u);
            vertices.Append(0x7FC00000u);
        }
        auto indices = CMChunk::Leaf(GenSub_IndexList, 1, {});
        for (uint16_t index : {0, 2, 1})
            indices.Append(index);
        lod.AddChild(std::move(descriptor));
        lod.AddChild(std::move(vertices));
        lod.AddChild(std::move(indices));
        resource.AddChild(std::move(lod));
        return resource;
    }

    PackageSession Fixture()
    {
        PackageSession session;
        session.package = std::make_unique<PKPackage>();
        auto& roots = session.package->rootChunks;
        roots.push_back(Geometry("first"));
        roots.push_back(Geometry("second", 4));
        auto missing = CMChunk::Container(GenSub_Resource, 1);
        missing.AddChild(CMChunkResourceHeader::Build(0, kCMResourceType_Geometry, 0, 0, 0, 0, "missing"));
        roots.push_back(std::move(missing));
        auto audio = CMChunk::Container(GenSub_Resource, 1);
        audio.AddChild(CMChunkResourceHeader::Build(0, kCMResourceType_AudioSample, 0, 0, 0, 0, "audio"));
        roots.push_back(std::move(audio));
        session.selected = &roots[0];
        return session;
    }

    CMChunk TestMaterial(uint32_t textureCRC)
    {
        auto chunk = CMChunk::Leaf(Geo_MaterialInfo, 64, std::vector<uint8_t>(244));
        std::memcpy(chunk.data.data(), "fixture", 7);
        for (size_t offset : {100, 104, 108, 112})
            PutBE32(chunk.data, offset, 1);
        chunk.Append(textureCRC);
        chunk.Append<uint32_t>(0);
        chunk.Append<uint32_t>(0);
        chunk.AppendBytes(std::string(128, 'x').data(), 128);
        for (int i = 0; i < 4; ++i)
            chunk.Append(1.0f);
        chunk.Append(0.0f);
        chunk.Append<uint32_t>(0x00000000);
        chunk.Append<uint32_t>(0x01000000);
        chunk.Append<uint32_t>(0);

        const size_t pass = chunk.data.size();
        chunk.data.resize(pass + 188);
        PutBE32(chunk.data, pass, 0x42);
        chunk.Append<uint32_t>(88);
        const size_t declaration = chunk.data.size();
        chunk.data.resize(declaration + 88);
        PutBE32(chunk.data, declaration + 24, 3);
        const uint32_t offsets[] = {4, 16, 24};
        const uint32_t types[] = {0x2a23b9, 0x2a2190, 0x2c2359};
        const uint8_t usages[] = {0, 3, 5};
        for (size_t i = 0; i < 3; ++i)
        {
            const size_t at = declaration + 52 + i * 12;
            PutBE32(chunk.data, at, offsets[i]);
            PutBE32(chunk.data, at + 4, types[i]);
            chunk.data[at + 9] = usages[i];
            chunk.data[at + 10] = i == 2 ? 1 : 0;
        }
        for (int shader = 0; shader < 2; ++shader)
        {
            chunk.Append<uint32_t>(1);
            chunk.Append<uint32_t>(0);
            chunk.Append<uint32_t>(0);
        }
        chunk.length = chunk.data.size();
        return chunk;
    }

    PackageSession TexturedFixture()
    {
        auto session = Fixture();
        auto& resource = session.package->rootChunks[0];
        auto materials = CMChunk::Container(Geo_Geometry, 3);
        materials.AddChild(TestMaterial(0));
        resource.AddChild(std::move(materials));

        auto& lod = resource.children[1];
        auto& descriptor = lod.children[0];
        descriptor.data.clear();
        for (uint32_t value : {0u, 0u, 32u, 0x42u, 3u, 0u, 3u, 0u, 0u, 0u, 0u, 0u})
            descriptor.Append(value);
        auto& vertices = lod.children[1];
        vertices.data.clear();
        for (int i = 0; i < 3; ++i)
        {
            vertices.Append(77.0f);
            vertices.Append(i == 0 ? -1.0f : (i == 1 ? 1.0f : 0.0f));
            vertices.Append(i == 2 ? 1.0f : 0.0f);
            vertices.Append(0.0f);
            vertices.Append<uint32_t>(0x7FC00000);
            vertices.Append<uint32_t>(0);
            vertices.Append<int16_t>(i == 1 ? 1024 : 0);
            vertices.Append<int16_t>(i == 2 ? 1024 : 0);
            vertices.Append<uint32_t>(0);
        }

        std::vector<uint8_t> pixels(16 * 16 * 4, 0);
        for (size_t i = 0; i < pixels.size(); i += 4)
            pixels[i + 1] = 255;
        const auto texture = RZTexture::FromRGBA8("fixture diffuse", pixels.data(), 16, 16);
        resource.children.back().children[0] = TestMaterial(texture.nameCRC);

        auto root = CMChunk::Container(Root, 1);
        auto library = CMChunk::Container(Gen_TextureLibrary, 1);
        library.AddChild(texture.BuildLibraryResource(0x123400));
        root.AddChild(std::move(library));
        auto post = texture.BuildPostLoadData(0x123400);
        post.offset = 0x123400;
        root.AddChild(std::move(post));
        session.package->rootChunks.push_back(std::move(root));
        session.selected = &session.package->rootChunks[0];
        return session;
    }

    void MaterialTests()
    {
        auto session = TexturedFixture();
        ModelPreview preview;
        preview.Update(session);
        Check(preview.Error().empty(), preview.Error().c_str());
        Check(preview.Geometry().materials.size() == 1 && preview.PackageTexture(0) &&
                preview.PackageTexture(0)->GetName() == "fixture diffuse",
            "Material CRC did not resolve its texture");

        const auto& mesh = preview.Geometry().meshes.at(0);
        Check(mesh.positions[0] == -1.0f && mesh.positions[1] == 0.0f && mesh.positions[3] == 1.0f,
            "Declaration position offset was ignored");
        Check(mesh.hasUV && mesh.texcoords[2] == 1024.0f && mesh.texcoords[5] == 1024.0f,
            "UV set 1 or integer short2 was decoded incorrectly");
        Check(mesh.normals[2] > 0.99f, "Packed normal was decoded incorrectly");

        auto& material = session.package->rootChunks[0].children.back().children[0];
        material.data.pop_back();
        ++session.revision;
        preview.Update(session);
        Check(preview.Error().find("truncated") != std::string::npos, "Truncated shader record was accepted");

        material = TestMaterial(0);
        material.data[244 + 140 + 23] = 255;
        ++session.revision;
        preview.Update(session);
        Check(preview.Error().empty() && !preview.PackageTexture(0), "Missing diffuse sampler selected another texture");

        auto image = CMChunk::Leaf(0, 1, std::vector<uint8_t>(0x368 + 40));
        PutBE32(image.data, 0x368, 0x102a1101);
        PutBE32(image.data, 0x36c, 36);
        PutBE32(image.data, 0x370, 4);
        PutBE32(image.data, 0x368 + 36, 0x12345678);
        const CMChunkMaterialInfo::ShaderRecord record{0, 0x368 + 36, 0x368 + 36, 4, 0x368};
        CMChunkMaterialInfo exported;
        exported.name = "test\\shader\"\n";
        exported.source = &image;
        exported.passes.push_back({0, 0, 1, record, {}});
        const auto container = exported.ShaderContainer(record);
        Check(container.size() == 40 && container[0] == 0x10 && container[36] == 0x12,
            "Shader export did not concatenate the virtual and physical container");

        const auto exportPath = (std::filesystem::temp_directory_path() / "pkzgui-model-shader-test.bin").u8string();
        ExportMaterialShaders(exported, exportPath);
        Check(ReadFile(exportPath) == container, "Exported shader bytes differ");
        const auto json = ReadFile(exportPath + ".json");
        Check(std::string(json.begin(), json.end()).find("\\u000a") != std::string::npos,
            "Shader binding manifest contains an unescaped control character");
        std::filesystem::remove(exportPath);
        std::filesystem::remove(exportPath + ".json");

        image.data.pop_back();
        Check(Throws([&] { exported.ShaderContainer(record); }), "Truncated shader physical data was exported");
        std::puts("PASS: material CRCs, declarations, UV sets, packed normals, missing samplers and truncated shaders");
    }

    void ReferenceTests()
    {
        auto inlineModel = Geometry("same name");
        auto descriptors = inlineModel.children[1];
        auto buffers = descriptors;
        descriptors.children.erase(descriptors.children.begin() + 1, descriptors.children.end());
        descriptors.children[0].version = 8;
        buffers.children.erase(buffers.children.begin());
        const auto info = CMChunk::Leaf(Geo_GeomLODInfo, 6, std::vector<uint8_t>(24));
        descriptors.AddChild(info);
        buffers.AddChild(info);

        auto geometry = CMChunk::Container(Geo_Geometry, 3);
        geometry.AddChild(std::move(descriptors));
        auto library = CMChunk::Container(GenSub_Resource, 1);
        library.AddChild(CMChunkResourceHeader::Build(11, kCMResourceType_Geometry, 31, 0, 0x2000, 0, "same name"));
        library.AddChild(std::move(geometry));
        auto post = CMChunk::Container(Gen_PostLoadData, 1);
        post.offset = 0x2000;
        post.AddChild(library.children[0]);
        post.AddChild(std::move(buffers));
        auto decoy = post;
        decoy.offset = 0x1000;
        decoy.children[0] = CMChunkResourceHeader::Build(22, kCMResourceType_Geometry, 31, 0, 0x1000, 0, "same name");
        auto root = CMChunk::Container(Root, 1);
        root.AddChild(std::move(library));
        root.AddChild(std::move(decoy));
        root.AddChild(std::move(post));

        PackageSession session;
        session.package = std::make_unique<PKPackage>();
        session.package->rootChunks.push_back(std::move(root));
        auto& children = session.package->rootChunks[0].children;
        session.selected = &children[0];
        ModelPreview preview;
        preview.Update(session);
        Check(preview.Error().empty() && preview.VertexCount() == 3, "Resource-offset geometry lookup failed");

        children[0].children[0] = CMChunkResourceHeader::Build(11, kCMResourceType_Geometry, 31, 0, 0x1000, 0, "same name");
        ++session.revision;
        preview.Update(session);
        Check(preview.Error().find("does not match") != std::string::npos, "A same-name model at the wrong resource offset was used");

        children[0].children[0] = CMChunkResourceHeader::Build(11, kCMResourceType_Geometry, 31, 0, 0x2000, 0, "same name");
        children[2].children[1].children.back().data[15] = 1;
        ++session.revision;
        preview.Update(session);
        Check(preview.Error().find("LOD") != std::string::npos, "Mismatched LOD descriptors and buffers were paired");
        std::puts("PASS: exact resource offsets, same-name collisions and matching LOD indices");
    }

    void SelectionTests()
    {
        auto session = Fixture();
        ModelPreview preview;
        preview.Update(session);
        Check(preview.IsActive() && preview.Error().empty() && preview.VertexCount() == 3 && preview.TriangleCount() == 1,
            "Valid padded vertex stride was rejected or decoded incorrectly");

        session.selected = &session.package->rootChunks[1];
        preview.Update(session);
        Check(preview.Name() == "second" && preview.VertexCount() == 4 && preview.Error().empty(), "Wrong model selected");

        session.selected = &session.package->rootChunks[2];
        preview.Update(session);
        Check(preview.IsActive() && !preview.Error().empty() && preview.VertexCount() == 0, "Missing model displayed unrelated geometry");

        session.selected = &session.package->rootChunks[3];
        preview.Update(session);
        Check(!preview.IsActive(), "Model preview intercepted an audio selection");

        session.selected = nullptr;
        preview.Update(session);
        Check(!preview.IsActive(), "Model preview activated without a selection");

        session.selected = &session.package->rootChunks[0];
        session.package->isLittleEndian = true;
        preview.Update(session);
        Check(preview.Error().find("big-endian") != std::string::npos, "Unsupported byte order was silently decoded");

        session.package->isLittleEndian = false;
        auto& vertices = session.package->rootChunks[0].children[1].children[1].data;
        vertices[0] = 0x7F;
        vertices[1] = 0xC0;
        vertices[2] = vertices[3] = 0;
        ++session.revision;
        preview.Update(session);
        Check(!preview.Error().empty() && preview.VertexCount() == 0, "Non-finite vertices were accepted");
        std::puts("PASS: geometry selection, padded stride, missing resources, audio, byte order and invalid vertices");
    }

    template<typename T>
    T Proc(const char* name)
    {
        auto proc = reinterpret_cast<T>(glfwGetProcAddress(name));
        Check(proc != nullptr, name);
        return proc;
    }

    class GLHarness
    {
    public:
        GLHarness()
        {
            glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
            ApplyOpenGLContextHints();
            window = glfwCreateWindow(600, 480, "Model regression", nullptr, nullptr);
            if (!window)
                return;
            glfwMakeContextCurrent(window);
            ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.DisplaySize = ImVec2(600, 480);
            io.DeltaTime = 1.0f / 60;
            unsigned char* pixels;
            int width, height;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
            genFramebuffers = Proc<void (*)(int, unsigned*)>("glGenFramebuffers");
            bindFramebuffer = Proc<void (*)(unsigned, unsigned)>("glBindFramebuffer");
            deleteFramebuffers = Proc<void (*)(int, const unsigned*)>("glDeleteFramebuffers");
            isFramebuffer = Proc<unsigned char (*)(unsigned)>("glIsFramebuffer");
            isBuffer = Proc<unsigned char (*)(unsigned)>("glIsBuffer");
            genFramebuffers(2, callerFramebuffers);
        }

        ~GLHarness()
        {
            if (!window)
                return;
            bindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
            bindFramebuffer(GL_READ_FRAMEBUFFER, 0);
            deleteFramebuffers(2, callerFramebuffers);
            ImGui::DestroyContext();
            glfwDestroyWindow(window);
        }

        bool Ready() const { return window != nullptr; }

        void BindCallerFramebuffers()
        {
            bindFramebuffer(GL_DRAW_FRAMEBUFFER, callerFramebuffers[0]);
            bindFramebuffer(GL_READ_FRAMEBUFFER, callerFramebuffers[1]);
        }

        void Draw(ModelPreview& preview)
        {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
            ImGui::Begin("Test", nullptr, ImGuiWindowFlags_NoDecoration);
            preview.Draw();
            ImGui::End();
            ImGui::Render();
            Check(preview.Error().empty(), preview.Error().c_str());
            CheckCallerStateRestored();
        }

        unsigned PreviewFramebuffer() const
        {
            unsigned found = 0;
            for (unsigned i = 1; i < 256; ++i)
                if (isFramebuffer(i) && i != callerFramebuffers[0] && i != callerFramebuffers[1])
                    found = i;
            return found;
        }

        size_t CountColor(unsigned framebuffer, bool green)
        {
            bindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
            GLint attachment, width, height;
            Proc<void (*)(unsigned, unsigned, unsigned, int*)>("glGetFramebufferAttachmentParameteriv")(
                GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, kAttachmentObjectName, &attachment);
            glBindTexture(GL_TEXTURE_2D, attachment);
            glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
            glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
            glBindTexture(GL_TEXTURE_2D, 0);
            std::vector<uint8_t> rgba(size_t(width) * height * 4);
            glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
            bindFramebuffer(GL_READ_FRAMEBUFFER, callerFramebuffers[1]);
            size_t count = 0;
            for (size_t i = 0; i < rgba.size(); i += 4)
            {
                const bool isGreen = rgba[i + 1] > rgba[i] + 40 && rgba[i + 1] > rgba[i + 2] + 40;
                const bool isMagenta = rgba[i] > rgba[i + 1] + 40 && rgba[i + 2] > rgba[i + 1] + 40;
                if (green ? isGreen : isMagenta)
                    ++count;
            }
            return count;
        }

        void (*bindFramebuffer)(unsigned, unsigned) = nullptr;
        unsigned char (*isFramebuffer)(unsigned) = nullptr;
        unsigned char (*isBuffer)(unsigned) = nullptr;
        unsigned callerFramebuffers[2]{};

    private:
        void CheckCallerStateRestored() const
        {
            int value, viewport[4];
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &value);
            Check(value == int(callerFramebuffers[0]), "Draw FBO not restored");
            glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &value);
            Check(value == int(callerFramebuffers[1]), "Read FBO not restored");
            glGetIntegerv(GL_VIEWPORT, viewport);
            Check(viewport[0] == 4 && viewport[2] == 73, "Viewport not restored");
            Check(glIsEnabled(GL_SCISSOR_TEST) && glIsEnabled(GL_BLEND) && glIsEnabled(GL_DEPTH_TEST) && glIsEnabled(GL_CULL_FACE),
                "GL enables not restored");
            GLboolean mask;
            glGetBooleanv(GL_DEPTH_WRITEMASK, &mask);
            Check(mask == GL_FALSE, "Depth write mask not restored");
            glGetIntegerv(GL_DEPTH_FUNC, &value);
            Check(value == GL_GREATER, "Depth function not restored");
            glGetIntegerv(GL_FRONT_FACE, &value);
            Check(value == GL_CW, "Winding not restored");
            GLboolean colors[4];
            glGetBooleanv(GL_COLOR_WRITEMASK, colors);
            Check(!colors[0] && colors[1] && !colors[2] && colors[3], "Color write mask not restored");
            for (unsigned binding : {unsigned(GL_CURRENT_PROGRAM), unsigned(GL_VERTEX_ARRAY_BINDING),
                     unsigned(GL_ARRAY_BUFFER_BINDING), unsigned(GL_RENDERBUFFER_BINDING), unsigned(GL_TEXTURE_BINDING_2D)})
            {
                glGetIntegerv(binding, &value);
                Check(value == 0, "GL object binding leaked into caller");
            }
            Check(glGetError() == GL_NO_ERROR, "Model preview generated a GL error");
        }

        GLFWwindow* window = nullptr;
        void (*genFramebuffers)(int, unsigned*) = nullptr;
        void (*deleteFramebuffers)(int, const unsigned*) = nullptr;
    };

    void SetCallerState(GLHarness& gl)
    {
        gl.BindCallerFramebuffers();
        glViewport(4, 5, 73, 67);
        glEnable(GL_SCISSOR_TEST);
        glScissor(0, 0, 1, 1);
        glEnable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glDepthMask(GL_FALSE);
        glColorMask(GL_FALSE, GL_TRUE, GL_FALSE, GL_TRUE);
        glDepthFunc(GL_GREATER);
        glCullFace(GL_FRONT);
        glFrontFace(GL_CW);
    }

    void UntexturedRenderTests(GLHarness& gl)
    {
        auto session = Fixture();
        ModelPreview preview;
        preview.Update(session);
        preview.SetTextured(false);
        gl.Draw(preview);

        std::set<unsigned> buffers;
        for (unsigned i = 1; i < 64; ++i)
            if (gl.isBuffer(i))
                buffers.insert(i);
        const unsigned modelFramebuffer = gl.PreviewFramebuffer();
        Check(buffers.size() == 5 && modelFramebuffer != 0, "Mesh and grid buffers were not retained");

        gl.bindFramebuffer(GL_READ_FRAMEBUFFER, modelFramebuffer);
        std::array<unsigned char, 256 * 256 * 4> image{};
        glReadPixels(0, 0, 256, 256, GL_RGBA, GL_UNSIGNED_BYTE, image.data());
        bool visible = false;
        for (size_t i = 0; i < image.size(); i += 4)
            visible = visible || image[i] > 90;
        Check(visible, "Camera did not show the triangle");

        gl.bindFramebuffer(GL_DRAW_FRAMEBUFFER, modelFramebuffer);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glClearColor(1, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glColorMask(GL_FALSE, GL_TRUE, GL_FALSE, GL_TRUE);
        gl.BindCallerFramebuffers();
        gl.Draw(preview);
        gl.bindFramebuffer(GL_READ_FRAMEBUFFER, modelFramebuffer);
        std::array<unsigned char, 4> pixel{};
        glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
        Check(pixel[0] == 255 && pixel[1] == 0, "Idle preview was rendered again");
        gl.bindFramebuffer(GL_READ_FRAMEBUFFER, gl.callerFramebuffers[1]);

        ImGui::GetIO().DisplaySize = ImVec2(800, 600);
        gl.Draw(preview);
        for (unsigned id : buffers)
            Check(gl.isBuffer(id), "Resize reallocated a mesh or grid buffer");
        preview.Clear();
        for (unsigned id : buffers)
            Check(!gl.isBuffer(id), "Clear leaked a mesh or grid buffer");
    }

    void TexturedRenderTests(GLHarness& gl)
    {
        auto session = TexturedFixture();
        ModelPreview preview;
        preview.Update(session);
        preview.SetTextured(true);
        gl.Draw(preview);
        const unsigned framebuffer = gl.PreviewFramebuffer();
        Check(framebuffer != 0, "Textured preview has no framebuffer");
        const auto greenPixels = [&] { return gl.CountColor(framebuffer, true); };
        const auto magentaPixels = [&] { return gl.CountColor(framebuffer, false); };
        Check(greenPixels() > 100, "Package texture missing, UV not uploaded, or non-opacity alpha discarded the mesh");

        const auto dir = std::filesystem::temp_directory_path() / "pkzgui-model-preview-test";
        std::filesystem::create_directories(dir);
        const auto png = dir / "override.png";
        const std::vector<uint8_t> magenta = {255, 0, 255, 255};
        WriteFile(png, BUPng::EncodeRGBA8(magenta.data(), 1, 1));
        const auto selected = session.selected;
        const auto before = RZTexture::Load(*session.package, *preview.PackageTexture(0)).DecodeLevel0();

        const auto activeTexture = Proc<void (*)(unsigned)>("glActiveTexture");
        unsigned sentinel;
        glGenTextures(1, &sentinel);
        activeTexture(GL_TEXTURE0 + 3);
        glBindTexture(GL_TEXTURE_2D, sentinel);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 8);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 9);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, 2);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, 1);
        preview.LoadRetexture(0, png.u8string());
        GLint saved;
        glGetIntegerv(GL_ACTIVE_TEXTURE, &saved);
        Check(saved == GL_TEXTURE0 + 3, "PNG upload changed the active texture unit");
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &saved);
        Check(saved == GLint(sentinel), "PNG upload changed another texture unit's binding");
        glGetIntegerv(GL_UNPACK_ROW_LENGTH, &saved);
        Check(saved == 9, "PNG upload leaked pixel unpack state");
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
        glDeleteTextures(1, &sentinel);
        activeTexture(GL_TEXTURE0);

        gl.Draw(preview);
        Check(magentaPixels() > 100, "PNG override did not reach the material sampler");
        Check(!session.dirty && session.selected == selected &&
                before == RZTexture::Load(*session.package, *preview.PackageTexture(0)).DecodeLevel0(),
            "Preview-only override changed the package");

        preview.UsePackageTexture(0);
        gl.Draw(preview);
        Check(greenPixels() > 100, "Use package texture did not restore the original image");
        preview.UseRetexture(0);
        gl.Draw(preview);
        Check(magentaPixels() > 100 && !session.dirty, "Switching back to the retexture lost the PNG or modified the package");
        preview.SetTextured(false);
        gl.Draw(preview);
        Check(magentaPixels() < 100 && !session.dirty, "Texture visibility toggle did not hide the retexture");
        preview.SetTextured(true);
        gl.Draw(preview);
        Check(magentaPixels() > 100, "Texture visibility toggle lost the active retexture");
        preview.ClearRetexture(0);
        gl.Draw(preview);
        Check(greenPixels() > 100, "Clearing a retexture did not restore the package texture");
        Check(Throws([&] { preview.UseRetexture(0); }), "A cleared retexture remained selectable");

        preview.LoadRetexture(0, png.u8string());
        const uint8_t edited[4] = {0, 255, 0, 255};
        WriteFile(png, BUPng::EncodeRGBA8(edited, 1, 1));
        Check(Throws([&] { preview.ApplyRetexture(session, 0); }) && !session.dirty,
            "A PNG edited after preview was applied without reloading");
        preview.LoadRetexture(0, png.u8string());
        gl.Draw(preview);
        Check(greenPixels() > 100, "Reloading an edited PNG did not refresh the preview");

        WriteFile(png, BUPng::EncodeRGBA8(magenta.data(), 1, 1));
        preview.LoadRetexture(0, png.u8string());
        preview.ApplyRetexture(session, 0);
        Check(session.dirty && session.selected == selected, "Applying a model texture changed selection or failed to mark dirty");
        const auto changed = RZTexture::Load(*session.package, *preview.PackageTexture(0)).DecodeLevel0();
        Check(changed != before && changed[0] > 240 && changed[1] < 10 && changed[2] > 240,
            "Applied texture did not retain the replacement pixels");
        preview.Update(session);
        gl.Draw(preview);
        Check(magentaPixels() > 100, "Package edit invalidated the model or reset the textured mode");
        preview.Clear();
        std::filesystem::remove_all(dir);
    }

    struct MultisampleSupport
    {
        inline static decltype(gl3::CheckFramebufferStatus) original = nullptr;
        inline static int limit = 0, rejected = 0;

        static GLenum CheckStatus(GLenum target)
        {
            GLint samples = 0;
            glGetIntegerv(0x80A9 /* GL_SAMPLES */, &samples);
            if (samples > limit)
            {
                ++rejected;
                return 0x8CDD; // GL_FRAMEBUFFER_UNSUPPORTED
            }
            return original(target);
        }

        explicit MultisampleSupport(int maximum)
        {
            original = gl3::CheckFramebufferStatus;
            limit = maximum;
            rejected = 0;
            gl3::CheckFramebufferStatus = CheckStatus;
        }
        ~MultisampleSupport() { gl3::CheckFramebufferStatus = original; }
    };

    void MultisampleFallbackTests(GLHarness& gl)
    {
        GLint maxSamples = 0;
        glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
        for (int limit : {2, 0})
        {
            const MultisampleSupport support(limit);
            auto session = TexturedFixture();
            ModelPreview preview;
            preview.Update(session);
            gl.Draw(preview);
            Check(maxSamples < 2 || maxSamples <= limit || MultisampleSupport::rejected > 0,
                "MSAA fallback was not exercised");
            const unsigned framebuffer = gl.PreviewFramebuffer();
            Check(framebuffer != 0 && gl.CountColor(framebuffer, true) > 100,
                "MSAA fallback did not render the textured model");
            if (!limit)
            {
                gl.bindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
                GLint type = 0;
                Proc<void (*)(unsigned, unsigned, unsigned, int*)>("glGetFramebufferAttachmentParameteriv")(
                    GL_READ_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, 0x8CD0 /* GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE */, &type);
                Check(type == GL_RENDERBUFFER, "Single-sample fallback has no depth buffer");
                gl.bindFramebuffer(GL_READ_FRAMEBUFFER, gl.callerFramebuffers[1]);
            }
            preview.Clear();
            Check(!gl.isFramebuffer(framebuffer), "Fallback framebuffer leaked");
            for (unsigned id = 1; id < 64; ++id)
                Check(!gl.isBuffer(id), "Fallback leaked a mesh or grid buffer");
        }
    }

    void AnimatedBufferTests(GLHarness& gl)
    {
        ModelRenderer renderer;
        renderer.drawNoTex = true;
        std::vector<RZGeometry::Mesh> meshes(1);
        meshes[0].positions = {-1, 0, 0, 1, 0, 0, 0, 2, 0};
        meshes[0].normals = {0, 0, 1, 0, 0, 1, 0, 0, 1};
        meshes[0].texcoords.resize(6);
        meshes[0].indices = {0, 1, 2};
        OrbitCamera camera;
        camera.yaw = 0;
        camera.pitch = 0;
        camera.target = BUVector3(0, 1, 0);
        camera.distance = 5;
        std::vector<ModelRenderer::MeshStyle> styles(1);
        styles[0].color[0] = 1;
        styles[0].color[1] = styles[0].color[2] = 0;
        renderer.Render(meshes, styles, camera, true, 128, 128);
        const auto redPixels = [&] {
            const GLStateGuard saved;
            std::array<uint8_t, 128 * 128 * 4> pixels{};
            glBindTexture(GL_TEXTURE_2D, renderer.FrameTexture());
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            size_t count = 0;
            for (size_t i = 0; i < pixels.size(); i += 4)
                if (pixels[i] > pixels[i + 1] + 40 && pixels[i] > pixels[i + 2] + 40) ++count;
            return count;
        };
        Check(redPixels() > 100, "Animated buffer fixture is not visible");
        std::set<unsigned> buffers;
        for (unsigned id = 1; id < 64; ++id) if (gl.isBuffer(id)) buffers.insert(id);
        for (size_t i = 0; i < meshes[0].positions.size(); i += 3) meshes[0].positions[i] += 50;
        renderer.UpdateVertices(meshes);
        renderer.Render(meshes, styles, camera, true, 128, 128);
        Check(redPixels() == 0, "Updated animation vertices were not uploaded");
        std::set<unsigned> after;
        for (unsigned id = 1; id < 64; ++id) if (gl.isBuffer(id)) after.insert(id);
        Check(after == buffers, "Animation reallocated GPU buffers");
        meshes[0].positions.push_back(0);
        meshes[0].normals.push_back(0);
        Check(Throws([&] { renderer.UpdateVertices(meshes); }), "Animation accepted a changed vertex layout");
        renderer.Release();
        for (unsigned id : buffers) Check(!gl.isBuffer(id), "Animation buffers leaked on shutdown");
    }

    int GLTests()
    {
        if (!glfwInit())
            return 77;
        int result = 0;
        {
            GLHarness gl;
            if (!gl.Ready())
                result = 77;
            else
            {
                SetCallerState(gl);
                UntexturedRenderTests(gl);
                TexturedRenderTests(gl);
                MultisampleFallbackTests(gl);
                AnimatedBufferTests(gl);
                std::puts("PASS: visible model, GL state isolation, retained buffers, cached frames, resize, textured "
                          "rendering, PNG preview/reset/apply, MSAA fallback and shutdown cleanup");
            }
        }
        glfwTerminate();
        return result;
    }
}

int main(int argc, char**)
{
    try
    {
        if (argc > 1)
            return GLTests();
        SelectionTests();
        MaterialTests();
        ReferenceTests();
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::fprintf(stderr, "%s\n", ex.what());
        return 1;
    }
}
