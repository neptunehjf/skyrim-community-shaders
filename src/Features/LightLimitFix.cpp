#include "LightLimitFix.h"

#include <PerlinNoise.hpp>

#include "State.h"
#include "Util.h"

static constexpr uint CLUSTER_SIZE_X = 16;
static constexpr uint CLUSTER_SIZE_Y = 16;
static constexpr uint CLUSTER_SIZE_Z = 16;
constexpr uint CLUSTER_MAX_LIGHTS = 128;

constexpr std::uint32_t CLUSTER_COUNT = CLUSTER_SIZE_X * CLUSTER_SIZE_Y * CLUSTER_SIZE_Z;

static constexpr uint MAX_LIGHTS = 2048;

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	LightLimitFix::Settings,
	EnableContactShadows,
	EnableFirstPersonShadows,
	EnableParticleLights,
	EnableParticleLightsCulling,
	EnableParticleLightsDetection,
	ParticleLightsSaturation,
	EnableParticleLightsOptimization,
	ParticleLightsOptimisationClusterRadius,
	ParticleBrightness,
	ParticleRadius,
	BillboardBrightness,
	BillboardRadius)

void LightLimitFix::DrawSettings()
{
	if (ImGui::TreeNodeEx("Particle Lights", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Checkbox("Enable Particle Lights", &settings.EnableParticleLights);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Enables Particle Lights.");
		}

		ImGui::Checkbox("Enable Culling", &settings.EnableParticleLightsCulling);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Significantly improves performance by not rendering empty textures. Only disable if you are encountering issues.");
		}

		ImGui::Checkbox("Enable Detection", &settings.EnableParticleLightsDetection);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Adds particle lights to the player light level, so that NPCs can detect them for stealth and gameplay.");
		}

		ImGui::Checkbox("Enable Optimization", &settings.EnableParticleLightsOptimization);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Merges vertices which are close enough to each other to significantly improve performance.");
		}
		ImGui::SliderInt("Optimisation Cluster Radius", (int*)&settings.ParticleLightsOptimisationClusterRadius, 1, 64);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Radius to use for clustering lights.");
		}
		ImGui::Spacing();
		ImGui::Spacing();

		ImGui::TextWrapped("Particle Lights Customisation");
		ImGui::SliderFloat("Saturation", &settings.ParticleLightsSaturation, 1.0, 2.0, "%.2f");
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Particle light saturation.");
		}
		ImGui::SliderFloat("Particle Brightness", &settings.ParticleBrightness, 0.0, 10.0, "%.2f");
		ImGui::SliderFloat("Particle Radius", &settings.ParticleRadius, 0.0, 10.0, "%.2f");
		ImGui::SliderFloat("Billboard Brightness", &settings.BillboardBrightness, 0.0, 10.0, "%.2f");
		ImGui::SliderFloat("Billboard Radius", &settings.BillboardRadius, 0.0, 10.0, "%.2f");

		ImGui::Spacing();
		ImGui::Spacing();
		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Shadows", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Checkbox("Enable Contact Shadows", &settings.EnableContactShadows);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("All lights cast small shadows. Performance impact.");
		}

		ImGui::Checkbox("Enable First-Person Shadows", &settings.EnableFirstPersonShadows);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Torches and light spells will cast shadows in first-person. Performance impact.");
		}

		ImGui::Spacing();
		ImGui::Spacing();
		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Light Limit Visualization", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Checkbox("Enable Lights Visualisation", &settings.EnableLightsVisualisation);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Enables visualization of the light limit\n");
		}

		{
			static const char* comboOptions[] = { "Light Limit", "Strict Lights Count", "Clustered Lights Count" };
			ImGui::Combo("Lights Visualisation Mode", (int*)&settings.LightsVisualisationMode, comboOptions, 3);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text(
					" - Visualise the light limit. Red when the \"strict\" light limit is reached (portal-strict lights). "
					" - Visualise the number of strict lights. "
					" - Visualise the number of clustered lights. ");
			}
		}

		ImGui::Spacing();
		ImGui::Spacing();
		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Statistics", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Text(std::format("Clustered Light Count : {}", lightCount).c_str());
		ImGui::Text(std::format("Particle Lights Detection Count : {}", particleLightsDetectionHits).c_str());

		ImGui::TreePop();
	}
}

