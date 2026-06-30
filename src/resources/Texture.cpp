#include <resources/Texture.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

Texture::Texture(Texture&& other) noexcept
    : m_image(other.m_image)
    , m_width(other.m_width)
    , m_height(other.m_height)
{
    other.m_image = {};
    other.m_width = 0;
    other.m_height = 0;
}

Texture& Texture::operator=(Texture&& other) noexcept {
    if (this != &other) {
        destroy();
        m_image = other.m_image;
        m_width = other.m_width;
        m_height = other.m_height;
        other.m_image = {};
        other.m_width = 0;
        other.m_height = 0;
    }
    return *this;
}

Texture::~Texture() {
    destroy();
}

bool Texture::loadFromFile(const std::string& path) {
    int w, h, channels;
    unsigned char* pixels = stbi_load(path.c_str(), &w, &h, &channels, 4);
    if (!pixels) return false;

    bool ok = loadFromPixels(pixels, w, h, 4);
    stbi_image_free(pixels);
    return ok;
}

bool Texture::loadFromMemory(const void* data, size_t size) {
    int w, h, channels;
    unsigned char* pixels = stbi_load_from_memory(
        (const unsigned char*)data, (int)size, &w, &h, &channels, 4);
    if (!pixels) return false;

    bool ok = loadFromPixels(pixels, w, h, 4);
    stbi_image_free(pixels);
    return ok;
}

bool Texture::loadFromRGBA8(const void* pixels, int w, int h) {
    return loadFromPixels(pixels, w, h, 4);
}

bool Texture::loadFromPixels(const void* pixels, int w, int h, int channels) {
    destroy();

    m_width = w;
    m_height = h;

    sg_image_desc desc = {};
    desc.type = SG_IMAGETYPE_2D;
    desc.width = w;
    desc.height = h;
    desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    desc.data.mip_levels[0].ptr = pixels;
    desc.data.mip_levels[0].size = (size_t)(w * h * channels);
    desc.label = "texture";
    m_image = sg_make_image(&desc);

    return m_image.id != SG_INVALID_ID;
}

void Texture::destroy() {
    if (m_image.id != SG_INVALID_ID) {
        sg_destroy_image(m_image);
        m_image = {};
    }
    m_width = 0;
    m_height = 0;
}
