/// component.cpp
///
/// 2016 blk

#include "blk_core.h"
#include "Matrix.h"
#include "Quaternion.h"
#include "entity_header.h"
#include "game.h"

BLK_DEFINE_COMPONENT(Component)
BLK_DEFINE_COMPONENT(TransformComponent)
BLK_DEFINE_COMPONENT(GameLogicComponent)
BLK_DEFINE_COMPONENT(DamageComponent)
BLK_DEFINE_COMPONENT(ActorComponent)

/*
void CopyVarToComponent( const Component * Src, Component * Dst, const TypeInfoVar * currentVar ) {
	byte * DstByte = ( ( byte * ) Dst );
	byte * SrcByte = ( ( byte * ) Src );

	if ( currentVar->IsArray() ) {
		switch( currentVar->Type() ) {
			case BLK_TYPEINFO_SHADER : {
				std::vector< class Shader * >	& DestShaderList = *( std::vector< class Shader * > *)( &DstByte[currentVar->Offset()] );
				std::vector< class Shader * >	& SrcShaderList = *( std::vector< class Shader * > *)( &SrcByte[currentVar->Offset()] );

				DestShaderList = SrcShaderList;
				break;
			}

			default: {
				byte *const SrcArrayPtr = &SrcByte[currentVar->Offset()];
				byte *const DestArrayPtr = &DstByte[currentVar->Offset()];
				const int arraySize = g_NameToTypeInfoMap->GetVectorSize( SrcByte, currentVar->GetStructName() );
				g_NameToTypeInfoMap->ResizeVector( DestArrayPtr, currentVar->GetStructName(), arraySize );
				for ( int i = 0; i < arraySize; i++ ) {

					byte *const Destin = (byte*)g_NameToTypeInfoMap->GetVectorElement( arrayBytePtr, currentVar->GetStructName(), i );

					if ( currentVar->Type() == BLK_TYPEINFO_STRUCT ) {
						while ( m_Buffer[m_CurrentReadPos] != '{' ) {
							m_CurrentReadPos++;
						}
						ReadComponent( pGameEntity, currentVar->GetStructName(), (Component*)arrayElem );
					} else {
						m_CurrentReadPos = nextStringPos + 1;
						nextStringPos = m_Buffer.find_first_of( " {\n\r\t", m_CurrentReadPos );
						nextToken = m_Buffer.substr( m_CurrentReadPos, nextStringPos - m_CurrentReadPos );
						ReadProperty( currentVar, arrayElem, nextToken, nextStringPos );
						nextStringPos = m_Buffer.find_first_of( " {\n\r\t", m_CurrentReadPos );
					}
					nextStringPos = m_Buffer.find_first_of( " }{\n\r\t", m_CurrentReadPos );
				}
				if ( arraySize == 0 ) {
					m_CurrentReadPos = m_Buffer.find_first_of( "}", m_CurrentReadPos );
				}
				break;
			}
		}	else {
		switch( currentVar->Type() ) {
			case BLK_TYPEINFO_BOOL :
			{
				const bool & srcBool = *(const bool*)&SrcByte[currentVar->Offset()];
				bool & dstBool = *(bool*)&DstByte[currentVar->Offset()];
				dstBool = srcBool;
				break;
			}

			case BLK_TYPEINFO_FLOAT : {
				const float & srcFloat = *( const float* )&SrcByte[currentVar->Offset()];
				float & dstFloat = *( float* )&DstByte[currentVar->Offset()];
				dstFloat = srcFloat;
				break;
			}

			case BLK_TYPEINFO_INT :
			{
				const int & srcInt = *( const int* )&SrcByte[currentVar->Offset()];
				int & dstInt = *( int* )&DstByte[currentVar->Offset()];
				dstInt = srcInt;
				break;
			}

			case BLK_TYPEINFO_STD_STRING :
			{
				const std::string & srcString =  *(const std::string*)&SrcByte[currentVar->Offset()];
				std::string & dstString = *(std::string*)&DstByte[currentVar->Offset()];
				dstString = srcString;
				break;
			}

			case BLK_TYPEINFO_VECTOR4 :
			{
				const Vec4 & srcVec = *(Vec4*)&SrcByte[currentVar->Offset()];
				Vec4 & dstVec = *(Vec4*)&DstByte[currentVar->Offset()];
				dstVec = srcVec;
				break;
			}

			case BLK_TYPEINFO_VECTOR :
			{
				const Vec3 & srcVec = *(Vec3*)&SrcByte[currentVar->Offset()];
				Vec3 & dstVec = *(Vec3*)&DstByte[currentVar->Offset()];
				dstVec = srcVec;
				break;
			}

			case BLK_TYPEINFO_PTR :
			case BLK_TYPEINFO_TEXTURE :
			case BLK_TYPEINFO_STATICMODEL :
			case BLK_TYPEINFO_SHADER :
			{
				INT_PTR * destPtr = ( INT_PTR * )&DstByte[currentVar->Offset()];
				INT_PTR & destRef = *destPtr;
				const INT_PTR * srcPtr = ( const INT_PTR * )&SrcByte[currentVar->Offset()];
				const INT_PTR & srcRef = *srcPtr;
				destRef = srcRef;
				break;
			}

			case BLK_TYPEINFO_ENUM : {
				int & srcEnum = *(int*)&SrcByte[currentVar->Offset()];
				int & destEnum = *(int*)&DstByte[currentVar->Offset()];
				destEnum = srcEnum;
			}
		}
	}
}*/

