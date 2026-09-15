/// type_info.cpp
///
/// 2016 blk

#include "blk_core.h"
#include "Matrix.h"
//#include "Quaternion.h"
#include "bounds.h"
#include "entity_header.h"
#include "render_defs.h"
#include "material.h"
#include "level_component.h"
#include "breakable_component.h"

using namespace std;

NameToTypeInfoMap* g_NameToTypeInfoMap = nullptr;

/// NameToTypeInfoMap::NameToTypeInfoMap()
NameToTypeInfoMap::NameToTypeInfoMap() {
	RegisterVectorOperations<String>("String");
	RegisterVectorOperations<float>("float");
	RegisterVectorOperations<Vec4>("Vec4");
}

/// NameToTypeInfoMap::~NameToTypeInfoMap()
NameToTypeInfoMap::~NameToTypeInfoMap() {
}

/// NameToTypeInfoMap::AddTypeInfo()
void NameToTypeInfoMap::AddTypeInfo(const TypeInfoClass* const classToAdd) {
	m_Map[classToAdd->GetClassName()] = classToAdd;
}

/// NameToTypeInfoMap::AddEnum()
void NameToTypeInfoMap::AddEnum(const std::string& enumName, const std::vector< std::string >& enumFields) {
	m_EnumMap[enumName] = enumFields;
}

Component* ConstructClassFromName(const std::string& className) {
	// find(), not GetTypeInfoFromClassName(), which inserts a null entry for every miss.
	const std::map<std::string, const TypeInfoClass*>& class_map = g_NameToTypeInfoMap->GetClassMap();
	auto it = class_map.find(className);

	// Accepts the legacy kb-prefixed class names still found in older levels and packages.
	if (it == class_map.end() && className.compare(0, 2, "kb") == 0) {
		it = class_map.find(className.substr(2));
	}

	if (it == class_map.end() || it->second == nullptr) {
		return nullptr;
	}

	return it->second->ConstructInstance();
}


// Should be a macro with each member specified

EBlendMode_Enum EBlendmode_EnumClass;

ECullMode_Enum ECullMode_EnumClass;

ERenderPass_Enum ERenderPass_EnumClass;

EClothType_Enum EClothType_EnumClass;

ECollisionType_Enum ECollisionType_EnumClass;

EBillboardType_Enum EBillboardType_EnumClass;

eWidgetAnchor_Enum EWidgetAnchor_EnumClass;

eWidgetAxisLock_Enum EWidgetAxisLock_EnumClass;

BLK_DEFINE_CLASS(Component)

BLK_DEFINE_CLASS(EditorGlobalSettingsComponent)

BLK_DEFINE_CLASS(EditorLevelSettingsComponent)

BLK_DEFINE_CLASS(ShaderParamComponent)

BLK_DEFINE_CLASS(MaterialComponent)

BLK_DEFINE_CLASS(GameComponent)

BLK_DEFINE_CLASS(AnimEvent)

BLK_DEFINE_CLASS(VectorAnimEvent)

BLK_DEFINE_CLASS(ModelEmitter)

BLK_DEFINE_CLASS(RenderComponent)

BLK_DEFINE_CLASS(StaticModelComponent)

BLK_DEFINE_CLASS(AnimComponent)

BLK_DEFINE_CLASS(SkeletalModelComponent)

BLK_DEFINE_CLASS(FlingPhysicsComponent)

BLK_DEFINE_CLASS(Grass)

BLK_DEFINE_CLASS(TerrainComponent)

BLK_DEFINE_CLASS(TransformComponent)

BLK_DEFINE_CLASS(LightComponent)

BLK_DEFINE_CLASS(DirectionalLightComponent)

BLK_DEFINE_CLASS(PointLightComponent)

BLK_DEFINE_CLASS(CylindricalLightComponent)

BLK_DEFINE_CLASS(LightShaftsComponent)

BLK_DEFINE_CLASS(FogComponent)

BLK_DEFINE_CLASS(ParticleComponent)

BLK_DEFINE_CLASS(ClothBone)

BLK_DEFINE_CLASS(BoneCollisionSphere)

BLK_DEFINE_CLASS(ClothComponent)

BLK_DEFINE_CLASS(GameLogicComponent)

BLK_DEFINE_CLASS(DamageComponent)

BLK_DEFINE_CLASS(CollisionComponent)

BLK_DEFINE_CLASS(ActorComponent)

BLK_DEFINE_CLASS(PlayerStartComponent)

BLK_DEFINE_CLASS(SoundData)

BLK_DEFINE_CLASS(DebugSphereCollision)

BLK_DEFINE_CLASS(LevelComponent)

BLK_DEFINE_CLASS(ShaderModifierComponent)

BLK_DEFINE_CLASS(DeleteEntityComponent)

BLK_DEFINE_CLASS(PlaySoundComponent)

BLK_DEFINE_CLASS(UIComponent)

BLK_DEFINE_CLASS(UIWidgetComponent)

BLK_DEFINE_CLASS(UISlider)

eCinematicActionType_Enum eCinematicActionType_EnumClass;

BLK_DEFINE_CLASS(CinematicAction)

BLK_DEFINE_CLASS(CinematicComponent)

BLK_DEFINE_CLASS(GrassZone)

BLK_DEFINE_CLASS(BreakableComponent)

BLK_DEFINE_CLASS(AnimationComponent)

BLK_DEFINE_CLASS(GaussianSplatComponent)

EBreakableBehavior_Enum EBreakableBehavior_EnumClass;
typedef Resource* ResourcePtr;
