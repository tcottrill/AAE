// -----------------------------------------------------------------------------
// vk_texture_loader.cpp - see vk_texture_loader.h for design notes.
// ASCII-only comments.
// -----------------------------------------------------------------------------
#include "vk_texture_loader.h"
#include "sys_log.h"
#include "aae_fileio.h"      // load_zip_file / get_last_zip_file_size / file_exists
#include "aae_mame_driver.h" // struct artworks, art_loaded[], Machine, ART_TEX
#include "config.h"          // config.exartpath + artwork/overlay/bezel flags
#include "menu.h"            // g_*Available menu flags
#include "path_helper.h"     // getpathM

// Header-only include; STB_IMAGE_IMPLEMENTATION is compiled exactly once,
// in system/graphics/sys_texture.cpp - do NOT define it again here.
#include "stb_image.h"

#include <cstdlib>        // free()
#include <filesystem>
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

// -----------------------------------------------------------------------------
// Per-game legacy artwork (see header). Mirrors texture_handler.cpp's
// make_single_bitmap / load_artwork for the Vulkan chain.
// -----------------------------------------------------------------------------
namespace
{
    VkTexture s_artTex[8]{};
    bool      s_artHave[8]{};

    // GAME_TEX slot 0: the textured-shot sprite (shot.png / cineshot.png).
    // Plan 9 - the one GAME_TEX entry the VK chain consumes (ShotDrawVK);
    // other GAME_TEX/FUN_TEX entries still feed GL-only draw paths.
    VkTexture s_shotTex{};
    bool      s_shotHave = false;

    // Path-by-path mirror of make_single_bitmap (texture_handler.cpp):
    //   1a. ZIP archive in config.exartpath
    //   1b. unzipped <exartpath>\<gamename>\<filename>
    //   2a. ZIP archive in the default "artwork" folder (next to the exe)
    //   2b. unzipped artwork\<gamename>\<filename>
    bool VkArt_LoadSingle(VkContext& ctx, VkTexture& out,
                          const char* filename, const char* archname,
                          bool flipY = false, bool mips = true)
    {
        auto join_path = [](const std::string& root, const char* child) -> std::string {
            if (root.empty() || !child || !child[0])
                return std::string();
            std::string p = root;
            if (p.back() != '\\' && p.back() != '/')
                p.push_back('\\');
            p.append(child);
            return p;
        };

        // Artwork defaults: no stbi flip (see header) and full mip chains (GL
        // loads art with filter=1 trilinear mips; the letterbox minifies the
        // same way here). The raster scanline overlay overrides both.
        const bool kFlipY = flipY;
        const bool kMips  = mips;

        std::string archivePath;

        if (config.exartpath && config.exartpath[0] != '\0')
        {
            archivePath = join_path(config.exartpath, archname);
            if (!archivePath.empty() && file_exists(archivePath.c_str()))
            {
                LOG_INFO("VkArt: loading '%s' from external archive: %s", filename, archivePath.c_str());
                if (VkTex_Load(ctx, filename, archivePath.c_str(), kFlipY, kMips, out))
                    return true;
                LOG_ERROR("VkArt: external archive found but load failed for '%s'", filename);
            }

            if (Machine && Machine->gamedrv && Machine->gamedrv->name)
            {
                std::string folderBase = join_path(config.exartpath, Machine->gamedrv->name);
                std::filesystem::path fullFilePath = std::filesystem::path(folderBase) / filename;
                if (std::filesystem::exists(fullFilePath))
                {
                    LOG_INFO("VkArt: loading '%s' from external folder: %s",
                             filename, fullFilePath.string().c_str());
                    std::string fullPathStr = fullFilePath.string();
                    if (VkTex_Load(ctx, fullPathStr.c_str(), nullptr, kFlipY, kMips, out))
                        return true;
                    LOG_ERROR("VkArt: external folder file found but load failed for '%s'", filename);
                }
            }
        }

        archivePath = getpathM("artwork", archname);
        if (VkTex_Load(ctx, filename, archivePath.c_str(), kFlipY, kMips, out))
            return true;

        if (Machine && Machine->gamedrv && Machine->gamedrv->name)
        {
            std::string folderPath = getpathM("artwork", Machine->gamedrv->name);
            std::filesystem::path fullFilePath = std::filesystem::path(folderPath) / filename;
            if (std::filesystem::exists(fullFilePath))
            {
                std::string fullPathStr = fullFilePath.string();
                if (VkTex_Load(ctx, fullPathStr.c_str(), nullptr, kFlipY, kMips, out))
                    return true;
            }
        }

        LOG_ERROR("VkArt: failed to load '%s' from all paths (external and default)", filename);
        return false;
    }
}

