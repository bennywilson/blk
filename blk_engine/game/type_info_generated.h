// AUTO GENERATED TYPE INFO //////////////////////////////////////////

// Enum fields are matched positionally: the enum's integer value indexes this
// list. Entries must stay in declaration order and cover every enumerator, or
// File silently clamps out-of-range values to 0 on save. The strings are the
// on-disk serialization format - renaming one invalidates existing saved levels.

// clang-format off
GenerateEnum(
	ERenderPass, "ERenderPass",
	AddEnumField(RP_FirstPerson, "FirstPersonPass")
	AddEnumField(RP_Lighting, "LightingPass")
	AddEnumField(RP_PreTranslucent, "PreTransucentPass")
	AddEnumField(RP_Translucent, "TranslucentPass")
	AddEnumField(RP_TranslucentWithDepth, "TranslucentWithDepthPass")
	AddEnumField(RP_PostLighting, "Post-LightingPass")
	AddEnumField(RP_InWorldUI, "In-World UI")
	AddEnumField(RP_Distortion, "DistortionPass")
	AddEnumField(RP_PostProcess, "PostProcess")
	AddEnumField(RP_UI, "UIPass")
	AddEnumField(RP_Debug, "DebugPass")
	AddEnumField(RP_MousePicker, "MousePickerPass")
)

GenerateEnum(
	EBlendMode, "EBlendMode",
	AddEnumField(None, "None")
	AddEnumField(Alpha, "Alpha")
	AddEnumField(Additive, "Additive")
)

GenerateEnum(
	ECullMode, "ECullMode",
	AddEnumField(CullMode_ShaderDefault, "ShaderDefault")
	AddEnumField(CullMode_None, "None")
	AddEnumField(CullMode_FrontFaces, "FrontFaces")
	AddEnumField(CullMode_BackFaces, "BackFaces")
)

GenerateEnum(
	EClothType, "EClothType",
	AddEnumField(CT_None, "None")
	AddEnumField(CT_Square, "Square")
)

GenerateEnum(
	ECollisionType, "ECollisionType",
	AddEnumField(CollisionType_Sphere, "Sphere")
	AddEnumField(CollisionType_Box, "Box")
	AddEnumField(CollisionType_StaticMesh, "StaticMesh")
	AddEnumField(CollisionType_CustomTriangles, "CustomTriangles")
)

GenerateClass(
	Component,
	AddField("Enabled", BLK_TYPEINFO_BOOL, GameComponent, m_IsEnabled, false, "")
)

GenerateClass(
	GameComponent,
	AddField("LifeTime", BLK_TYPEINFO_FLOAT, GameComponent, m_StartingLifeTime, false, "")
)

GenerateClass(
	EditorGlobalSettingsComponent,
	AddField("CameraSpeedIdx", BLK_TYPEINFO_INT, EditorGlobalSettingsComponent, m_CameraSpeedIdx, false, "")
)

GenerateClass(
	EditorLevelSettingsComponent,
	AddField("MainCameraPosition", BLK_TYPEINFO_VECTOR, EditorLevelSettingsComponent, m_CameraPosition, false, "")
	AddField("MainCameraRotation", BLK_TYPEINFO_VECTOR4, EditorLevelSettingsComponent, m_CameraRotation, false, "")
)

GenerateClass(
	AnimEvent,
	AddField("EventName", BLK_TYPEINFO_STRING, AnimEvent, m_EventName, false, "")
	AddField("EventTime", BLK_TYPEINFO_FLOAT, AnimEvent, m_EventTime, false, "")
	AddField("EventValue", BLK_TYPEINFO_FLOAT, AnimEvent, m_EventValue, false, "")
)

GenerateClass(
	VectorAnimEvent,
	AddField("EventName", BLK_TYPEINFO_STRING, VectorAnimEvent, m_EventName, false, "")
	AddField("EventTime", BLK_TYPEINFO_FLOAT, VectorAnimEvent, m_EventTime, false, "")
	AddField("EventValue", BLK_TYPEINFO_VECTOR4, VectorAnimEvent, m_EventValue, false, "")
)


