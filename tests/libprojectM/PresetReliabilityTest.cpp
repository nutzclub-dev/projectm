#include <gtest/gtest.h>

#include "ProjectM.hpp"
#include "Renderer/TextureManager.hpp"
#include "MilkdropPreset/MilkdropPreset.hpp"
#include "MilkdropPreset/MilkdropPresetExceptions.hpp"
#include <sstream>
#include <chrono>

class PresetReliabilityTest : public ::testing::Test {
protected:
    void SetUp() override {
    }
};

TEST_F(PresetReliabilityTest, TextureManagerMissingTextureFallback) {
    std::vector<std::string> searchPaths = {"/nonexistent_path_12345"};
    libprojectM::Renderer::TextureManager texManager(searchPaths);

    // Request non-existent texture
    auto desc = texManager.GetTexture("missing_texture.jpg");
    EXPECT_NE(desc.Texture(), nullptr); // Should return placeholder texture, not null

    // Repeat request to verify cached negative lookup (performance & no repeated I/O)
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 1000; ++i) {
        auto descRepeat = texManager.GetTexture("missing_texture.jpg");
        EXPECT_NE(descRepeat.Texture(), nullptr);
    }
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now() - start
    ).count();

    // 1000 negative lookups should take under 100ms with caching
    EXPECT_LT(elapsed, 100);
}

TEST_F(PresetReliabilityTest, MalformedPresetParsingHandling) {
    std::string malformedData = "[preset00]\n"
                                "fRating=5.0\n"
                                "per_frame_1=invalid_syntax_error %%%%%%%%\n";
    std::stringstream ss(malformedData);

    libprojectM::MilkdropPreset::MilkdropPreset preset(ss);
    // Preset parsing shouldn't crash; invalid syntax will be detected during initialization or evaluation
    EXPECT_NO_THROW(preset.Filename());
}