bool VkArt_LoadFromArchive(VkContext& ctx, const char* filename, const char* archname,
                           bool flipY, bool generateMips, VkTexture& outTex)
{
    return VkArt_LoadSingle(ctx, outTex, filename, archname, flipY, generateMips);
}

void VkArt_FreeAll(VkContext& ctx)
{
    for (int i = 0; i < 8; ++i)
    {
        if (s_artHave[i])
        {
            VK_DestroyTexture(ctx, s_artTex[i]);
            s_artTex[i] = VkTexture{};
            s_artHave[i] = false;
        }
    }
    if (s_shotHave)
    {
        VK_DestroyTexture(ctx, s_shotTex);
        s_shotTex = VkTexture{};
        s_shotHave = false;
    }
}

VkTexture* VkArt_GetShotTex(void)
{
    return s_shotHave ? &s_shotTex : nullptr;
}

VkTexture* VkArt_Get(int slot)
{
    if (slot < 0 || slot >= 8 || !s_artHave[slot])
        return nullptr;
    return &s_artTex[slot];
}

void VkArt_LoadForGame(VkContext& ctx, const struct artworks* p)
{
    VkArt_FreeAll(ctx);

    // One command buffer and ONE fence wait for the whole set, instead of a
    // blocking GPU round-trip per image. This loop is the bulk of the launch
    // stall on artwork-heavy games (the Atari vector bezels especially), and it
    // runs on the main thread with nothing repainting behind it. The free above
    // stays outside the batch: it destroys the PREVIOUS game's textures, which
    // is a device-wait-idle, and must complete before the batch starts
    // recording.
    const bool batched = VK_BeginUploadBatch(ctx);

    if (p)
    {
        for (int i = 0; p[i].filename != NULL; i++)
        {
            // GAME_TEX slot 0 is the textured-shot sprite - the VK shot pass
            // (ShotDrawVK, Plan 9) needs it. All other FUN_TEX/GAME_TEX
            // entries feed GL-only draw paths (fun screens etc.) and are
            // skipped; the VK compositor otherwise uses ART_TEX only.
            if (p[i].type == GAME_TEX && p[i].target == 0)
            {
                if (VkArt_LoadSingle(ctx, s_shotTex, p[i].filename, p[i].zipfile))
                    s_shotHave = true;
                else
                    LOG_INFO("VkArt: could not load shot texture '%s'.", p[i].filename);
                continue;
            }
            if (p[i].type != ART_TEX)
                continue;

            const int t = p[i].target;
            if (t < 0 || t >= 8)
            {
                LOG_ERROR("VkArt: bad art target %d for '%s'", t, p[i].filename);
                continue;
            }

            if (VkArt_LoadSingle(ctx, s_artTex[t], p[i].filename, p[i].zipfile))
            {
                s_artHave[t] = true;
                if (t < 6)
                    art_loaded[t] = 1;
            }
            else
            {
                LOG_INFO("VkArt: could not load '%s' (target=%d).", p[i].filename, t);
            }
        }
    }

    // Submit and wait once. Must happen BEFORE the availability bookkeeping
    // below: a failed batch means none of these textures are usable, and the
    // flags have to reflect that.
    if (batched && !VK_EndUploadBatch(ctx))
    {
        LOG_ERROR("VkArt: artwork upload batch failed; dropping this game's artwork");
        VkArt_FreeAll(ctx);
        for (int i = 0; i < 6; ++i)
            art_loaded[i] = 0;
    }

    // --- Same post-load bookkeeping as GL load_artwork: disable config flags
    // for layers that failed so the renderer/menu never reference them. ---
    if (!art_loaded[0] && config.artwork)
    {
        LOG_INFO("VkArt: backdrop not available - disabling config.artwork.");
        config.artwork = 0;
    }
    if (!art_loaded[1] && config.overlay)
    {
        LOG_INFO("VkArt: overlay not available - disabling config.overlay.");
        config.overlay = 0;
    }
    if (!art_loaded[3] && config.bezel)
    {
        LOG_INFO("VkArt: bezel not available - disabling config.bezel and config.artcrop.");
        config.bezel = 0;
        config.artcrop = 0;
    }

    g_artworkAvailable = art_loaded[0];
    g_overlayAvailable = art_loaded[1];
    g_bezelAvailable   = art_loaded[3];
    g_artcropAvailable = art_loaded[3];

    LOG_INFO("VkArt: availability flags: artwork=%d overlay=%d bezel=%d artcrop=%d",
             g_artworkAvailable, g_overlayAvailable, g_bezelAvailable, g_artcropAvailable);
}
