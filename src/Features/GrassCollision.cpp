#include "GrassCollision.h"

#include "State.h"
#include "Util.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	GrassCollision::Settings,
	EnableGrassCollision,
	RadiusMultiplier,
	DisplacementMultiplier)

enum class GrassShaderTechniques
{
	RenderDepth = 8,
};

void GrassCollision::DrawSettings()
{
	if (ImGui::TreeNodeEx("Grass Collision", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Checkbox("Enable Grass Collision", (bool*)&settings.EnableGrassCollision);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Allows player collision to modify grass position.");
		}

		ImGui::Spacing();
		ImGui::SliderFloat("Radius Multiplier", &settings.RadiusMultiplier, 0.0f, 8.0f);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Distance from collision centres to apply collision.");
		}

		ImGui::SliderFloat("Max Distance from Player", &settings.maxDistance, 0.0f, 1500.0f);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Distance from player to apply collision (NPCs). 0 to disable NPC collisions.");
		}

		ImGui::SliderFloat("Displacement Multiplier", &settings.DisplacementMultiplier, 0.0f, 32.0f);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Strength of each collision on grass position.");
		}

		if (ImGui::SliderInt("Calculation Frame Interval", (int*)&settings.frameInterval, 0, 30)) {
			if (settings.frameInterval)  // increment so mod math works (e.g., skip 1 frame means frame % 2).
				settings.frameInterval++;
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("How many frames to skip before calculating positions again. 0 means calculate every frame (most smooth/costly).");
		}

		ImGui::TreePop();
	}
	if (ImGui::TreeNodeEx("Statistics", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Text(std::format("Active/Total Actors : {}/{}", activeActorCount, totalActorCount).c_str());
		ImGui::Text(std::format("Total Collisions : {}", currentCollisionCount).c_str());
		ImGui::TreePop();
	}
}

static bool GetShapeBound(RE::NiAVObject* a_node, RE::NiPoint3& centerPos, float& radius)
{
	RE::bhkNiCollisionObject* Colliedobj = nullptr;
	if (a_node->collisionObject)
		Colliedobj = a_node->collisionObject->AsBhkNiCollisionObject();

	if (!Colliedobj)
		return false;

	RE::bhkRigidBody* bhkRigid = Colliedobj->body.get() ? Colliedobj->body.get()->AsBhkRigidBody() : nullptr;
	RE::hkpRigidBody* hkpRigid = bhkRigid ? skyrim_cast<RE::hkpRigidBody*>(bhkRigid->referencedObject.get()) : nullptr;
	if (bhkRigid && hkpRigid) {
		RE::hkVector4 massCenter;
		bhkRigid->GetCenterOfMassWorld(massCenter);
		float massTrans[4];
		_mm_store_ps(massTrans, massCenter.quad);
		centerPos = RE::NiPoint3(massTrans[0], massTrans[1], massTrans[2]) * RE::bhkWorld::GetWorldScaleInverse();

		const RE::hkpShape* shape = hkpRigid->collidable.GetShape();
		if (shape) {
			float upExtent = shape->GetMaximumProjection(RE::hkVector4{ 0.0f, 0.0f, 1.0f, 0.0f }) * RE::bhkWorld::GetWorldScaleInverse();
			float downExtent = shape->GetMaximumProjection(RE::hkVector4{ 0.0f, 0.0f, -1.0f, 0.0f }) * RE::bhkWorld::GetWorldScaleInverse();
			auto z_extent = (upExtent + downExtent) / 2.0f;

			float forwardExtent = shape->GetMaximumProjection(RE::hkVector4{ 0.0f, 1.0f, 0.0f, 0.0f }) * RE::bhkWorld::GetWorldScaleInverse();
			float backwardExtent = shape->GetMaximumProjection(RE::hkVector4{ 0.0f, -1.0f, 0.0f, 0.0f }) * RE::bhkWorld::GetWorldScaleInverse();
			auto y_extent = (forwardExtent + backwardExtent) / 2.0f;

			float leftExtent = shape->GetMaximumProjection(RE::hkVector4{ 1.0f, 0.0f, 0.0f, 0.0f }) * RE::bhkWorld::GetWorldScaleInverse();
			float rightExtent = shape->GetMaximumProjection(RE::hkVector4{ -1.0f, 0.0f, 0.0f, 0.0f }) * RE::bhkWorld::GetWorldScaleInverse();
			auto x_extent = (leftExtent + rightExtent) / 2.0f;

			radius = sqrtf(x_extent * x_extent + y_extent * y_extent + z_extent * z_extent);

			return true;
		}
	}

	return false;
}