/// Component::Constructor
	void Component::Constructor() {
	m_pOwner = nullptr;
	m_pOwningComponent = nullptr;
	m_bIsDirty = false;
	m_IsEnabled = false;
}

/// Component::SetOwner
void Component::SetOwner(Entity* const pGameEntity) {

	if (pGameEntity == nullptr) {
		blk::error("Initializing a Component with a NULL game entity", GetComponentClassName());
	}

	m_pOwner = pGameEntity;
}

/// GameComponent::Constructor
void GameComponent::Constructor() {
	m_StartingLifeTime = -1.0f;
	m_LifeTimeRemaining = -1.0f;
}

/// GameComponent::Enable
void GameComponent::Enable(const bool setEnabled) {

	if (m_IsEnabled == setEnabled) {
		return;
	}

	m_IsEnabled = setEnabled;

	if (GetOwner() == nullptr || GetOwner()->is_prefab() == true) {
		return;
	}

	enable_internal(setEnabled);

	if (setEnabled) {
		m_LifeTimeRemaining = m_StartingLifeTime;
	}
}

/// GameComponent::EditorChange
void GameComponent::editor_change(const std::string& propertyName) {
	Super::editor_change(propertyName);

	if (propertyName == "Enabled") {
		if (GetOwner() == nullptr || GetOwner()->is_prefab() == true) {
			return;
		}

		enable_internal(m_IsEnabled);
	}
}

/// GameComponent::Update
void GameComponent::Update(const float DeltaTimeSeconds) {
	if (m_LifeTimeRemaining >= 0.0f) {
		m_LifeTimeRemaining -= DeltaTimeSeconds;
		if (m_LifeTimeRemaining < 0) {
			Enable(false);
			LifeTimeExpired();
			return;
		}
	}

	update_internal(DeltaTimeSeconds);
	m_bIsDirty = false;
}

/// GameComponent::GetOwnerName
String GameComponent::owner_name() const {
	return GetOwner()->name();
}

/// GameComponent::GetOwnerPosition
Vec3 GameComponent::owner_position() const {
	return ((GameEntity*)GetOwner())->position();
}

/// GameComponent::GetOwnerScale
Vec3 GameComponent::owner_scale() const {
	return ((GameEntity*)GetOwner())->scale();
}

/// GameComponent::GetOwnerRotation
Quat4 GameComponent::owner_rotation() const {
	return ((GameEntity*)GetOwner())->rotation();
}

/// GameComponent::SetOwnerPosition
void GameComponent::SetOwnerPosition(const Vec3& position) {
	GetOwner()->set_position(position);
}

/// GameComponent::SetOwnerRotation
void GameComponent::SetOwnerRotation(const Quat4& rotation) {
	GetOwner()->set_rotation(rotation);
}

/// TransformComponent::Constructor
void TransformComponent::Constructor() {
	m_position.set(0.0f, 0.0f, 0.0f);
	m_scale.set(1.0f, 1.0f, 1.0f);
	m_rotation.set(0.0f, 0.0f, 0.0f, 1.0f);
}

/// TransformComponent::position
const Vec3 TransformComponent::position() const {
	const GameEntity* const owner = GetOwner();
	if (owner != nullptr) {
		if (owner->component(0) == this) {
			return m_position;
		}

		const Mat4 parentRotation = owner->rotation().to_mat4();
		const Vec3 worldPosition = parentRotation.transform_point(m_position);

		return parentRotation.transform_point(m_position) + owner->position();
	}

	return m_position;
}

/// TransformComponent::scale
const Vec3 TransformComponent::scale() const {
	const GameEntity* const owner = GetOwner();
	if (owner != nullptr) {
		if (owner->component(0) == this) {
			return m_scale;
		}
		return owner->scale() * m_scale;
	}

	return m_scale;
}

