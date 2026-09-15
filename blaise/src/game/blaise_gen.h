// AUTO GENERATED TYPE INFO //////////////////////////////////////////

GenerateEnum(
	ELevelType, "ELevelType",
	AddEnumField(LevelType_Menu, "Menu")
		AddEnumField(LevelType_2D, "2D"))

	GenerateEnum(
		ECameraMoveMode, "ECameraMoveMode",
		AddEnumField(MoveMode_None, "None")
			AddEnumField(MoveMode_Follow, "Follow"))

		GenerateClass(
			CannonCameraComponent,
			AddField("NearPlane", BLK_TYPEINFO_FLOAT, CannonCameraComponent, m_NearPlane, false, "")
				AddField("FarPlane", BLK_TYPEINFO_FLOAT, CannonCameraComponent, m_FarPlane, false, "")
					AddField("MovementMode", BLK_TYPEINFO_ENUM, CannonCameraComponent, m_MoveMode, false, "ECameraMoveMode")
						AddField("PositionOffset", BLK_TYPEINFO_VECTOR, CannonCameraComponent, m_positionOffset, false, "")
							AddField("LookAtOffset", BLK_TYPEINFO_VECTOR, CannonCameraComponent, m_LookAtOffset, false, ""))

			GenerateClass(
				CannonCameraShakeComponent,
				AddField("Duration", BLK_TYPEINFO_FLOAT, CannonCameraShakeComponent, m_Duration, false, "")
					AddField("AmplitudeX", BLK_TYPEINFO_FLOAT, CannonCameraShakeComponent, m_AmplitudeX, false, "")
						AddField("FrequencyX", BLK_TYPEINFO_FLOAT, CannonCameraShakeComponent, m_FrequencyX, false, "")
							AddField("AmplitudeY", BLK_TYPEINFO_FLOAT, CannonCameraShakeComponent, m_AmplitudeY, false, "")
								AddField("FrequencyY", BLK_TYPEINFO_FLOAT, CannonCameraShakeComponent, m_FrequencyY, false, "")
									AddField("ActivateOnEnable", BLK_TYPEINFO_BOOL, CannonCameraShakeComponent, m_bActivateOnEnable, false, "")
										AddField("ActivationDelay", BLK_TYPEINFO_FLOAT, CannonCameraShakeComponent, m_ActivationDelaySeconds, false, ""))

				GenerateClass(
					CannonLevelComponent,
					AddField("Dummy2", BLK_TYPEINFO_FLOAT, CannonLevelComponent, m_Dummy2, false, ""))

					GenerateClass(
						CannonFogComponent,
						AddField("Shader", BLK_TYPEINFO_SHADER, CannonFogComponent, m_shader, false, "")
							AddField("StartDist", BLK_TYPEINFO_FLOAT, CannonFogComponent, m_FogStartDist, false, "")
								AddField("EndDist", BLK_TYPEINFO_FLOAT, CannonFogComponent, m_FogEndDist, false, "")
									AddField("Clamp", BLK_TYPEINFO_FLOAT, CannonFogComponent, m_FogClamp, false, "")
										AddField("Color", BLK_TYPEINFO_VECTOR4, CannonFogComponent, m_FogColor, false, ""))