GenerateClass(
	ShaderParamComponent,
	AddField("ParamName", BLK_TYPEINFO_STRING, ShaderParamComponent, m_param_name, false, "")
	AddField("Texture", BLK_TYPEINFO_TEXTURE, ShaderParamComponent, m_texture, false, "")
	AddField("Vector", BLK_TYPEINFO_VECTOR4, ShaderParamComponent, m_vector, false, "")
)

GenerateClass(
	MaterialComponent,
	AddField("Shader", BLK_TYPEINFO_SHADER, MaterialComponent, m_shader, false, "")
	AddField("ShaderParams", BLK_TYPEINFO_STRUCT, MaterialComponent, m_shader_params, true, "ShaderParamComponent")
	AddField("CullModeOverride", BLK_TYPEINFO_ENUM, MaterialComponent, m_cull_override, false, "ECullMode")
	AddField("BlendOverride", BLK_TYPEINFO_ENUM, MaterialComponent, m_blend_override, false, "EBlendMode")
)

GenerateClass(
	ModelEmitter,
	AddField("Model", BLK_TYPEINFO_STATICMODEL, ModelEmitter, m_model, false, "")
	AddField("MaterialList", BLK_TYPEINFO_STRUCT, ModelEmitter, m_materials, true, "MaterialComponent")
)

GenerateClass(
	ClothBone,
	AddField("BoneName", BLK_TYPEINFO_STRING, ClothBone, m_BoneName, false, "")
	AddField("NeighborBones", BLK_TYPEINFO_STRING, ClothBone, m_NeighborBones, true, "String")
	AddField("Anchored", BLK_TYPEINFO_BOOL, ClothBone, m_bIsAnchored, false, "")
)

GenerateClass(
	BoneCollisionSphere,
	AddField("BoneName", BLK_TYPEINFO_STRING, BoneCollisionSphere, m_BoneName, false, "")
	AddField("Sphere", BLK_TYPEINFO_VECTOR4, BoneCollisionSphere, m_Sphere, false, "")
)

GenerateClass(
	ClothComponent,
	AddField("BoneInfo", BLK_TYPEINFO_STRUCT, ClothComponent, m_BoneInfo, true, "ClothBone")
	AddField("ClothType", BLK_TYPEINFO_ENUM, ClothComponent, m_ClothType, false, "EClothType")
	AddField("NumIterations", BLK_TYPEINFO_INT, ClothComponent, m_NumConstrainIterations, false, "")
	AddField("Width", BLK_TYPEINFO_INT, ClothComponent, m_Width, false, "")
	AddField("Height", BLK_TYPEINFO_INT, ClothComponent, m_Height, false, "")
	AddField("AdditionalBoneInfo", BLK_TYPEINFO_STRUCT, ClothComponent, m_AdditionalBoneInfo, true, "ClothBone")
	AddField("Collision", BLK_TYPEINFO_STRUCT, ClothComponent, m_CollisionSpheres, true, "BoneCollisionSphere")
	AddField("Gravity", BLK_TYPEINFO_VECTOR, ClothComponent, m_gravity, false, "")
	AddField("MinWindVelocity", BLK_TYPEINFO_VECTOR, ClothComponent, m_MinWindVelocity, false, "")
	AddField("MaxWindVelocity", BLK_TYPEINFO_VECTOR, ClothComponent, m_MaxWindVelocity, false, "")
	AddField("MinWindGustDuration", BLK_TYPEINFO_FLOAT, ClothComponent, m_MinWindGustDuration, false, "")
	AddField("MaxWindGustDuration", BLK_TYPEINFO_FLOAT, ClothComponent, m_MaxWindGustDuration, false, "")
	AddField("AddFakeOscillation", BLK_TYPEINFO_BOOL, ClothComponent, m_bAddFakeOscillation, false, "")
)

GenerateClass(
	TransformComponent,
	AddField("Name", BLK_TYPEINFO_STRING, TransformComponent, m_name, false, "")
	AddField("Position", BLK_TYPEINFO_VECTOR, TransformComponent, m_position, false, "")
	AddField("Scale", BLK_TYPEINFO_VECTOR, TransformComponent, m_scale, false, "")
	AddField("Rotation", BLK_TYPEINFO_VECTOR4, TransformComponent, m_rotation, false, "")
)

