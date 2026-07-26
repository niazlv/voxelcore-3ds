// Port-side model/skeleton asset loader (AssetsLoader itself is GL-bound):
// loads pack skeletons + their bone models, base drop/block models,
// generates per-item drop models and loads loose model textures.
#include <string>
#include <unordered_set>
#include <vector>

#include "assets/Assets.hpp"
#include "coders/imageio.hpp"
#include "coders/obj.hpp"
#include "coders/vcm.hpp"
#include "coders/vec3.hpp"
#include "content/Content.hpp"
#include "debug/Logger.hpp"
#include "engine/Engine.hpp"
#include "engine/EnginePaths.hpp"
#include "graphics/commons/Model.hpp"
#include "graphics/core/Texture.hpp"
#include "graphics/render/ModelsGenerator.hpp"
#include "io/io.hpp"
#include "objects/rigging.hpp"
#include "voxels/Block.hpp"
#include "items/ItemDef.hpp"

static debug::Logger logger("models-3ds");

static std::unordered_set<std::string> textureNames;

static void collect_textures(const model::Model& model) {
    for (const auto& mesh : model.meshes) {
        const auto& tex = mesh.texture;
        if (tex.empty() || tex[0] == '$' ||
            tex.find(':') != std::string::npos) {
            continue;
        }
        textureNames.insert(tex);
    }
}

static bool load_model(
    Assets& assets, const ResPaths& paths, const std::string& name
) {
    if (assets.get<model::Model>(name)) {
        return true;
    }
    std::string file = "models/" + name;
    auto path = paths.find(file + ".vec3");
    if (io::exists(path)) {
        auto bytes = io::read_bytes_buffer(path);
        auto modelFile = vec3::load(path.string(), bytes);
        for (auto& [modelName, entry] : modelFile.models) {
            std::string fullName = name;
            if (name != modelName) {
                fullName += "." + modelName;
            }
            collect_textures(entry.model);
            assets.store(
                std::make_unique<model::Model>(entry.model), fullName);
        }
        return true;
    }
    path = paths.find(file + ".obj");
    if (io::exists(path)) {
        try {
            auto model = obj::parse(path.string(), io::read_string(path));
            collect_textures(*model);
            assets.store(std::move(model), name);
            return true;
        } catch (const std::exception& err) {
            logger.error() << file << ": " << err.what();
            return false;
        }
    }
    for (const char* ext : {".vcm", ".xml"}) {
        path = paths.find(file + ext);
        if (!io::exists(path)) {
            continue;
        }
        try {
            auto vcmModel = vcm::parse(
                path.string(), io::read_string(path),
                path.extension() == ".xml");
            if (vcmModel.parts.empty()) {
                return false;
            }
            if (vcmModel.parts.size() == 1) {
                auto model = std::make_unique<model::Model>(
                    std::move(vcmModel.squash()));
                collect_textures(*model);
                assets.store(std::move(model), name);
                return true;
            }
            for (auto& [partName, model] : vcmModel.parts) {
                collect_textures(model);
                assets.store(
                    std::make_unique<model::Model>(std::move(model)),
                    name + "." + partName);
            }
            if (vcmModel.skeleton) {
                for (auto& bone : vcmModel.skeleton->getBones()) {
                    bone->setModel(name + "." + bone->model.name);
                }
                assets.store<rigging::SkeletonConfig>(
                    std::make_unique<rigging::SkeletonConfig>(
                        std::move(*vcmModel.skeleton)),
                    name);
            }
            return true;
        } catch (const std::exception& err) {
            logger.error() << file << ": " << err.what();
            return false;
        }
    }
    logger.warning() << "model not found: " << name;
    return false;
}

// textures referenced by loaded models: 'atlas:region' resolves through
// atlases at draw time; bare names are loose PNG files
static void load_model_textures(Assets& assets, const ResPaths& paths) {
    for (const auto& name : textureNames) {
        if (assets.get<Texture>(name)) {
            continue;
        }
        auto path = paths.find("textures/" + name + ".png");
        if (!io::exists(path)) {
            logger.warning() << "model texture not found: " << name;
            continue;
        }
        try {
            auto image = imageio::read(path);
            if (image) {
                assets.store<Texture>(Texture::from(image.get()), name);
            }
        } catch (const std::exception& err) {
            logger.error() << name << ": " << err.what();
        }
    }
}

bool vc3ds_load_entity_assets(
    Engine& engine, Content& content, Assets& assets
) {
    const ResPaths& paths = engine.getResPaths();

    // base models used by the drop-model generator
    load_model(assets, paths, "block");
    load_model(assets, paths, "drop-item");

    // custom block models referenced by name (ContentGfxCache requires
    // them when building its region/model cache)
    for (const auto& [bname, bdef] : content.blocks.getDefs()) {
        const auto& name = bdef->defaults.model.name;
        if (bdef->defaults.model.type == BlockModelType::CUSTOM &&
            !name.empty()) {
            std::string plain = name;
            size_t pos = plain.find(':');
            if (pos != std::string::npos) {
                plain = plain.substr(pos + 1);
            }
            if (!load_model(assets, paths, plain) && plain != name) {
                load_model(assets, paths, name);
            }
            // the cache looks the model up under the def's name
            if (auto* m = assets.get<model::Model>(plain)) {
                if (plain != name) {
                    assets.store(
                        std::make_unique<model::Model>(*m), name);
                }
            }
        }
    }

    // pack skeletons + their bone models
    for (const auto& entry : content.getPacks()) {
        const auto& packid = entry.first;
        io::path dir(packid + ":skeletons");
        if (!io::is_directory(dir)) {
            continue;
        }
        for (const auto& file : io::directory_iterator(dir)) {
            if (file.extension() != ".json") {
                continue;
            }
            std::string name = packid + ":" + file.stem();
            try {
                auto skeleton = rigging::SkeletonConfig::parse(
                    io::read_string(file), file.string(), name);
                for (auto& bone : skeleton->getBones()) {
                    std::string model = bone->model.name;
                    size_t pos = model.rfind('.');
                    if (pos != std::string::npos) {
                        model = model.substr(0, pos);
                    }
                    if (!model.empty()) {
                        load_model(assets, paths, model);
                    }
                }
                assets.store(std::move(skeleton), name);
            } catch (const std::exception& err) {
                logger.error() << name << ": " << err.what();
            }
        }
    }

    // generated block/item drop models ('<item>.model'); per-item guard:
    // a single bad pack definition must not kill the port
    for (auto& [bname, bdef] : content.blocks.getDefs()) {
        try {
            ModelsGenerator::prepareModel(assets, *bdef, bdef->defaults, 0);
            if (bdef->variants) {
                auto& variants = bdef->variants->variants;
                for (size_t i = 1; i < variants.size(); i++) {
                    ModelsGenerator::prepareModel(
                        assets, *bdef, variants[i], i);
                }
            }
        } catch (const std::exception& err) {
            logger.error() << "block model " << bname << ": " << err.what();
        }
    }
    for (auto& [iname, idef] : content.items.getDefs()) {
        try {
            assets.store(
                std::make_unique<model::Model>(
                    ModelsGenerator::generate(*idef, content, assets)),
                iname + ".model");
        } catch (const std::exception& err) {
            logger.error() << "item model " << iname << ": " << err.what();
            if (auto* fallback = assets.get<model::Model>("drop-item")) {
                assets.store(
                    std::make_unique<model::Model>(*fallback),
                    iname + ".model");
            }
        }
    }

    load_model_textures(assets, paths);
    logger.info() << "entity assets loaded";
    return true;
}
