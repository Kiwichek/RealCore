#pragma once
#include <sokol_gfx.h>
#include <cstddef>
#include <string>

class Texture {
public:
    Texture() = default;
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    Texture(Texture&& other) noexcept;
    Texture& operator=(Texture&& other) noexcept;
    ~Texture();

    bool loadFromFile(const std::string& path);
    bool loadFromMemory(const void* data, size_t size);
    bool loadFromRGBA8(const void* pixels, int w, int h);
    void destroy();

    sg_image image() const { return m_image; }
    int width() const { return m_width; }
    int height() const { return m_height; }

private:
    bool loadFromPixels(const void* pixels, int w, int h, int channels);

    sg_image m_image = {};
    int m_width = 0;
    int m_height = 0;
};