GenerateClass(
	RenderComponent,
	AddField("RenderPass", BLK_TYPEINFO_ENUM, RenderComponent, m_render_pass, false, "ERenderPass")
	AddField("RenderOrderBias", BLK_TYPEINFO_FLOAT, RenderComponent, m_render_order_bias, false, "")
	AddField("CastsShadow", BLK_TYPEINFO_BOOL, RenderComponent, m_casts_shadow, false, "")
	AddField("Materials", BLK_TYPEINFO_STRUCT, RenderComponent, m_materials, true, "MaterialComponent")
)

GenerateClass(
	StaticModelComponent,
	AddField("Model", BLK_TYPEINFO_STATICMODEL, StaticModelComponent, m_model, false, "")
)

GenerateClass(
	AnimComponent,
	AddField("AnimationName", BLK_TYPEINFO_STRING, AnimComponent, m_animation_name, false, "")
	AddField("Animation", BLK_TYPEINFO_ANIMATION, AnimComponent, m_animation, false, "")
	AddField("TimeScale", BLK_TYPEINFO_FLOAT, AnimComponent, m_time_scale, false, "")
	AddField("IsLooping", BLK_TYPEINFO_BOOL, AnimComponent, m_is_looping, false, "")
	AddField("AnimationEvent", BLK_TYPEINFO_STRUCT, AnimComponent, m_anim_events, true, "AnimEvent")
)

GenerateClass(
	SkeletalModelComponent,
	AddField("Model", BLK_TYPEINFO_STATICMODEL, SkeletalModelComponent, m_model, false, "")
	AddField("Animations", BLK_TYPEINFO_STRUCT, SkeletalModelComponent, m_Animations, true, "AnimComponent")
	AddField("DebugAnimIndex", BLK_TYPEINFO_INT, SkeletalModelComponent, m_DebugAnimIdx, false, "")
)

GenerateClass(
	FlingPhysicsComponent,
	AddField("MinLinearVelocity", BLK_TYPEINFO_VECTOR, FlingPhysicsComponent, m_min_linear_vel, false, "")
	AddField("MaxLinearVelocity", BLK_TYPEINFO_VECTOR, FlingPhysicsComponent, m_max_linear_vel, false, "")
	AddField("MinAngularSpeed", BLK_TYPEINFO_FLOAT, FlingPhysicsComponent, m_MinAngularSpeed, false, "")
	AddField("MaxAngularSpeed", BLK_TYPEINFO_FLOAT, FlingPhysicsComponent, m_MaxAngularSpeed, false, "")
	AddField("Gravity", BLK_TYPEINFO_VECTOR, FlingPhysicsComponent, m_max_linear_vel, false, "")
)

GenerateClass(
	LightComponent,
	AddField("Color", BLK_TYPEINFO_VECTOR4, LightComponent, m_color, false, "")
	AddField("CastsShadows", BLK_TYPEINFO_BOOL, LightComponent, m_casts_shadow, false, "")
	AddField("Materials", BLK_TYPEINFO_STRUCT, LightComponent, m_materials, true, "MaterialComponent")
)

GenerateClass(
	LightShaftsComponent,
	AddField("Texture", BLK_TYPEINFO_TEXTURE, LightShaftsComponent, m_Texture, false, "")
	AddField("Color", BLK_TYPEINFO_VECTOR4, LightShaftsComponent, m_Color, false, "")
	AddField("BaseWidth", BLK_TYPEINFO_FLOAT, LightShaftsComponent, m_BaseWidth, false, "")
	AddField("BaseHeight", BLK_TYPEINFO_FLOAT, LightShaftsComponent, m_BaseHeight, false, "")
	AddField("IterationWidth", BLK_TYPEINFO_FLOAT, LightShaftsComponent, m_IterationWidth, false, "")
	AddField("IterationHeight", BLK_TYPEINFO_FLOAT, LightShaftsComponent, m_IterationHeight, false, "")
	AddField("NumIteractions", BLK_TYPEINFO_INT, LightShaftsComponent, m_NumIterations, false, "")
	AddField("Directional", BLK_TYPEINFO_BOOL, LightShaftsComponent, m_Directional, false, "")
)

GenerateClass(
	FogComponent,
	AddField("Color", BLK_TYPEINFO_VECTOR4, FogComponent, m_Color, false, "")
	AddField("StartDistance", BLK_TYPEINFO_FLOAT, FogComponent, m_StartDistance, false, "")
	AddField("EndDistance", BLK_TYPEINFO_FLOAT, FogComponent, m_EndDistance, false, "")
)