void LightLimitFix::SetupResources()
{
	{
		D3D11_BUFFER_DESC sbDesc{};
		sbDesc.Usage = D3D11_USAGE_DYNAMIC;
		sbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		sbDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		sbDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		sbDesc.StructureByteStride = sizeof(PerPass);
		sbDesc.ByteWidth = sizeof(PerPass);
		perPass = std::make_unique<Buffer>(sbDesc);

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.FirstElement = 0;
		srvDesc.Buffer.NumElements = 1;
		perPass->CreateSRV(srvDesc);
	}

	{
		D3D11_BUFFER_DESC sbDesc{};
		sbDesc.Usage = D3D11_USAGE_DYNAMIC;
		sbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		sbDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		sbDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		sbDesc.StructureByteStride = sizeof(StrictLightData);
		sbDesc.ByteWidth = sizeof(StrictLightData);
		strictLightData = std::make_unique<Buffer>(sbDesc);

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.FirstElement = 0;
		srvDesc.Buffer.NumElements = 1;
		strictLightData->CreateSRV(srvDesc);
	}

	{
		clusterBuildingCS = (ID3D11ComputeShader*)Util::CompileShader(L"Data\\Shaders\\LightLimitFix\\ClusterBuildingCS.hlsl", {}, "cs_5_0");
		clusterCullingCS = (ID3D11ComputeShader*)Util::CompileShader(L"Data\\Shaders\\LightLimitFix\\ClusterCullingCS.hlsl", {}, "cs_5_0");

		lightBuildingCB = new ConstantBuffer(ConstantBufferDesc<LightBuildingCB>());
		lightCullingCB = new ConstantBuffer(ConstantBufferDesc<LightCullingCB>());
	}

	{
		D3D11_BUFFER_DESC sbDesc{};
		sbDesc.Usage = D3D11_USAGE_DEFAULT;
		sbDesc.CPUAccessFlags = 0;
		sbDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		sbDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.FirstElement = 0;

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
		uavDesc.Format = DXGI_FORMAT_UNKNOWN;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		uavDesc.Buffer.FirstElement = 0;
		uavDesc.Buffer.Flags = 0;

		std::uint32_t numElements = CLUSTER_COUNT;

		sbDesc.StructureByteStride = sizeof(ClusterAABB);
		sbDesc.ByteWidth = sizeof(ClusterAABB) * numElements;
		clusters = eastl::make_unique<Buffer>(sbDesc);
		srvDesc.Buffer.NumElements = numElements;
		clusters->CreateSRV(srvDesc);
		uavDesc.Buffer.NumElements = numElements;
		clusters->CreateUAV(uavDesc);

		numElements = 1;
		sbDesc.StructureByteStride = sizeof(uint32_t);
		sbDesc.ByteWidth = sizeof(uint32_t) * numElements;
		lightCounter = eastl::make_unique<Buffer>(sbDesc);
		srvDesc.Buffer.NumElements = numElements;
		lightCounter->CreateSRV(srvDesc);
		uavDesc.Buffer.NumElements = numElements;
		lightCounter->CreateUAV(uavDesc);

		numElements = CLUSTER_COUNT * CLUSTER_MAX_LIGHTS;
		sbDesc.StructureByteStride = sizeof(uint32_t);
		sbDesc.ByteWidth = sizeof(uint32_t) * numElements;
		lightList = eastl::make_unique<Buffer>(sbDesc);
		srvDesc.Buffer.NumElements = numElements;
		lightList->CreateSRV(srvDesc);
		uavDesc.Buffer.NumElements = numElements;
		lightList->CreateUAV(uavDesc);

		numElements = CLUSTER_COUNT;
		sbDesc.StructureByteStride = sizeof(LightGrid);
		sbDesc.ByteWidth = sizeof(LightGrid) * numElements;
		lightGrid = eastl::make_unique<Buffer>(sbDesc);
		srvDesc.Buffer.NumElements = numElements;
		lightGrid->CreateSRV(srvDesc);
		uavDesc.Buffer.NumElements = numElements;
		lightGrid->CreateUAV(uavDesc);
	}

	{
		D3D11_BUFFER_DESC sbDesc{};
		sbDesc.Usage = D3D11_USAGE_DYNAMIC;
		sbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		sbDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		sbDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		sbDesc.StructureByteStride = sizeof(LightData);
		sbDesc.ByteWidth = sizeof(LightData) * MAX_LIGHTS;
		lights = eastl::make_unique<Buffer>(sbDesc);

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.FirstElement = 0;
		srvDesc.Buffer.NumElements = MAX_LIGHTS;
		lights->CreateSRV(srvDesc);
	}
}

void LightLimitFix::Reset()
{
	rendered = false;
	for (auto& particleLight : particleLights) {
		if (const auto particleSystem = netimmerse_cast<RE::NiParticleSystem*>(particleLight.first)) {
			if (auto particleData = particleSystem->GetParticleRuntimeData().particleData.get()) {
				particleData->DecRefCount();
			}
		}
		particleLight.first->DecRefCount();
	}
	particleLights.clear();
	std::swap(particleLights, queuedParticleLights);
	boundViews = false;
}

void LightLimitFix::Load(json& o_json)
{
	if (o_json[GetName()].is_object())
		settings = o_json[GetName()];
	Feature::Load(o_json);
}

void LightLimitFix::Save(json& o_json)
{
	o_json[GetName()] = settings;
}

void LightLimitFix::RestoreDefaultSettings()
{
	settings = {};
}

void LightLimitFix::BSLightingShader_SetupGeometry_Before(RE::BSRenderPass*)
{
	strictLightDataTemp.NumLights = 0;
}

void LightLimitFix::BSLightingShader_SetupGeometry_GeometrySetupConstantPointLights(RE::BSRenderPass* a_pass, DirectX::XMMATRIX&, uint32_t, uint32_t, float, Space)
{
	auto accumulator = RE::BSGraphics::BSShaderAccumulator::GetCurrentAccumulator();
	bool inWorld = accumulator->GetRuntimeData().activeShadowSceneNode == RE::BSShaderManager::State::GetSingleton().shadowSceneNode[0];

	strictLightDataTemp.NumLights = a_pass->numLights - 1;
	for (uint32_t i = 0; i < strictLightDataTemp.NumLights; i++) {
		auto bsLight = a_pass->sceneLights[i + 1];
		auto niLight = bsLight->light.get();

		auto& runtimeData = niLight->GetLightRuntimeData();

		LightData light{};
		light.color = { runtimeData.diffuse.red, runtimeData.diffuse.green, runtimeData.diffuse.blue };
		light.color *= runtimeData.fade;
		light.color *= bsLight->lodDimmer;

		light.radius = runtimeData.radius.x;

		light.firstPersonShadow = false;

		SetLightPosition(light, niLight->world.translate, inWorld);

		strictLightDataTemp.StrictLights[i] = light;
	}
}