static bool GetShapeBound(RE::bhkNiCollisionObject* Colliedobj, RE::NiPoint3& centerPos, float& radius)
{
	if (!Colliedobj)
		return false;

	RE::bhkRigidBody* bhkRigid = Colliedobj->body.get() ? Colliedobj->body.get()->AsBhkRigidBody() : nullptr;
	RE::hkpRigidBody* hkpRigid = bhkRigid ? skyrim_cast<RE::hkpRigidBody*>(bhkRigid->referencedObject.get()) : nullptr;
	if (bhkRigid && hkpRigid) {
		RE::hkVector4 massCenter;
		bhkRigid->GetCenterOfMassWorld(massCenter);
		float massTrans[4];
		_mm_store_ps(massTrans, massCenter.quad);
		centerPos = RE::NiPoint3(massTrans[0], massTrans[1], massTrans[2]) * RE::bhkWorld::GetWorldScaleInverse();

		const RE::hkpShape* shape = hkpRigid->collidable.GetShape();
		if (shape) {
			float upExtent = shape->GetMaximumProjection(RE::hkVector4{ 0.0f, 0.0f, 1.0f, 0.0f }) * RE::bhkWorld::GetWorldScaleInverse();
			float downExtent = shape->GetMaximumProjection(RE::hkVector4{ 0.0f, 0.0f, -1.0f, 0.0f }) * RE::bhkWorld::GetWorldScaleInverse();
			auto z_extent = (upExtent + downExtent) / 2.0f;

			float forwardExtent = shape->GetMaximumProjection(RE::hkVector4{ 0.0f, 1.0f, 0.0f, 0.0f }) * RE::bhkWorld::GetWorldScaleInverse();
			float backwardExtent = shape->GetMaximumProjection(RE::hkVector4{ 0.0f, -1.0f, 0.0f, 0.0f }) * RE::bhkWorld::GetWorldScaleInverse();
			auto y_extent = (forwardExtent + backwardExtent) / 2.0f;

			float leftExtent = shape->GetMaximumProjection(RE::hkVector4{ 1.0f, 0.0f, 0.0f, 0.0f }) * RE::bhkWorld::GetWorldScaleInverse();
			float rightExtent = shape->GetMaximumProjection(RE::hkVector4{ -1.0f, 0.0f, 0.0f, 0.0f }) * RE::bhkWorld::GetWorldScaleInverse();
			auto x_extent = (leftExtent + rightExtent) / 2.0f;

			radius = sqrtf(x_extent * x_extent + y_extent * y_extent + z_extent * z_extent);

			return true;
		}
	}

	return false;
}