GenerateClass(
	DirectionalLightComponent,
	AddField("CascadedShadowSplits", BLK_TYPEINFO_FLOAT, DirectionalLightComponent, m_cascade_start_distances, true, "float")
)

GenerateClass(
	PointLightComponent,
	AddField("LightRadius", BLK_TYPEINFO_FLOAT, PointLightComponent, m_radius, false, "float")
)

GenerateClass(
	CylindricalLightComponent,
	AddField("Length", BLK_TYPEINFO_FLOAT, CylindricalLightComponent, m_length, false, "float")
)

GenerateClass(
	Grass,
	AddField("GrassShader", BLK_TYPEINFO_SHADER, Grass, m_pGrassShader, false, "")
	AddField("GrassCellsPerTerrainSide", BLK_TYPEINFO_INT, Grass, m_grassCellsPerTerrainSide, false, "")
	AddField("PatchStartCullDistance", BLK_TYPEINFO_FLOAT, Grass, m_PatchStartCullDistance, false, "")
	AddField("PatchEndCullDistance", BLK_TYPEINFO_FLOAT, Grass, m_PatchEndCullDistance, false, "")
	AddField("PatchesPerCellSide", BLK_TYPEINFO_INT, Grass, m_PatchesPerCellSide, false, "")
	AddField("BladeMinWidth", BLK_TYPEINFO_FLOAT, Grass, m_BladeMinWidth, false, "")
	AddField("BladeMaxWidth", BLK_TYPEINFO_FLOAT, Grass, m_BladeMaxWidth, false, "")
	AddField("BladeMinHeight", BLK_TYPEINFO_FLOAT, Grass, m_BladeMinHeight, false, "")
	AddField("BladeMaxHeight", BLK_TYPEINFO_FLOAT, Grass, m_BladeMaxHeight, false, "")
	AddField("MaxBladeJitterOffset", BLK_TYPEINFO_FLOAT, Grass, m_MaxBladeJitterOffset, false, "")
	AddField("MaxPatchJitterOffset", BLK_TYPEINFO_FLOAT, Grass, m_MaxPatchJitterOffset, false, "")
	AddField("FakeAODarkness", BLK_TYPEINFO_FLOAT, Grass, m_FakeAODarkness, false, "")
	AddField("FakeAOPower", BLK_TYPEINFO_FLOAT, Grass, m_FakeAOPower, false, "")
	AddField("FakeAOClipPlaneFadeOutDist", BLK_TYPEINFO_FLOAT, Grass, m_FakeAOClipPlaneFadeStartDist, false, "")
	AddField("ShaderParams", BLK_TYPEINFO_STRUCT, Grass, m_ShaderParamList, true, "ShaderParamComponent")
)

GenerateClass(
	GrassZone,
	AddField("Center", BLK_TYPEINFO_VECTOR, GrassZone, m_Center, false, "")
	AddField("Extents", BLK_TYPEINFO_VECTOR, GrassZone, m_Extents, false, "")
)

GenerateClass(
	TerrainComponent,
	AddField("HeightMap", BLK_TYPEINFO_TEXTURE, TerrainComponent, m_height_map, false, "")
	AddField("HeightScale", BLK_TYPEINFO_FLOAT, TerrainComponent, m_height_scale, false, "")
	AddField("Width", BLK_TYPEINFO_FLOAT, TerrainComponent, m_world_width, false, "")
	AddField("Dimensions", BLK_TYPEINFO_INT, TerrainComponent, m_vertex_dimensions, false, "")
	AddField("SmoothAmount", BLK_TYPEINFO_INT, TerrainComponent, m_terrain_smooth_filter_width, false, "")
	AddField("SplatMap", BLK_TYPEINFO_TEXTURE, TerrainComponent, m_splat_map, false, "")
	AddField("Grass", BLK_TYPEINFO_STRUCT, TerrainComponent, m_grass, true, "Grass")
	AddField("DebugRegenTerrain", BLK_TYPEINFO_BOOL, TerrainComponent, m_debug_force_gen_terrain, false, "")
	AddField("GrassZones", BLK_TYPEINFO_STRUCT, TerrainComponent, m_grass_zones, true, "GrassZone")
)