void LightLimitFix::BSLightingShader_SetupGeometry_After(RE::BSRenderPass*)
{
	static bool wasEmpty = false;
	bool isEmpty = strictLightDataTemp.NumLights == 0;
	if (!isEmpty || (isEmpty && !wasEmpty)) {
		auto& context = State::GetSingleton()->context;
		D3D11_MAPPED_SUBRESOURCE mapped;
		DX::ThrowIfFailed(context->Map(strictLightData->resource.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
		size_t bytes = sizeof(StrictLightData);
		memcpy_s(mapped.pData, bytes, &strictLightDataTemp, bytes);
		context->Unmap(strictLightData->resource.get(), 0);
		wasEmpty = isEmpty;
	}
}

void LightLimitFix::SetLightPosition(LightLimitFix::LightData& a_light, RE::NiPoint3 a_initialPosition, bool a_cached)
{
	auto& state = State::GetSingleton()->shadowState;
	for (int eyeIndex = 0; eyeIndex < eyeCount; eyeIndex++) {
		RE::NiPoint3 eyePosition;
		Matrix viewMatrix;

		if (a_cached) {
			eyePosition = eyePositionCached[eyeIndex];
			viewMatrix = viewMatrixCached[eyeIndex];
		} else {
			eyePosition = eyeCount == 1 ?
			                  state->GetRuntimeData().posAdjust.getEye(eyeIndex) :
			                  state->GetVRRuntimeData().posAdjust.getEye(eyeIndex);
			viewMatrix = eyeCount == 1 ?
			                 state->GetRuntimeData().cameraData.getEye(eyeIndex).viewMat :
			                 state->GetVRRuntimeData().cameraData.getEye(eyeIndex).viewMat;
		}

		auto worldPos = a_initialPosition - eyePosition;
		a_light.positionWS[eyeIndex].data.x = worldPos.x;
		a_light.positionWS[eyeIndex].data.y = worldPos.y;
		a_light.positionWS[eyeIndex].data.z = worldPos.z;
		a_light.positionVS[eyeIndex].data = DirectX::SimpleMath::Vector3::Transform(a_light.positionWS[eyeIndex].data, viewMatrix);
	}
}

float LightLimitFix::CalculateLuminance(CachedParticleLight& light, RE::NiPoint3& point)
{
	// See BSLight::CalculateLuminance_14131D3D0
	// Performs lighting on the CPU which is identical to GPU code

	auto lightDirection = light.position - point;
	float lightDist = lightDirection.Length();
	float intensityFactor = std::clamp(lightDist / light.radius, 0.0f, 1.0f);
	float intensityMultiplier = 1 - intensityFactor * intensityFactor;

	return light.grey * intensityMultiplier;
}

void LightLimitFix::AddParticleLightLuminance(RE::NiPoint3& targetPosition, int& numHits, float& lightLevel)
{
	std::lock_guard<std::shared_mutex> lk{ cachedParticleLightsMutex };
	particleLightsDetectionHits = 0;
	if (settings.EnableParticleLightsDetection) {
		for (auto& light : cachedParticleLights) {
			auto luminance = CalculateLuminance(light, targetPosition);
			lightLevel += luminance;
			if (luminance > 0.0)
				particleLightsDetectionHits++;
		}
	}
	numHits += particleLightsDetectionHits;
}

void LightLimitFix::Bind()
{
	auto& context = State::GetSingleton()->context;
	auto accumulator = RE::BSGraphics::BSShaderAccumulator::GetCurrentAccumulator();

	auto reflections = (!REL::Module::IsVR() ?
							   RE::BSGraphics::RendererShadowState::GetSingleton()->GetRuntimeData().cubeMapRenderTarget :
							   RE::BSGraphics::RendererShadowState::GetSingleton()->GetVRRuntimeData().cubeMapRenderTarget) == RE::RENDER_TARGETS_CUBEMAP::kREFLECTIONS;

	if (!boundViews) {
		boundViews = true;

		ID3D11ShaderResourceView* view = perPass->srv.get();
		context->PSSetShaderResources(32, 1, &view);

		view = strictLightData->srv.get();
		context->PSSetShaderResources(37, 1, &view);
	}

	if (reflections || accumulator->GetRuntimeData().activeShadowSceneNode != RE::BSShaderManager::State::GetSingleton().shadowSceneNode[0]) {
		if (perPassData.EnableGlobalLights) {
			perPassData.EnableGlobalLights = false;

			D3D11_MAPPED_SUBRESOURCE mapped;
			DX::ThrowIfFailed(context->Map(perPass->resource.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
			size_t bytes = sizeof(PerPass);
			memcpy_s(mapped.pData, bytes, &perPassData, bytes);
			context->Unmap(perPass->resource.get(), 0);
		}
	} else {
		if (!rendered) {
			UpdateLights();
			rendered = true;
			ID3D11ShaderResourceView* views[3]{};
			views[0] = lights->srv.get();
			views[1] = lightList->srv.get();
			views[2] = lightGrid->srv.get();
			context->PSSetShaderResources(17, ARRAYSIZE(views), views);
			perPassData.EnableGlobalLights = false;
		}

		if (!perPassData.EnableGlobalLights) {
			auto viewport = RE::BSGraphics::State::GetSingleton();

			perPassData.LightsNear = lightsNear;
			perPassData.LightsFar = lightsFar;

			const auto imageSpaceManager = RE::ImageSpaceManager::GetSingleton();
			auto bTAA = !REL::Module::IsVR() ? imageSpaceManager->GetRuntimeData().BSImagespaceShaderISTemporalAA->taaEnabled :
			                                   imageSpaceManager->GetVRRuntimeData().BSImagespaceShaderISTemporalAA->taaEnabled;

			perPassData.FrameCount = viewport->uiFrameCount * (bTAA || State::GetSingleton()->upscalerLoaded);
			perPassData.EnableGlobalLights = true;
			perPassData.EnableContactShadows = settings.EnableContactShadows;
			perPassData.EnableLightsVisualisation = settings.EnableLightsVisualisation;
			perPassData.LightsVisualisationMode = settings.LightsVisualisationMode;

			D3D11_MAPPED_SUBRESOURCE mapped;
			DX::ThrowIfFailed(context->Map(perPass->resource.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
			size_t bytes = sizeof(PerPass);
			memcpy_s(mapped.pData, bytes, &perPassData, bytes);
			context->Unmap(perPass->resource.get(), 0);
		}
	}
}

bool LightLimitFix::IsValidLight(RE::BSLight* a_light)
{
	return a_light && !a_light->light->GetFlags().any(RE::NiAVObject::Flag::kHidden);
}

bool LightLimitFix::IsGlobalLight(RE::BSLight* a_light)
{
	return !(a_light->portalStrict || !a_light->portalGraph);
}

struct VertexColor
{
	std::uint8_t data[4];
};

struct VertexPosition
{
	std::uint8_t data[3];
};

std::string ExtractTextureStem(std::string_view a_path)
{
	if (a_path.size() < 1)
		return {};

	auto lastSeparatorPos = a_path.find_last_of("\\/");
	if (lastSeparatorPos == std::string::npos)
		return {};

	a_path = a_path.substr(lastSeparatorPos + 1);
	a_path.remove_suffix(4);  // Remove ".dds"

#pragma warning(push)
#pragma warning(disable: 4244)
	auto textureNameView = a_path | std::views::transform(::tolower);
	std::string textureName = { textureNameView.begin(), textureNameView.end() };
#pragma warning(pop)

	return textureName;
}

std::optional<LightLimitFix::ConfigPair> LightLimitFix::GetParticleLightConfigs(RE::BSRenderPass* a_pass)
{
	// see https://www.nexusmods.com/skyrimspecialedition/articles/1391
	if (settings.EnableParticleLights) {
		if (auto shaderProperty = netimmerse_cast<RE::BSEffectShaderProperty*>(a_pass->shaderProperty)) {
			if (!shaderProperty->lightData) {
				if (auto material = shaderProperty->GetMaterial()) {
					if (!material->sourceTexturePath.empty()) {
						std::string textureName = ExtractTextureStem(material->sourceTexturePath.c_str());
						if (textureName.size() < 1)
							return std::nullopt;

						auto& configs = ParticleLights::GetSingleton()->particleLightConfigs;
						auto it = configs.find(textureName);
						if (it == configs.end())
							return std::nullopt;

						ParticleLights::Config* config = &it->second;
						ParticleLights::GradientConfig* gradientConfig = nullptr;
						if (!material->greyscaleTexturePath.empty()) {
							textureName = ExtractTextureStem(material->greyscaleTexturePath.c_str());
							if (textureName.size() < 1)
								return std::nullopt;

							auto& gradientConfigs = ParticleLights::GetSingleton()->particleLightGradientConfigs;
							auto itGradient = gradientConfigs.find(textureName);
							if (itGradient == gradientConfigs.end())
								return std::nullopt;
							gradientConfig = &itGradient->second;
						}
						return std::make_pair(config, gradientConfig);
					}
				}
			}
		}
	}
	return std::nullopt;
}

bool LightLimitFix::CheckParticleLights(RE::BSRenderPass* a_pass, uint32_t)
{
	auto configs = GetParticleLightConfigs(a_pass);
	if (configs.has_value()) {
		if (AddParticleLight(a_pass, configs.value())) {
			return !(settings.EnableParticleLightsCulling && configs->first->cull);
		}
	}
	return true;
}

bool LightLimitFix::AddParticleLight(RE::BSRenderPass* a_pass, LightLimitFix::ConfigPair a_config)
{
	auto shaderProperty = netimmerse_cast<RE::BSEffectShaderProperty*>(a_pass->shaderProperty);
	auto material = shaderProperty->GetMaterial();
	auto config = a_config.first;
	auto gradientConfig = a_config.second;

	a_pass->geometry->IncRefCount();
	if (const auto particleSystem = netimmerse_cast<RE::NiParticleSystem*>(a_pass->geometry)) {
		if (auto particleData = particleSystem->GetParticleRuntimeData().particleData.get()) {
			particleData->IncRefCount();
		}
	}

	RE::NiColorA color;
	color.red = material->baseColor.red * material->baseColorScale;
	color.green = material->baseColor.green * material->baseColorScale;
	color.blue = material->baseColor.blue * material->baseColorScale;
	color.alpha = material->baseColor.alpha * shaderProperty->alpha;

	if (auto emittance = shaderProperty->unk88) {
		color.red *= emittance->red;
		color.green *= emittance->green;
		color.blue *= emittance->blue;
	}

	if (auto rendererData = a_pass->geometry->GetGeometryRuntimeData().rendererData) {
		if (auto triShape = a_pass->geometry->AsTriShape()) {
			uint32_t vertexSize = rendererData->vertexDesc.GetSize();
			if (rendererData->vertexDesc.HasFlag(RE::BSGraphics::Vertex::Flags::VF_COLORS)) {
				uint32_t offset = rendererData->vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::Attribute::VA_COLOR);

				uint8_t maxAlpha = 0u;
				VertexColor* vertexColor = nullptr;
				bool alphaOne = false;
				bool alphaZero = false;

				for (int v = 0; v < triShape->GetTrishapeRuntimeData().vertexCount; v++) {
					if (VertexColor* vertex = reinterpret_cast<VertexColor*>(&rendererData->rawVertexData[vertexSize * v + offset])) {
						uint8_t alpha = vertex->data[3];
						alphaZero = alphaOne || alpha == 0;
						alphaOne = alphaOne || alpha == 255;
						if (alpha > maxAlpha) {
							maxAlpha = alpha;
							vertexColor = vertex;
						}
					}
				}

				if (!vertexColor || !alphaZero || !alphaOne)
					return false;

				color.red *= vertexColor->data[0] / 255.f;
				color.green *= vertexColor->data[1] / 255.f;
				color.blue *= vertexColor->data[2] / 255.f;
				if (shaderProperty->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kVertexAlpha)) {
					color.alpha *= vertexColor->data[3] / 255.f;
				}
			}
		}
	}

	if (gradientConfig) {
		auto grey = float3(config->colorMult.red, config->colorMult.green, config->colorMult.blue).Dot(float3(0.3f, 0.59f, 0.11f));
		color.red *= grey * gradientConfig->color.red;
		color.green *= grey * gradientConfig->color.green;
		color.blue *= grey * gradientConfig->color.blue;
	} else {
		color.red *= config->colorMult.red;
		color.green *= config->colorMult.green;
		color.blue *= config->colorMult.blue;
	}

	queuedParticleLights.insert({ a_pass->geometry, { color, *config } });
	return true;
}

enum class GrassShaderTechniques
{
	RenderDepth = 8,
};

void LightLimitFix::Draw(const RE::BSShader* shader, const uint32_t)
{
	switch (shader->shaderType.get()) {
	case RE::BSShader::Type::Lighting:
	case RE::BSShader::Type::Grass:
	case RE::BSShader::Type::Effect:
	case RE::BSShader::Type::Water:
		Bind();
		break;
	}
}

void LightLimitFix::PostPostLoad()
{
	ParticleLights::GetSingleton()->GetConfigs();
	LightLimitFix::InstallHooks();
}

void LightLimitFix::DataLoaded()
{
	auto iMagicLightMaxCount = RE::GameSettingCollection::GetSingleton()->GetSetting("iMagicLightMaxCount");
	iMagicLightMaxCount->data.i = MAXINT32;
	logger::info("[LLF] Unlocked magic light limit");
}

float LightLimitFix::CalculateLightDistance(float3 a_lightPosition, float a_radius)
{
	return (a_lightPosition.x * a_lightPosition.x) + (a_lightPosition.y * a_lightPosition.y) + (a_lightPosition.z * a_lightPosition.z) - (a_radius * a_radius);
}

void LightLimitFix::AddCachedParticleLights(eastl::vector<LightData>& lightsData, LightLimitFix::LightData& light, ParticleLights::Config* a_config, RE::BSGeometry* a_geometry, double a_timer)
{
	static float& lightFadeStart = (*(float*)REL::RelocationID(527668, 414582).address());
	static float& lightFadeEnd = (*(float*)REL::RelocationID(527669, 414583).address());

	float distance = CalculateLightDistance(light.positionWS[0].data, light.radius);

	float dimmer = 0.0f;

	if (distance < lightFadeStart || lightFadeEnd == 0.0f) {
		dimmer = 1.0f;
	} else if (distance <= lightFadeEnd) {
		dimmer = 1.0f - ((distance - lightFadeStart) / (lightFadeEnd - lightFadeStart));
	} else {
		dimmer = 0.0f;
	}

	light.color *= dimmer;

	float distantLightFadeStart = lightsFar * lightsFar * (lightFadeStart / lightFadeEnd);
	float distantLightFadeEnd = lightsFar * lightsFar;

	if (distance < distantLightFadeStart || distantLightFadeEnd == 0.0f) {
		dimmer = 1.0f;
	} else if (distance <= distantLightFadeEnd) {
		dimmer = 1.0f - ((distance - distantLightFadeStart) / (distantLightFadeEnd - distantLightFadeStart));
	} else {
		dimmer = 0.0f;
	}

	light.color *= dimmer;

	if ((light.color.x + light.color.y + light.color.z) > 1e-4 && light.radius > 1e-4) {
		if (a_geometry && a_config && a_config->flicker) {
			auto seed = (std::uint32_t)std::hash<void*>{}(a_geometry);

			siv::PerlinNoise perlin1{ seed };
			siv::PerlinNoise perlin2{ seed + 1 };
			siv::PerlinNoise perlin3{ seed + 2 };
			siv::PerlinNoise perlin4{ seed + 3 };

			auto scaledTimer = a_timer * a_config->flickerSpeed;

			for (int eyeIndex = 0; eyeIndex < eyeCount; eyeIndex++) {
				light.positionWS[eyeIndex].data.x += (float)perlin1.noise1D(scaledTimer) * a_config->flickerMovement;
				light.positionWS[eyeIndex].data.y += (float)perlin2.noise1D(scaledTimer) * a_config->flickerMovement;
				light.positionWS[eyeIndex].data.z += (float)perlin3.noise1D(scaledTimer) * a_config->flickerMovement;
			}

			light.color.x = std::max(0.0f, light.color.x - ((float)perlin4.noise1D_01(scaledTimer) * a_config->flickerIntensity));
			light.color.y = std::max(0.0f, light.color.y - ((float)perlin4.noise1D_01(scaledTimer) * a_config->flickerIntensity));
			light.color.z = std::max(0.0f, light.color.z - ((float)perlin4.noise1D_01(scaledTimer) * a_config->flickerIntensity));
		}

		for (int eyeIndex = 0; eyeIndex < eyeCount; eyeIndex++)
			light.positionVS[eyeIndex].data = DirectX::SimpleMath::Vector3::Transform(light.positionWS[eyeIndex].data, viewMatrixCached[eyeIndex]);

		lightsData.push_back(light);

		CachedParticleLight cachedParticleLight{};
		cachedParticleLight.grey = float3(light.color.x, light.color.y, light.color.z).Dot(float3(0.3f, 0.59f, 0.11f));
		cachedParticleLight.radius = light.radius;
		cachedParticleLight.position = { light.positionWS[0].data.x + eyePositionCached[0].x, light.positionWS[0].data.y + eyePositionCached[0].y, light.positionWS[0].data.z + eyePositionCached[0].z };

		cachedParticleLights.push_back(cachedParticleLight);
	}
}

float3 LightLimitFix::Saturation(float3 color, float saturation)
{
	float grey = color.Dot(float3(0.3f, 0.59f, 0.11f));
	color.x = std::max(std::lerp(grey, color.x, saturation), 0.0f);
	color.y = std::max(std::lerp(grey, color.y, saturation), 0.0f);
	color.z = std::max(std::lerp(grey, color.z, saturation), 0.0f);
	return color;
}

void LightLimitFix::UpdateLights()
{
	auto accumulator = RE::BSGraphics::BSShaderAccumulator::GetCurrentAccumulator();

	if (!(accumulator && accumulator->kCamera))
		return;

	lightsNear = std::max(0.0f, accumulator->kCamera->GetRuntimeData2().viewFrustum.fNear);
	lightsFar = std::min(16384.0f, accumulator->kCamera->GetRuntimeData2().viewFrustum.fFar);

	auto shadowSceneNode = RE::BSShaderManager::State::GetSingleton().shadowSceneNode[0];
	auto& state = State::GetSingleton()->shadowState;

	// Cache data since cameraData can become invalid in first-person

	for (int eyeIndex = 0; eyeIndex < eyeCount; eyeIndex++) {
		eyePositionCached[eyeIndex] = eyeCount == 1 ?
		                                  state->GetRuntimeData().posAdjust.getEye(eyeIndex) :
		                                  state->GetVRRuntimeData().posAdjust.getEye(eyeIndex);
		viewMatrixCached[eyeIndex] = eyeCount == 1 ?
		                                 state->GetRuntimeData().cameraData.getEye(eyeIndex).viewMat :
		                                 state->GetVRRuntimeData().cameraData.getEye(eyeIndex).viewMat;
		viewMatrixCached[eyeIndex].Invert(viewMatrixInverseCached[eyeIndex]);
	}

	RE::NiLight* refLight = nullptr;
	RE::NiLight* magicLight = nullptr;
	RE::BSLight* firstPersonLight = nullptr;
	RE::BSLight* thirdPersonLight = nullptr;

	if (settings.EnableFirstPersonShadows) {
		if (auto playerCamera = RE::PlayerCamera::GetSingleton()) {
			if (playerCamera->IsInFirstPerson() || REL::Module::IsVR()) {
				if (auto player = RE::PlayerCharacter::GetSingleton()) {
					firstPersonLight = player->GetInfoRuntimeData().firstPersonLight.get();
					thirdPersonLight = player->GetInfoRuntimeData().thirdPersonLight.get();
					if (auto light = player->extraList.GetByType<RE::ExtraLight>()) {
						refLight = light->lightData->light.get();
					}
					if (auto light = player->extraList.GetByType<RE::ExtraMagicLight>()) {
						magicLight = light->lightData->light.get();
					}
				}
			}
		}
	}

	eastl::vector<LightData> lightsData{};
	lightsData.reserve(MAX_LIGHTS);

	// Process point lights

	for (auto& e : shadowSceneNode->GetRuntimeData().activePointLights) {
		if (auto bsLight = e.get()) {
			if (auto niLight = bsLight->light.get()) {
				if (IsValidLight(bsLight) && IsGlobalLight(bsLight)) {
					auto& runtimeData = niLight->GetLightRuntimeData();

					LightData light{};
					light.color = { runtimeData.diffuse.red, runtimeData.diffuse.green, runtimeData.diffuse.blue };
					light.color *= runtimeData.fade;
					light.color *= bsLight->lodDimmer;

					light.radius = runtimeData.radius.x;

					SetLightPosition(light, niLight->world.translate);

					static float& lightFadeStart = (*(float*)REL::RelocationID(527668, 414582).address());
					static float& lightFadeEnd = (*(float*)REL::RelocationID(527669, 414583).address());

					float distance = CalculateLightDistance(light.positionWS[0].data, light.radius);

					float distantLightFadeStart = lightsFar * lightsFar * (lightFadeStart / lightFadeEnd);
					float distantLightFadeEnd = lightsFar * lightsFar;

					float dimmer;

					if (distance < distantLightFadeStart || distantLightFadeEnd == 0.0f) {
						dimmer = 1.0f;
					} else if (distance <= distantLightFadeEnd) {
						dimmer = 1.0f - ((distance - distantLightFadeStart) / (distantLightFadeEnd - distantLightFadeStart));
					} else {
						dimmer = 0.0f;
					}

					light.color *= dimmer;

					if ((light.color.x + light.color.y + light.color.z) > 1e-4 && light.radius > 1e-4) {
						light.firstPersonShadow = bsLight == firstPersonLight || bsLight == thirdPersonLight || niLight == refLight || niLight == magicLight;
						lightsData.push_back(light);
					}
				}
			}
		}
	}

	{
		std::lock_guard<std::shared_mutex> lk{ cachedParticleLightsMutex };
		cachedParticleLights.clear();

		LightData clusteredLight{};
		uint32_t clusteredLights = 0;

		auto eyePositionOffset = eyePositionCached[0] - eyePositionCached[1];

		for (const auto& particleLight : particleLights) {
			if (const auto particleSystem = netimmerse_cast<RE::NiParticleSystem*>(particleLight.first);
				particleSystem && particleSystem->GetParticleRuntimeData().particleData.get()) {
				// Process BSGeometry
				auto particleData = particleSystem->GetParticleRuntimeData().particleData.get();

				auto numVertices = particleData->GetActiveVertexCount();
				for (std::uint32_t p = 0; p < numVertices; p++) {
					float radius = particleData->GetParticlesRuntimeData().sizes[p] * 70.0f;

					auto initialPosition = particleData->GetParticlesRuntimeData().positions[p];
					if (!particleSystem->GetParticleSystemRuntimeData().isWorldspace) {
						// Detect first-person meshes
						if ((particleLight.first->GetModelData().modelBound.radius * particleLight.first->world.scale) != particleLight.first->worldBound.radius)
							initialPosition += particleLight.first->worldBound.center;
						else
							initialPosition += particleLight.first->world.translate;
					}

					RE::NiPoint3 positionWS = initialPosition - eyePositionCached[0];

					if (clusteredLights) {
						auto averageRadius = clusteredLight.radius / (float)clusteredLights;
						float radiusDiff = abs(averageRadius - radius);

						auto averagePosition = clusteredLight.positionWS[0].data / (float)clusteredLights;
						float positionDiff = positionWS.GetDistance({ averagePosition.x, averagePosition.y, averagePosition.z });

						if ((radiusDiff + positionDiff) > settings.ParticleLightsOptimisationClusterRadius || !settings.EnableParticleLightsOptimization) {
							clusteredLight.radius /= (float)clusteredLights;
							clusteredLight.positionWS[0].data /= (float)clusteredLights;
							clusteredLight.positionWS[1].data = clusteredLight.positionWS[0].data;
							if (eyeCount == 2) {
								clusteredLight.positionWS[1].data.x += eyePositionOffset.x / (float)clusteredLights;
								clusteredLight.positionWS[1].data.y += eyePositionOffset.y / (float)clusteredLights;
								clusteredLight.positionWS[1].data.z += eyePositionOffset.z / (float)clusteredLights;
							}

							AddCachedParticleLights(lightsData, clusteredLight);

							clusteredLights = 0;
							clusteredLight.color = { 0, 0, 0 };
							clusteredLight.radius = 0;
							clusteredLight.positionWS[0].data = { 0, 0, 0 };
						}
					}

					float alpha = particleLight.second.color.alpha * particleData->GetParticlesRuntimeData().color[p].alpha;
					float3 color;
					color.x = particleLight.second.color.red * particleData->GetParticlesRuntimeData().color[p].red;
					color.y = particleLight.second.color.green * particleData->GetParticlesRuntimeData().color[p].green;
					color.z = particleLight.second.color.blue * particleData->GetParticlesRuntimeData().color[p].blue;
					clusteredLight.color += Saturation(color, settings.ParticleLightsSaturation) * alpha * settings.ParticleBrightness;

					clusteredLight.radius += radius * settings.ParticleRadius * particleLight.second.config.radiusMult;
					clusteredLight.positionWS[0].data.x += positionWS.x;
					clusteredLight.positionWS[0].data.y += positionWS.y;
					clusteredLight.positionWS[0].data.z += positionWS.z;

					clusteredLights++;
				}

			} else {
				// Process billboard
				LightData light{};

				light.color.x = particleLight.second.color.red;
				light.color.y = particleLight.second.color.green;
				light.color.z = particleLight.second.color.blue;

				light.color = Saturation(light.color, settings.ParticleLightsSaturation);

				light.color *= particleLight.second.color.alpha * settings.BillboardBrightness;
				light.radius = particleLight.first->worldBound.radius * settings.BillboardRadius * particleLight.second.config.radiusMult;

				auto position = particleLight.first->world.translate;

				SetLightPosition(light, position);  // Light is complete for both eyes by now

				AddCachedParticleLights(lightsData, light, &particleLight.second.config, particleLight.first, State::GetSingleton()->timer);
			}
		}

		if (clusteredLights) {
			clusteredLight.radius /= (float)clusteredLights;
			clusteredLight.positionWS[0].data /= (float)clusteredLights;
			clusteredLight.positionWS[1].data = clusteredLight.positionWS[0].data;
			if (eyeCount == 2) {
				clusteredLight.positionWS[1].data.x += eyePositionOffset.x / (float)clusteredLights;
				clusteredLight.positionWS[1].data.y += eyePositionOffset.y / (float)clusteredLights;
				clusteredLight.positionWS[1].data.z += eyePositionOffset.z / (float)clusteredLights;
			}
			AddCachedParticleLights(lightsData, clusteredLight);
		}
	}

	static auto& context = State::GetSingleton()->context;

	{
		auto projMatrixUnjittered = eyeCount == 1 ? state->GetRuntimeData().cameraData.getEye().projMatrixUnjittered : state->GetVRRuntimeData().cameraData.getEye().projMatrixUnjittered;
		float fov = atan(1.0f / static_cast<float4x4>(projMatrixUnjittered).m[0][0]) * 2.0f * (180.0f / 3.14159265359f);

		static float _near = 0.0f, _far = 0.0f, _fov = 0.0f, _lightsNear = 0.0f, _lightsFar = 0.0f;
		if (fabs(_near - accumulator->kCamera->GetRuntimeData2().viewFrustum.fNear) > 1e-4 || fabs(_far - accumulator->kCamera->GetRuntimeData2().viewFrustum.fFar) > 1e-4 || fabs(_fov - fov) > 1e-4 || fabs(_lightsNear - lightsNear) > 1e-4 || fabs(_lightsFar - lightsFar) > 1e-4) {
			LightBuildingCB updateData{};
			updateData.InvProjMatrix[0] = DirectX::XMMatrixInverse(nullptr, projMatrixUnjittered);
			if (eyeCount == 1)
				updateData.InvProjMatrix[1] = updateData.InvProjMatrix[0];
			else
				updateData.InvProjMatrix[1] = DirectX::XMMatrixInverse(nullptr, state->GetVRRuntimeData().cameraData.getEye(1).projMatrixUnjittered);
			updateData.LightsNear = lightsNear;
			updateData.LightsFar = lightsFar;

			lightBuildingCB->Update(updateData);

			ID3D11Buffer* buffer = lightBuildingCB->CB();
			context->CSSetConstantBuffers(0, 1, &buffer);

			ID3D11UnorderedAccessView* clusters_uav = clusters->uav.get();
			context->CSSetUnorderedAccessViews(0, 1, &clusters_uav, nullptr);

			context->CSSetShader(clusterBuildingCS, nullptr, 0);
			context->Dispatch(CLUSTER_SIZE_X, CLUSTER_SIZE_Y, CLUSTER_SIZE_Z);

			ID3D11UnorderedAccessView* null_uav = nullptr;
			context->CSSetUnorderedAccessViews(0, 1, &null_uav, nullptr);

			_near = accumulator->kCamera->GetRuntimeData2().viewFrustum.fNear;
			_far = accumulator->kCamera->GetRuntimeData2().viewFrustum.fFar;
			_fov = fov;
			_lightsNear = lightsNear;
			_lightsFar = lightsFar;
		}
	}

	{
		lightCount = std::min((uint)lightsData.size(), MAX_LIGHTS);

		D3D11_MAPPED_SUBRESOURCE mapped;
		DX::ThrowIfFailed(context->Map(lights->resource.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
		size_t bytes = sizeof(LightData) * lightCount;
		memcpy_s(mapped.pData, bytes, lightsData.data(), bytes);
		context->Unmap(lights->resource.get(), 0);

		LightCullingCB updateData{};
		updateData.LightCount = lightCount;
		lightCullingCB->Update(updateData);

		ID3D11Buffer* buffer = lightCullingCB->CB();
		context->CSSetConstantBuffers(0, 1, &buffer);

		ID3D11ShaderResourceView* srvs[] = { clusters->srv.get(), lights->srv.get() };
		context->CSSetShaderResources(0, 2, srvs);

		ID3D11UnorderedAccessView* uavs[] = { lightCounter->uav.get(), lightList->uav.get(), lightGrid->uav.get() };
		context->CSSetUnorderedAccessViews(0, 3, uavs, nullptr);

		context->CSSetShader(clusterCullingCS, nullptr, 0);
		context->Dispatch(CLUSTER_SIZE_X / 16, CLUSTER_SIZE_Y / 16, CLUSTER_SIZE_Z / 4);
	}

	context->CSSetShader(nullptr, nullptr, 0);

	ID3D11Buffer* null_buffer = nullptr;
	context->CSSetConstantBuffers(0, 1, &null_buffer);

	ID3D11ShaderResourceView* null_srvs[2] = { nullptr };
	context->CSSetShaderResources(0, 2, null_srvs);

	ID3D11UnorderedAccessView* null_uavs[3] = { nullptr };
	context->CSSetUnorderedAccessViews(0, 3, null_uavs, nullptr);
}

bool LightLimitFix::HasShaderDefine(RE::BSShader::Type shaderType)
{
	switch (shaderType) {
	case RE::BSShader::Type::Lighting:
	case RE::BSShader::Type::Grass:
	case RE::BSShader::Type::Water:
		return true;
	case RE::BSShader::Type::Effect:
		return !REL::Module::IsVR();
	default:
		return false;
	}
}