void GrassCollision::UpdateCollisions()
{
	auto& state = State::GetSingleton()->shadowState;

	auto frameCount = RE::BSGraphics::State::GetSingleton()->uiFrameCount;

	if (settings.frameInterval == 0 || frameCount % settings.frameInterval == 0) {  // only calculate actor positions on some frames
		currentCollisionCount = 0;
		totalActorCount = 0;
		activeActorCount = 0;
		actorList.clear();
		collisionsData.clear();
		// actor query code from po3 under MIT
		// https://github.com/powerof3/PapyrusExtenderSSE/blob/7a73b47bc87331bec4e16f5f42f2dbc98b66c3a7/include/Papyrus/Functions/Faction.h#L24C7-L46
		if (const auto processLists = RE::ProcessLists::GetSingleton(); processLists && settings.maxDistance > 0.0f) {
			std::vector<RE::BSTArray<RE::ActorHandle>*> actors;
			actors.push_back(&processLists->highActorHandles);  // high actors are in combat or doing something interesting
			for (auto array : actors) {
				for (auto& actorHandle : *array) {
					auto actorPtr = actorHandle.get();
					if (actorPtr && actorPtr.get() && actorPtr.get()->Is3DLoaded()) {
						actorList.push_back(actorPtr.get());
						totalActorCount++;
					}
				}
			}
		}

		RE::NiPoint3 playerPosition;
		if (auto player = RE::PlayerCharacter::GetSingleton()) {
			actorList.push_back(player);
			playerPosition = player->GetPosition();
		}

		for (const auto actor : actorList) {
			if (auto root = actor->Get3D(false)) {
				if (playerPosition.GetDistance(actor->GetPosition()) > settings.maxDistance) {  // npc too far so skip
					continue;
				}
				activeActorCount++;
				RE::BSVisit::TraverseScenegraphCollision(root, [&](RE::bhkNiCollisionObject* a_object) -> RE::BSVisit::BSVisitControl {
					RE::NiPoint3 centerPos;
					float radius;
					if (GetShapeBound(a_object, centerPos, radius)) {
						radius *= settings.RadiusMultiplier;
						CollisionSData data{};
						RE::NiPoint3 eyePosition{};
						for (int eyeIndex = 0; eyeIndex < eyeCount; eyeIndex++) {
							if (!REL::Module::IsVR()) {
								eyePosition = state->GetRuntimeData().posAdjust.getEye();
							} else
								eyePosition = state->GetVRRuntimeData().posAdjust.getEye(eyeIndex);
							data.centre[eyeIndex].x = centerPos.x - eyePosition.x;
							data.centre[eyeIndex].y = centerPos.y - eyePosition.y;
							data.centre[eyeIndex].z = centerPos.z - eyePosition.z;
						}
						data.radius = radius;
						currentCollisionCount++;
						collisionsData.push_back(data);
					}
					return RE::BSVisit::BSVisitControl::kContinue;
				});
			}
		}
	}
	if (!currentCollisionCount) {
		CollisionSData data{};
		ZeroMemory(&data, sizeof(data));
		collisionsData.push_back(data);
		currentCollisionCount = 1;
	}

	bool collisionCountChanged = currentCollisionCount != colllisionCount;

	if (!collisions || collisionCountChanged) {
		colllisionCount = currentCollisionCount;

		D3D11_BUFFER_DESC sbDesc{};
		sbDesc.Usage = D3D11_USAGE_DYNAMIC;
		sbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		sbDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		sbDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		sbDesc.StructureByteStride = sizeof(CollisionSData);
		sbDesc.ByteWidth = sizeof(CollisionSData) * colllisionCount;
		collisions = std::make_unique<Buffer>(sbDesc);

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.FirstElement = 0;
		srvDesc.Buffer.NumElements = colllisionCount;
		collisions->CreateSRV(srvDesc);
	}

	auto& context = State::GetSingleton()->context;
	D3D11_MAPPED_SUBRESOURCE mapped;
	DX::ThrowIfFailed(context->Map(collisions->resource.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
	size_t bytes = sizeof(CollisionSData) * colllisionCount;
	memcpy_s(mapped.pData, bytes, collisionsData.data(), bytes);
	context->Unmap(collisions->resource.get(), 0);
}

void GrassCollision::ModifyGrass(const RE::BSShader*, const uint32_t)
{
	if (!loaded)
		return;

	if (updatePerFrame) {
		if (settings.EnableGrassCollision) {
			UpdateCollisions();
		}

		PerFrame perFrameData{};
		ZeroMemory(&perFrameData, sizeof(perFrameData));

		auto& state = State::GetSingleton()->shadowState;
		auto& shaderState = RE::BSShaderManager::State::GetSingleton();

		auto bound = shaderState.cachedPlayerBound;
		RE::NiPoint3 eyePosition{};
		for (int eyeIndex = 0; eyeIndex < eyeCount; eyeIndex++) {
			if (!REL::Module::IsVR()) {
				eyePosition = state->GetRuntimeData().posAdjust.getEye();
			} else
				eyePosition = state->GetVRRuntimeData().posAdjust.getEye(eyeIndex);
			perFrameData.boundCentre[eyeIndex].x = bound.center.x - eyePosition.x;
			perFrameData.boundCentre[eyeIndex].y = bound.center.y - eyePosition.y;
			perFrameData.boundCentre[eyeIndex].z = bound.center.z - eyePosition.z;
			perFrameData.boundCentre[eyeIndex].w = 0.0f;
		}
		perFrameData.boundRadius = bound.radius * settings.RadiusMultiplier;

		perFrameData.Settings = settings;

		perFrame->Update(perFrameData);

		updatePerFrame = false;
	}

	if (settings.EnableGrassCollision) {
		auto& context = State::GetSingleton()->context;

		ID3D11ShaderResourceView* views[1]{};
		views[0] = collisions->srv.get();
		context->VSSetShaderResources(0, ARRAYSIZE(views), views);

		ID3D11Buffer* buffers[1];
		buffers[0] = perFrame->CB();
		context->VSSetConstantBuffers(5, ARRAYSIZE(buffers), buffers);
	}
}

void GrassCollision::Draw(const RE::BSShader* shader, const uint32_t descriptor)
{
	switch (shader->shaderType.get()) {
	case RE::BSShader::Type::Grass:
		ModifyGrass(shader, descriptor);
		break;
	}
}

void GrassCollision::Load(json& o_json)
{
	if (o_json[GetName()].is_object())
		settings = o_json[GetName()];

	Feature::Load(o_json);
}

void GrassCollision::Save(json& o_json)
{
	o_json[GetName()] = settings;
}

void GrassCollision::RestoreDefaultSettings()
{
	settings = {};
}

void GrassCollision::SetupResources()
{
	perFrame = new ConstantBuffer(ConstantBufferDesc<PerFrame>());
}

void GrassCollision::Reset()
{
	updatePerFrame = true;
}

bool GrassCollision::HasShaderDefine(RE::BSShader::Type shaderType)
{
	switch (shaderType) {
	case RE::BSShader::Type::Grass:
		return true;
	default:
		return false;
	}
}