GenerateEnum(
	EBillboardType, "EBillboardType",
	AddEnumField(BT_FaceCamera, "FaceCamera")
	AddEnumField(BT_AxialBillboard, "AxialBillboard")
	AddEnumField(BT_AlignAlongVelocity, "AlignAlongVelocity")
)

GenerateClass(
	ParticleComponent,
	AddField("DebugPlayEntity", BLK_TYPEINFO_BOOL, ParticleComponent, m_debug_play_entity, false, "")
	AddField("RenderOrderBias", BLK_TYPEINFO_FLOAT, ParticleComponent, m_render_order_bias, false, "")
	AddField("MaterialList", BLK_TYPEINFO_STRUCT, ParticleComponent, m_materials, true, "MaterialComponent")
	AddField("TotalDuration", BLK_TYPEINFO_FLOAT, ParticleComponent, m_total_duration, false, "")
	AddField("StartDelay", BLK_TYPEINFO_FLOAT, ParticleComponent, m_start_delay, false, "")
	AddField("MinSpawnRate", BLK_TYPEINFO_FLOAT, ParticleComponent, m_min_spawn_rate, false, "")
	AddField("MaxSpawnRate", BLK_TYPEINFO_FLOAT, ParticleComponent, m_max_particle_spawn_rate, false, "")
	AddField("MaxParticlesToEmit", BLK_TYPEINFO_INT, ParticleComponent, m_max_particles_to_emit, false, "")

	AddField("MinDuration", BLK_TYPEINFO_FLOAT, ParticleComponent, m_min_duration, false, "")
	AddField("MaxDuration", BLK_TYPEINFO_FLOAT, ParticleComponent, m_max_duration, false, "")

	AddField("MinStartVelocity", BLK_TYPEINFO_VECTOR, ParticleComponent, m_min_start_velocity, false, "")
	AddField("MaxStartVelocity", BLK_TYPEINFO_VECTOR, ParticleComponent, m_max_start_velocity, false, "")
	AddField("MinEndVelocity", BLK_TYPEINFO_VECTOR, ParticleComponent, m_min_end_velocity, false, "")
	AddField("MaxEndVelocity", BLK_TYPEINFO_VECTOR, ParticleComponent, m_max_end_velocity, false, "")
	AddField("MinEndVelocity", BLK_TYPEINFO_VECTOR, ParticleComponent, m_min_end_velocity, false, "")
	AddField("VelocityCurve", BLK_TYPEINFO_STRUCT, ParticleComponent, m_velocity_over_life_curve, true, "AnimEvent")
	AddField("MaxEndVelocity", BLK_TYPEINFO_VECTOR, ParticleComponent, m_max_end_velocity, false, "")

	AddField("MinStartSize", BLK_TYPEINFO_VECTOR, ParticleComponent, m_min_start_size, false, "")
	AddField("MaxStartSize", BLK_TYPEINFO_VECTOR, ParticleComponent, m_max_start_size, false, "")
	AddField("MinEndSize", BLK_TYPEINFO_VECTOR, ParticleComponent, m_min_end_size, false, "")
	AddField("MaxEndSize", BLK_TYPEINFO_VECTOR, ParticleComponent, m_max_end_size, false, "")

	AddField("StartColor", BLK_TYPEINFO_VECTOR4, ParticleComponent, m_start_color, false, "")
	AddField("EndColor", BLK_TYPEINFO_VECTOR4, ParticleComponent, m_end_color, false, "")

	AddField("SizeOverLife", BLK_TYPEINFO_STRUCT, ParticleComponent, m_size_over_life_curve, true, "VectorAnimEvent")
	AddField("RotationOverLife", BLK_TYPEINFO_STRUCT, ParticleComponent, m_rotation_over_life_curve, true, "VectorAnimEvent")
	AddField("ColorOverLife", BLK_TYPEINFO_STRUCT, ParticleComponent, m_color_over_life_curve, true, "VectorAnimEvent")
	AddField("AlphaOverLife", BLK_TYPEINFO_STRUCT, ParticleComponent, m_alpha_over_life_curve, true, "AnimEvent")

	AddField("MaxBurstCount", BLK_TYPEINFO_INT, ParticleComponent, m_max_burst_count, false, "")
	AddField("MinBurstCount", BLK_TYPEINFO_INT, ParticleComponent, m_min_burst_count, false, "")

	AddField("MinStartRotationRate", BLK_TYPEINFO_FLOAT, ParticleComponent, m_min_start_rotation_rate, false, "")
	AddField("MaxStartRotationRate", BLK_TYPEINFO_FLOAT, ParticleComponent, m_max_start_rotation_rate, false, "")

	AddField("MinEndRotationRate", BLK_TYPEINFO_FLOAT, ParticleComponent, m_min_end_rotation_rate, false, "")
	AddField("MaxEndRotationRate", BLK_TYPEINFO_FLOAT, ParticleComponent, m_max_end_rotation_rate, false, "")

	AddField("MinStart3DRotation", BLK_TYPEINFO_VECTOR4, ParticleComponent, m_min_start_3d_rotation, false, "")
	AddField("MaxStart3DRotation", BLK_TYPEINFO_VECTOR4, ParticleComponent, m_max_start_3d_rotation, false, "")

	AddField("MinStart3DOffset", BLK_TYPEINFO_VECTOR, ParticleComponent, m_min_start_3d_offset, false, "")
	AddField("MaxStart3DOffset", BLK_TYPEINFO_VECTOR, ParticleComponent, m_max_start_3d_offset, false, "")

	AddField("Gravity", BLK_TYPEINFO_VECTOR, ParticleComponent, m_gravity, false, "")

	AddField("ModelEmitter", BLK_TYPEINFO_STRUCT, ParticleComponent, m_model_emitter, true, "StaticModelComponent")

	AddField("ParticleBillboardType", BLK_TYPEINFO_ENUM, ParticleComponent, m_billboard_type, false, "EBillboardType")

)

