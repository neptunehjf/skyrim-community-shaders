#pragma once

#include "Buffer.h"
#include "Feature.h"
#include "State.h"

struct ExtendedMaterials : Feature
{
	static ExtendedMaterials* GetSingleton()
	{
		static ExtendedMaterials singleton;
		return &singleton;
	}

	virtual inline std::string GetName() { return "Complex Parallax Materials"; }
	virtual inline std::string GetShortName() { return "ComplexParallaxMaterials"; }
	inline std::string_view GetShaderDefineName() override { return "COMPLEX_PARALLAX_MATERIALS"; }

	bool HasShaderDefine(RE::BSShader::Type shaderType) override;

	struct Settings
	{
		uint32_t EnableComplexMaterial = 1;

		uint32_t EnableParallax = 1;
		uint32_t EnableTerrain = 0;
		uint32_t EnableHighQuality = 0;

		uint32_t MaxDistance = 4096;
		float CRPMRange = 0.5f;
		float BlendRange = 0.25f;
		float Height = 0.1f;

		uint32_t EnableShadows = 1;
		uint32_t ShadowsStartFade = 512;
		uint32_t ShadowsEndFade = 1024;
	};

	struct PerPass
	{
		Settings settings;
	};

	Settings settings;

	std::unique_ptr<Buffer> perPass = nullptr;

	ID3D11SamplerState* terrainSampler = nullptr;

	virtual void SetupResources();
	virtual inline void Reset() {}

	virtual void DataLoaded() override;

	virtual void DrawSettings();

	void ModifyLighting(const RE::BSShader* shader, const uint32_t descriptor);
	virtual void Draw(const RE::BSShader* shader, const uint32_t descriptor);

	virtual void Load(json& o_json);
	virtual void Save(json& o_json);

	virtual void RestoreDefaultSettings();

	bool SupportsVR() override { return true; };
};
