// -----------------------------------------------------------------------------
// vk_texture_loader.cpp - see vk_texture_loader.h for design notes.
// ASCII-only comments.
// -----------------------------------------------------------------------------
#include "vk_texture_loader.h"
#include "sys_log.h"
#include "aae_fileio.h"   // load_zip_file / get_last_zip_file_size

// Header-only include; STB_IMAGE_IMPLEMENTATION is compiled exactly once,
// in system/graphics/sys_texture.cpp - do NOT define it again here.
#include "stb_image.h"

#include <cstdlib>        // free()
#include <string>
#include <unordered_map>

bool VkTex_Load(VkContext& ctx, const char* filename, const char* archname,
                 bool flipY, bool generateMips, VkTexture& outTex)
{
    if (!filename)
        return false;

    int width = 0, height = 0, comp = 0;
    unsigned char* pixels = nullptr;
    unsigned char* raw = nullptr;

    stbi_set_flip_vertically_on_load(flipY ? 1 : 0);

    if (!archname)
    {
        pixels = stbi_load(filename, &width, &height, &comp, 4);
    }
    else
    {
        raw = load_zip_file(archname, filename);
        if (raw)
        {
            const size_t size = get_last_zip_file_size();
            pixels = stbi_load_from_memory(raw, (int)size, &width, &height, &comp, 4);
        }
    }

    if (raw)
        free(raw);

    if (!pixels)
    {
        LOG_ERROR("VkTex_Load: could not load %s", filename);
        return false;
    }

    const bool ok = VK_CreateTextureRGBA8_UNORM_FromPixels(ctx, pixels,
        (uint32_t)width, (uint32_t)height, outTex, generateMips, /*nearestFilter=*/false);

    stbi_image_free(pixels);

    if (!ok)
        LOG_ERROR("VkTex_Load: GPU upload failed for %s", filename);

    return ok;
}

namespace
{
    std::unordered_map<std::string, VkTexture> s_cache;

    VkTexture s_solidWhite{};
    bool      s_solidWhiteInit = false;
    bool      s_solidWhiteFailed = false;
}

VkTexture* VkTex_LoadCached(VkContext& ctx, const char* filename, const char* archname,
                             bool flipY, bool generateMips)
{
    if (!filename)
        return nullptr;

    std::string key = archname ? (std::string(archname) + "|" + filename) : std::string(filename);

    auto it = s_cache.find(key);
    if (it != s_cache.end())
        return &it->second;

    VkTexture tex{};
    if (!VkTex_Load(ctx, filename, archname, flipY, generateMips, tex))
        return nullptr;

    auto res = s_cache.emplace(std::move(key), tex);
    return &res.first->second;
}

VkTexture* VkTex_GetSolidWhite(VkContext& ctx)
{
    if (s_solidWhiteInit)
        return &s_solidWhite;
    if (s_solidWhiteFailed)
        return nullptr;

    const uint8_t whitePixel[4] = { 255, 255, 255, 255 };
    if (!VK_CreateTextureRGBA8_UNORM_FromPixels(ctx, whitePixel, 1, 1, s_solidWhite,
            /*generateMips=*/false, /*nearestFilter=*/true))
    {
        s_solidWhiteFailed = true;
        LOG_ERROR("VkTex_GetSolidWhite: GPU upload failed");
        return nullptr;
    }

    s_solidWhiteInit = true;
    return &s_solidWhite;
}

void VkTex_ShutdownCache(VkContext& ctx)
{
    for (auto& kv : s_cache)
        VK_DestroyTexture(ctx, kv.second);
    s_cache.clear();

    if (s_solidWhiteInit)
    {
        VK_DestroyTexture(ctx, s_solidWhite);
        s_solidWhiteInit = false;
    }
    s_solidWhiteFailed = false;
}