GenerateClass(
	GameLogicComponent,
	AddField("DummyTemp", BLK_TYPEINFO_INT, GameLogicComponent, m_DummyTemp, false, "")
)

GenerateClass(
	DamageComponent,
	AddField("MinDamage", BLK_TYPEINFO_FLOAT, DamageComponent, m_MinDamage, false, "")
	AddField("MaxDamage", BLK_TYPEINFO_FLOAT, DamageComponent, m_MaxDamage, false, "")
)

GenerateClass(
	ActorComponent,
	AddField("Health", BLK_TYPEINFO_FLOAT, ActorComponent, m_MaxHealth, false, "")
)

GenerateClass(
	PlayerStartComponent,
	AddField("Dummy", BLK_TYPEINFO_FLOAT, PlayerStartComponent, m_DummyVar, false, "")
)

GenerateClass(
	CollisionComponent,
	AddField("CollisionType", BLK_TYPEINFO_ENUM, CollisionComponent, m_CollisionType, false, "ECollisionType")
	AddField("Extent", BLK_TYPEINFO_VECTOR, CollisionComponent, m_Extent, false, "")
	AddField("SphereCollision", BLK_TYPEINFO_STRUCT, CollisionComponent, m_LocalSpaceCollisionSpheres, true, "BoneCollisionSphere")
)

GenerateClass(
	SoundData,
	AddField("WaveFile", BLK_TYPEINFO_SOUNDWAVE, SoundData, m_pWaveFile, false, "")
	AddField("Radius", BLK_TYPEINFO_FLOAT, SoundData, m_Radius, false, "")
	AddField("Volume", BLK_TYPEINFO_FLOAT, SoundData, m_Volume, false, "")
	AddField("Looping", BLK_TYPEINFO_BOOL, SoundData, m_bLooping, false, "")
	AddField("TestPlaySoundNow", BLK_TYPEINFO_BOOL, SoundData, m_bDebugPlaySound, false, "")
)

GenerateClass(
	PlaySoundComponent,
	AddField("MinStartDelay", BLK_TYPEINFO_FLOAT, PlaySoundComponent, m_MinStartDelay, false, "")
	AddField("MaxStartDelay", BLK_TYPEINFO_FLOAT, PlaySoundComponent, m_MaxStartDelay, false, "")
	AddField("SoundData", BLK_TYPEINFO_STRUCT, PlaySoundComponent, m_SoundData, true, "SoundData")
)

GenerateClass(
	DebugSphereCollision,
	AddField("CollisionModel", BLK_TYPEINFO_STATICMODEL, DebugSphereCollision, m_pCollisionModel, false, "")
)