/// TransformComponent::rotation
const Quat4 TransformComponent::rotation() const {
	const GameEntity* const owner = GetOwner();
	if (owner != nullptr) {
		if (owner->component(0) == this) {
			return m_rotation;
		}
		return owner->rotation() * m_rotation;
	}

	return m_rotation;
}

/// GameLogicComponent::Constructor
void GameLogicComponent::Constructor() {
	m_DummyTemp = 0;
}

/// GameLogicComponent::update_internal
void GameLogicComponent::update_internal(const float DeltaTime) {
	START_SCOPED_TIMER(CLOTH_COMPONENT);
	Super::update_internal(DeltaTime);
}

/// DamageComponent::Constructor
void DamageComponent::Constructor() {
	m_MinDamage = 10.0f;
	m_MaxDamage = 10.0f;
}

/// ActorComponent::Constructor
void ActorComponent::Constructor() {
	m_MaxHealth = 10.0f;
	m_CurrentHealth = m_MaxHealth;
}

/// ActorComponent::Constructor
void ActorComponent::enable_internal(const bool bIsEnabled) {
	Super::enable_internal(bIsEnabled);

	if (bIsEnabled) {
		m_CurrentHealth = m_MaxHealth;
	}
}

/// ActorComponent::take_damage
void ActorComponent::take_damage(const class DamageComponent* const pDamageComponent, const GameLogicComponent* const attackerComponent) {
	if (pDamageComponent == nullptr) {
		return;
	}

	m_CurrentHealth -= pDamageComponent->GetMaxDamage();
}

/// DeleteEntityComponent::Constructor
void DeleteEntityComponent::Constructor() {
	m_Dummy = 1.0f;
}

/// DeleteEntityComponent::LifeTimeExpired
void DeleteEntityComponent::LifeTimeExpired() {
	g_pGame->RemoveGameEntity(GetOwner());
}

/// PlayerStartComponent::Constructor
void PlayerStartComponent::Constructor() {
	m_DummyVar = 0;
}

/// AnimEvent::Constructor()
void AnimEvent::Constructor() {
	m_EventTime = 0.0f;
	m_EventValue = 0.0f;
}

/// VectorAnimEvent::Constructor()
void VectorAnimEvent::Constructor() {
	m_EventTime = 0.0f;
	m_EventValue = Vec3::zero;
}

/// AnimEvent::Evaluate
float AnimEvent::Evaluate(const std::vector<AnimEvent>& eventList, const float t) {
	if (eventList.size() == 0) {
		blk::warn("AnimEvent::Evaluate() - Empty event list");
		return 0;
	}

	for (size_t i = 0; i < eventList.size(); i++) {
		if (t < eventList[i].GetEventTime()) {
			if (i == 0) {
				return eventList[0].GetEventValue();
			}

			const float lerp = (t - eventList[i - 1].GetEventTime()) / (eventList[i].GetEventTime() - eventList[i - 1].GetEventTime());
			return blk::lerp(eventList[i - 1].GetEventValue(), eventList[i].GetEventValue(), lerp);
		}
	}

	return eventList.back().GetEventValue();
}

/// VectorAnimEvent::Evaluate
Vec4 VectorAnimEvent::Evaluate(const std::vector<VectorAnimEvent>& eventList, const float t) {
	if (eventList.size() == 0) {
		blk::warn("VectorAnimEvent::Evaluate() - Empty event list");
		return Vec3::zero;
	}

	for (int i = 0; i < eventList.size(); i++) {
		if (t < eventList[i].GetEventTime()) {
			if (i == 0) {
				return eventList[0].GetEventValue();
			}

			const float lerp = (t - eventList[i - 1].GetEventTime()) / (eventList[i].GetEventTime() - eventList[i - 1].GetEventTime());
			//	blk::log( "i = %d, lerp = %f.  time1 = %f, time2 = %f", i, lerp, eventList[i-1].GetEventTime(), t - eventList[i-1].GetEventTime() );
			return blk::lerp(eventList[i - 1].GetEventValue(), eventList[i].GetEventValue(), lerp);
		}
	}

	//	blk::log( "Gah!");
	return eventList.back().GetEventValue();
}

/// EditorGlobalSettingsComponent::Constructor
void EditorGlobalSettingsComponent::Constructor() {
	m_CameraSpeedIdx = 0;
}

/// EditorLevelSettingsComponent::Constructor
void EditorLevelSettingsComponent::Constructor() {
	m_CameraPosition = Vec3::zero;
	m_CameraRotation = Quat4::identity;
}
