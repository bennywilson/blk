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

GenerateClass(
	CannonHealthBarUIComponent,
	AddField("HealthBarWarningFlashThreshold", KBTYPEINFO_FLOAT, CannonHealthBarUIComponent, m_healthBarWarningFlashThreshold, false, "")
	AddField("HealthBarWarningFlashSpeed", KBTYPEINFO_FLOAT, CannonHealthBarUIComponent, m_healthBarWarningFlashSpeed, false, "")	
)

GenerateClass(
	CannonBallUIComponent,
	AddField("SparkRelativePosition", KBTYPEINFO_VECTOR, CannonBallUIComponent, m_SparkRelativePosition, false, "")
	AddField("SparkRelativeSize", KBTYPEINFO_VECTOR, CannonBallUIComponent, m_SparkRelativeSize, false, "")
	AddField("BoomRelativePosition", KBTYPEINFO_VECTOR, CannonBallUIComponent, m_BoomRelativePosition, false, "")
	AddField("BoomRelativeSize", KBTYPEINFO_VECTOR, CannonBallUIComponent, m_BoomRelativeSize, false, "")
	AddField("SmokeRelativePosition", KBTYPEINFO_VECTOR, CannonBallUIComponent, m_SmokeRelativePosition, false, "")
	AddField("SmokeRelativeSize", KBTYPEINFO_VECTOR, CannonBallUIComponent, m_SmokeRelativeSize, false, "")
)

GenerateClass(
	CannonBallPauseMenuUIComponent,
	AddField("SliderWidgets", KBTYPEINFO_STRUCT, CannonBallPauseMenuUIComponent, m_SliderWidgets, true, "kbUISlider")
	AddField("Widgets", KBTYPEINFO_STRUCT, CannonBallPauseMenuUIComponent, m_Widgets, true, "kbUIWidgetComponent")
	AddField("WidgetSize", KBTYPEINFO_VECTOR, CannonBallPauseMenuUIComponent, m_WidgetSize, false, "")
	AddField("StartingWidgetAnchor", KBTYPEINFO_VECTOR, CannonBallPauseMenuUIComponent, m_StartingWidgetAnchorPt, false, "")
	AddField("SpaceBetweenWidgets", KBTYPEINFO_FLOAT, CannonBallPauseMenuUIComponent, m_SpaceBetweenWidgets, false, "")
	AddField("TestSound", KBTYPEINFO_STRUCT, CannonBallPauseMenuUIComponent, m_VolumeSliderTestWav, true, "kbSoundData")
)

GenerateClass(
	CannonBallMainMenuComponent,
	AddField("ActionVO", KBTYPEINFO_STRUCT, CannonBallMainMenuComponent, m_ActionVO, true, "kbSoundData")
)

GenerateClass(
	CannonBallYesNoPromptComponent,
)

GenerateClass(
	CannonBallScrollComponent,
	AddField("ScrollRate", KBTYPEINFO_VECTOR, CannonBallScrollComponent, m_ScrollRate, false, "")
)

GenerateClass(
	CannonBallGameSettingsComponent,
	AddField("MasterVolume", KBTYPEINFO_INT, CannonBallGameSettingsComponent, m_Volume, false, "")
	AddField("Brightness", KBTYPEINFO_INT, CannonBallGameSettingsComponent, m_Brightness, false, "")
	AddField("VideoQuality", KBTYPEINFO_INT, CannonBallGameSettingsComponent, m_VisualQuality, false, "")
)