GenerateClass(
	LevelComponent,
	AddField("LevelType", BLK_TYPEINFO_ENUM, LevelComponent, m_LevelType, false, "ELevelType")
	AddField("GlobalModelScale", BLK_TYPEINFO_FLOAT, LevelComponent, m_GlobalModelScale, false, "")
	AddField("EditorIconScale", BLK_TYPEINFO_FLOAT, LevelComponent, m_EditorIconScale, false, "")
	AddField("GlobalVolumeScale", BLK_TYPEINFO_FLOAT, LevelComponent, m_GlobalVolumeScale, false, "")
)

GenerateClass(
	ShaderModifierComponent,
	AddField("ShaderVectorEvents", BLK_TYPEINFO_STRUCT, ShaderModifierComponent, m_ShaderVectorEvents, true, "VectorAnimEvent")
)

GenerateClass(
	DeleteEntityComponent,
	AddField("Dummy", BLK_TYPEINFO_FLOAT, DeleteEntityComponent, m_Dummy, false, "")
)

GenerateClass(
	UIComponent,
	AddField("AuthoredWidth", BLK_TYPEINFO_INT, UIComponent, m_AuthoredWidth, false, "")
	AddField("AuthoredHeight", BLK_TYPEINFO_INT, UIComponent, m_AuthoredHeight, false, "")
	AddField("NormalizedAnchorPoint", BLK_TYPEINFO_VECTOR, UIComponent, m_NormalizedAnchorPt, false, "")
	AddField("UIToScreenSizeRatio", BLK_TYPEINFO_VECTOR, UIComponent, m_UIToScreenSizeRatio, false, "")
)

GenerateEnum(
	eWidgetAnchor, "eWidgetAnchor",
	AddEnumField(TopLeft, "TopLeft")
	AddEnumField(MiddleLeft, "MiddleLeft")
	AddEnumField(BottomLeft, "BottomLeft")
	AddEnumField(TopCenter, "TopCenter")
	AddEnumField(MiddleCenter, "MiddleCenter")
	AddEnumField(BottomCenter, "BottomCenter")
	AddEnumField(TopRight, "TopRight")
	AddEnumField(MiddleRight, "MiddleRight")
	AddEnumField(BottomRight, "BottomRight")
)

GenerateEnum(
	eWidgetAxisLock, "eWidgetAxisLock",
	AddEnumField(LockAll, "LockAll")
	AddEnumField(LockXAxis, "LockXAxis")
	AddEnumField(LockYAxis, "LockYAxis")
)

GenerateClass(
	UIWidgetComponent,
	AddField("Anchor", BLK_TYPEINFO_ENUM, UIWidgetComponent, m_Anchor, false, "eWidgetAnchor")
	AddField("AxisLock", BLK_TYPEINFO_ENUM, UIWidgetComponent, m_AxisLock, false, "eWidgetAxisLock")
	AddField("RelativePosition", BLK_TYPEINFO_VECTOR, UIWidgetComponent, m_StartingPosition, false, "")
	AddField("RelativeSize", BLK_TYPEINFO_VECTOR, UIWidgetComponent, m_StartingSize, false, "")
	AddField("Materials", BLK_TYPEINFO_STRUCT, UIWidgetComponent, m_Materials, true, "MaterialComponent")
	AddField("ChildWidgets", BLK_TYPEINFO_STRUCT, UIWidgetComponent, m_ChildWidgets, true, "UIWidgetComponent")

)

GenerateClass(
	UISlider,
	AddField("SliderBoundsMin", BLK_TYPEINFO_VECTOR, UISlider, m_SliderBoundsMin, false, "")
	AddField("SliderBoundsMax", BLK_TYPEINFO_VECTOR, UISlider, m_SliderBoundsMax, false, "")
)

GenerateEnum(
	eCinematicActionType, "eCinematicActionType",
	AddEnumField(CineAction_Override, "Override")
	AddEnumField(CineAction_Animate, "Animate")
	AddEnumField(CineAction_MoveTo, "MoveTo")
)

