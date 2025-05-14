// AUTO GENERATED TYPE INFO //////////////////////////////////////////

GenerateEnum( 
	ELevelType, "ELevelType",
	AddEnumField( LevelType_Menu, "Menu")
	AddEnumField( LevelType_2D, "2D")
)

GenerateEnum( 
	ECameraMoveMode, "ECameraMoveMode",
	AddEnumField( MoveMode_None, "None")
	AddEnumField( MoveMode_Follow, "Follow")
)

GenerateClass(
	CannonCameraComponent,
	AddField("NearPlane", KBTYPEINFO_FLOAT, CannonCameraComponent, m_NearPlane, false, "")
	AddField("FarPlane", KBTYPEINFO_FLOAT, CannonCameraComponent, m_FarPlane, false, "")
	AddField("MovementMode", KBTYPEINFO_ENUM, CannonCameraComponent, m_MoveMode, false, "ECameraMoveMode")
	AddField("PositionOffset", KBTYPEINFO_VECTOR, CannonCameraComponent, m_positionOffset, false, "")
	AddField("LookAtOffset", KBTYPEINFO_VECTOR, CannonCameraComponent, m_LookAtOffset, false, "")
)

GenerateClass(
	CannonCameraShakeComponent,
	AddField("Duration", KBTYPEINFO_FLOAT, CannonCameraShakeComponent, m_Duration, false, "")
	AddField("AmplitudeX", KBTYPEINFO_FLOAT, CannonCameraShakeComponent, m_AmplitudeX, false, "")
	AddField("FrequencyX", KBTYPEINFO_FLOAT, CannonCameraShakeComponent, m_FrequencyX, false, "")
	AddField("AmplitudeY", KBTYPEINFO_FLOAT, CannonCameraShakeComponent, m_AmplitudeY, false, "")
	AddField("FrequencyY", KBTYPEINFO_FLOAT, CannonCameraShakeComponent, m_FrequencyY, false, "")
	AddField("ActivateOnEnable", KBTYPEINFO_BOOL, CannonCameraShakeComponent, m_bActivateOnEnable, false, "")
	AddField("ActivationDelay", KBTYPEINFO_FLOAT, CannonCameraShakeComponent, m_ActivationDelaySeconds, false, "")
)

GenerateClass(
	CannonLevelComponent,
	AddField("Dummy2", KBTYPEINFO_FLOAT, CannonLevelComponent, m_Dummy2, false, "")
)

GenerateClass(
	CannonFogComponent,
	AddField("Shader", KBTYPEINFO_SHADER, CannonFogComponent, m_shader, false, "")
	AddField("StartDist", KBTYPEINFO_FLOAT, CannonFogComponent, m_FogStartDist, false, "")
	AddField("EndDist", KBTYPEINFO_FLOAT, CannonFogComponent, m_FogEndDist, false, "")
	AddField("Clamp", KBTYPEINFO_FLOAT, CannonFogComponent, m_FogClamp, false, "")
	AddField("Color", KBTYPEINFO_VECTOR4, CannonFogComponent, m_FogColor, false, "")	
)