GenerateClass(
	CinematicAction,
	AddField("ActionType", BLK_TYPEINFO_ENUM, CinematicAction, m_CineActionType, false, "eCinematicActionType")
	AddField("ActionStartTime", BLK_TYPEINFO_VECTOR4, CinematicAction, m_ActionStartTime, false, "")
	AddField("ActionDuration", BLK_TYPEINFO_VECTOR4, CinematicAction, m_ActionDuration, false, "")
	AddField("StringParam", BLK_TYPEINFO_STRING, CinematicAction, m_sCineParam, false, "")
	AddField("FloatParam", BLK_TYPEINFO_FLOAT, CinematicAction, m_fCineParam, false, "")
	AddField("EntityParam", BLK_TYPEINFO_GAMEENTITY, CinematicAction, m_pCineParam, false, "")
	AddField("VectorParam", BLK_TYPEINFO_VECTOR4, CinematicAction, m_vCineParam, false, "")
)

GenerateClass(
	CinematicComponent,
	AddField("Actions", BLK_TYPEINFO_STRUCT, CinematicComponent, m_Actions, true, "CinematicAction")
)


GenerateEnum(
	EBreakableBehavior,
	"EBreakableBehavior",
	AddEnumField(PushFromImpactPoint, "PushFromImpactPoint")
	AddEnumField(UserVelocity, "UserVelocity")
)

GenerateClass(
	AnimationComponent,
	AddField("AnimationName", BLK_TYPEINFO_STRING, AnimationComponent, m_animation_name, false, "")
	AddField("Animation", BLK_TYPEINFO_ANIMATION, AnimationComponent, m_animation, false, "")
	AddField("TimeScale", BLK_TYPEINFO_FLOAT, AnimationComponent, m_time_scale, false, "")
	AddField("IsLooping", BLK_TYPEINFO_BOOL, AnimationComponent, m_is_looping, false, "")
	AddField("AnimationEvent", BLK_TYPEINFO_STRUCT, AnimationComponent, m_anim_events, true, "AnimEvent")
)

GenerateClass(
	BreakableComponent,
	AddField("DestructibleBehavior", BLK_TYPEINFO_ENUM, BreakableComponent, m_destructible_type, false, "EBreakableBehavior")
	AddField("MaxLifeTime", BLK_TYPEINFO_FLOAT, BreakableComponent, m_life_duration, false, "")
	AddField("Gravity", BLK_TYPEINFO_VECTOR, BreakableComponent, m_gravity, false, "")
	AddField("MinLinearVelocity", BLK_TYPEINFO_VECTOR, BreakableComponent, m_min_linear_vel, false, "")
	AddField("MaxLinearVelocity", BLK_TYPEINFO_VECTOR, BreakableComponent, m_max_linear_vel, false, "")
	AddField("MinAngularVelocity", BLK_TYPEINFO_FLOAT, BreakableComponent, m_min_angular_vel, false, "")
	AddField("MaxAngularVelocity", BLK_TYPEINFO_FLOAT, BreakableComponent, m_max_angular_vel, false, "")
	AddField("Health", BLK_TYPEINFO_FLOAT, BreakableComponent, m_starting_health, false, "")
	AddField("ResetSim", BLK_TYPEINFO_BOOL, BreakableComponent, m_bDebugResetSim, false, "")
	AddField("DestructionFX", BLK_TYPEINFO_GAMEENTITY, BreakableComponent, m_complete_destruction_fx, false, "")
	AddField("DestructionFXLocalOffset", BLK_TYPEINFO_VECTOR, BreakableComponent, m_fx_local_offset, false, "")
)


GenerateClass(
GaussianSplatComponent,
	AddField("Model", BLK_TYPEINFO_STATICMODEL, GaussianSplatComponent, m_model, false, "")
	AddField("SplatFalloff", BLK_TYPEINFO_FLOAT, GaussianSplatComponent, m_splat_falloff, false, "")
	AddField("SplatScale", BLK_TYPEINFO_FLOAT, GaussianSplatComponent, m_splat_scale, false, "")
	AddField("MaxShDegree", BLK_TYPEINFO_INT, GaussianSplatComponent, m_max_sh_degree, false, "")
	AddField("GpuSort", BLK_TYPEINFO_BOOL, GaussianSplatComponent, m_gpu_sort, false, "")
	AddField("Contrast", BLK_TYPEINFO_FLOAT, GaussianSplatComponent, m_contrast, false, "")
)
