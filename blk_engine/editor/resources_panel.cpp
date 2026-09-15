/// resources_panel.cpp
///
/// 2026 blk

#include "blk_containers.h"
#include "resources_panel.h"
#include "editor.h"
#include "editor_entity.h"
#include "imgui.h"

ResourcesPanel* g_pResourcesPanel = nullptr;

namespace {

/// AbsolutePath_Recursive
///
/// Rebuilds target's "/"-joined folder path so dirty flags survive a tree rebuild by path match.
	std::string AbsolutePath_Recursive(const ResourceEntry_t* const cur, const ResourceEntry_t* const target) {
		if (cur == target) {
			return cur->m_FolderName;
		}
		for (const ResourceEntry_t& sub : cur->m_SubFolders) {
			const std::string found = AbsolutePath_Recursive(&sub, target);
			if (!found.empty()) {
				return cur->m_FolderName + "/" + found;
			}
		}
		for (const ResourceEntry_t& res : cur->m_Resources) {
			const std::string found = AbsolutePath_Recursive(&res, target);
			if (!found.empty()) {
				return cur->m_FolderName + "/" + found;
			}
		}
		return {};
	}

/// AbsolutePath
	std::string AbsolutePath(const std::vector<ResourceEntry_t>& roots, const ResourceEntry_t* const target) {
		for (const ResourceEntry_t& root : roots) {
			const std::string found = AbsolutePath_Recursive(&root, target);
			if (!found.empty()) {
				return found;
			}
		}
		return {};
	}

/// CollectDirtyPaths_Recursive
	void CollectDirtyPaths_Recursive(const std::vector<ResourceEntry_t>& roots, const ResourceEntry_t& entry, std::vector<std::string>& out_paths) {
		if (entry.m_bIsDirty) {
			out_paths.push_back(AbsolutePath(roots, &entry));
		}
		for (const ResourceEntry_t& sub : entry.m_SubFolders) {
			CollectDirtyPaths_Recursive(roots, sub, out_paths);
		}
		for (const ResourceEntry_t& res : entry.m_Resources) {
			CollectDirtyPaths_Recursive(roots, res, out_paths);
		}
	}

/// ReapplyDirtyPaths_Recursive
	void ReapplyDirtyPaths_Recursive(const std::vector<ResourceEntry_t>& roots, ResourceEntry_t& entry, const std::vector<std::string>& dirty_paths) {
		if (blk::std_contains(dirty_paths, AbsolutePath(roots, &entry))) {
			entry.m_bIsDirty = true;
		}
		for (ResourceEntry_t& sub : entry.m_SubFolders) {
			ReapplyDirtyPaths_Recursive(roots, sub, dirty_paths);
		}
		for (ResourceEntry_t& res : entry.m_Resources) {
			ReapplyDirtyPaths_Recursive(roots, res, dirty_paths);
		}
	}

/// ClearDirtyFlags_Recursive
	void ClearDirtyFlags_Recursive(ResourceEntry_t& entry) {
		entry.m_bIsDirty = false;
		for (ResourceEntry_t& sub : entry.m_SubFolders) {
			ClearDirtyFlags_Recursive(sub);
		}
		for (ResourceEntry_t& res : entry.m_Resources) {
			ClearDirtyFlags_Recursive(res);
		}
	}

/// FindEntryByPrefabPtr_Recursive
///
/// Finds the node whose m_pPrefab is target and writes its nearest .blkpkg ancestor to out_owning_package (nullptr if none).
	ResourceEntry_t* FindEntryByPrefabPtr_Recursive(std::vector<ResourceEntry_t>& nodes, const Prefab* const target, ResourceEntry_t* const current_package, ResourceEntry_t** const out_owning_package) {
		for (ResourceEntry_t& entry : nodes) {
			ResourceEntry_t* const package_for_children = blk::is_package_extension(GetFileExtension(entry.m_FolderName)) ? &entry : current_package;

			if (entry.m_pPrefab == target) {
				*out_owning_package = package_for_children;
				return &entry;
			}
			if (ResourceEntry_t* const found = FindEntryByPrefabPtr_Recursive(entry.m_SubFolders, target, package_for_children, out_owning_package)) {
				return found;
			}
			if (ResourceEntry_t* const found = FindEntryByPrefabPtr_Recursive(entry.m_Resources, target, package_for_children, out_owning_package)) {
				return found;
			}
		}
		return nullptr;
	}

/// FindEntryByPrefabEntity_Recursive
///
/// Same as FindEntryByPrefabPtr_Recursive(), but matches the prefab's GameEntity(0), which is all EventCB() receives.
	ResourceEntry_t* FindEntryByPrefabEntity_Recursive(std::vector<ResourceEntry_t>& nodes, const GameEntity* const target_entity, ResourceEntry_t* const current_package, ResourceEntry_t** const out_owning_package) {
		for (ResourceEntry_t& entry : nodes) {
			ResourceEntry_t* const package_for_children = blk::is_package_extension(GetFileExtension(entry.m_FolderName)) ? &entry : current_package;

			if (entry.m_pPrefab && entry.m_pPrefab->GetGameEntity(0) == target_entity) {
				*out_owning_package = package_for_children;
				return &entry;
			}
			if (ResourceEntry_t* const found = FindEntryByPrefabEntity_Recursive(entry.m_SubFolders, target_entity, package_for_children, out_owning_package)) {
				return found;
			}
			if (ResourceEntry_t* const found = FindEntryByPrefabEntity_Recursive(entry.m_Resources, target_entity, package_for_children, out_owning_package)) {
				return found;
			}
		}
		return nullptr;
	}

} // anonymous namespace

/// ResourcesPanel::ResourcesPanel
ResourcesPanel::ResourcesPanel() {
	g_Editor->RegisterEvent(this, WidgetCB_PrefabModified);
	g_pResourcesPanel = this;
	g_ResourceManager.register_cb(ResourceManagerCB, ResourceManager::CBR_FileModified);
}

/// ResourcesPanel::~ResourcesPanel
ResourcesPanel::~ResourcesPanel() {
	g_ResourceManager.unregister_cb(ResourceManagerCB);
}

/// ResourcesPanel::EventCB
///
/// Marks the prefab whose GameEntity(0) was edited dirty. PropertiesPanel broadcasts exactly that entity, so no selection fallback is needed.
void ResourcesPanel::EventCB(const widgetCBObject* const widget_cb_object) {
	if (widget_cb_object->widgetType != WidgetCB_PrefabModified) {
		return;
	}

	GameEntity* const modified_entity = (GameEntity*)static_cast<const widgetCBGeneric*>(widget_cb_object)->m_Value;
	if (!modified_entity) {
		return;
	}

	ResourceEntry_t* owning_package = nullptr;
	ResourceEntry_t* const entry = FindEntryByPrefabEntity_Recursive(m_ResourceTree, modified_entity, nullptr, &owning_package);
	if (!entry) {
		return;
	}

	entry->m_bIsDirty = true;
	if (owning_package) {
		owning_package->m_bIsDirty = true;
	}
}

/// ResourcesPanel::PostRendererInit
void ResourcesPanel::PostRendererInit() {
	RebuildResourceTree();
}

/// ResourcesPanel::GetSelectedPrefab
Prefab* ResourcesPanel::GetSelectedPrefab() const {
	return m_pSelectedEntry ? m_pSelectedEntry->m_pPrefab : nullptr;
}

/// ResourcesPanel::GetSelectedGameEntity
GameEntityPtr ResourcesPanel::GetSelectedGameEntity() const {
	GameEntityPtr ret;
	if (m_pPickedEntity) {
		ret.SetEntity(m_pPickedEntity->GetGameEntity());
	}
	return ret;
}

/// ResourcesPanel::AddPrefab
void ResourcesPanel::AddPrefab(Prefab* const prefab, const std::string& package_name, const std::string& folder_name, const std::string& prefab_name) {
	if (m_ResourceTree.empty()) {
		return;
	}
	ResourceEntry_t& packages_root = m_ResourceTree[0];

	for (ResourceEntry_t& package : packages_root.m_SubFolders) {
		if (package.m_FolderName != package_name) {
			continue;
		}

		for (ResourceEntry_t& folder : package.m_SubFolders) {
			if (folder.m_FolderName != folder_name) {
				continue;
			}

			for (ResourceEntry_t& existing_prefab : folder.m_Resources) {
				if (existing_prefab.m_pPrefab && existing_prefab.m_pPrefab->GetPrefabName() == prefab_name) {
					existing_prefab.m_FolderName = prefab->GetPrefabName();
					existing_prefab.m_pPrefab = prefab;
					return;
				}
			}

			ResourceEntry_t new_prefab;
			new_prefab.m_FolderName = prefab->GetPrefabName();
			new_prefab.m_pPrefab = prefab;
			new_prefab.m_bIsDirty = true;
			folder.m_Resources.push_back(new_prefab);
			package.m_bIsDirty = true;
			return;
		}

		ResourceEntry_t new_folder;
		new_folder.m_FolderName = folder_name;

		ResourceEntry_t new_prefab;
		new_prefab.m_FolderName = prefab->GetPrefabName();
		new_prefab.m_pPrefab = prefab;
		new_folder.m_Resources.push_back(new_prefab);

		package.m_SubFolders.push_back(new_folder);
		return;
	}

	ResourceEntry_t new_package;
	new_package.m_FolderName = package_name;

	ResourceEntry_t new_folder;
	new_folder.m_FolderName = folder_name;

	ResourceEntry_t new_prefab;
	new_prefab.m_FolderName = prefab->GetPrefabName();
	new_prefab.m_pPrefab = prefab;
	new_folder.m_Resources.push_back(new_prefab);

	new_package.m_SubFolders.push_back(new_folder);
	packages_root.m_SubFolders.push_back(new_package);
}

/// ResourcesPanel::MarkPrefabDirty
void ResourcesPanel::MarkPrefabDirty(Prefab* const prefab) {
	ResourceEntry_t* owning_package = nullptr;
	ResourceEntry_t* const entry = FindEntryByPrefabPtr_Recursive(m_ResourceTree, prefab, nullptr, &owning_package);
	if (!entry) {
		return;
	}

	entry->m_bIsDirty = true;
	if (owning_package) {
		owning_package->m_bIsDirty = true;
	}
}

/// ResourcesPanel::RebuildResourceTree
void ResourcesPanel::RebuildResourceTree() {
	std::vector<std::string> dirty_paths;
	for (const ResourceEntry_t& root : m_ResourceTree) {
		CollectDirtyPaths_Recursive(m_ResourceTree, root, dirty_paths);
	}

	m_ResourceTree.clear();
	m_pSelectedEntry = nullptr;

	m_ResourceTree.push_back(ResourceEntry_t());
	m_ResourceTree.back().m_FolderName = "Game Packages";

	m_ResourceTree.push_back(ResourceEntry_t());
	m_ResourceTree.back().m_FolderName = "Game Resources";
	FindResourcesRecursively("./assets/", m_ResourceTree.back());

	m_ResourceTree.push_back(ResourceEntry_t());
	m_ResourceTree.back().m_FolderName = "Engine Resources";
	FindResourcesRecursively("../blk_engine/assets/", m_ResourceTree.back());

	for (ResourceEntry_t& root : m_ResourceTree) {
		ReapplyDirtyPaths_Recursive(m_ResourceTree, root, dirty_paths);
	}
}

/// ResourcesPanel::FindResourcesRecursively
void ResourcesPanel::FindResourcesRecursively(const std::string& file, ResourceEntry_t& current_folder) {
	const size_t current_folder_start_pos = file.find_last_of("/", file.length() - 2);
	const size_t current_folder_end_pos = file.find_first_of("/", current_folder_start_pos + 1);

	const std::string current_folder_name = file.substr(current_folder_start_pos, current_folder_end_pos);
	if (current_folder_name == "/CVS/") {
		return;
	}

	current_folder.m_SubFolders.push_back(ResourceEntry_t());
	ResourceEntry_t& new_folder = current_folder.m_SubFolders.back();
	new_folder.m_FolderName = current_folder_name;

	WIN32_FIND_DATAA find_file_data = {};

	static char full_file_pattern[MAX_PATH];
	sprintf_s(full_file_pattern, "%s*", file.c_str());

	const HANDLE find_handle = FindFirstFileA(full_file_pattern, &find_file_data);

	if (find_handle == INVALID_HANDLE_VALUE) {
		return;
	}

	do {
		if (find_file_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			if (strcmp(find_file_data.cFileName, ".") == 0 || strcmp(find_file_data.cFileName, "..") == 0) {
				continue;
			}

			const std::string new_folder_path = file + find_file_data.cFileName + "/";
			FindResourcesRecursively(new_folder_path, new_folder);
			continue;
		}

		const char* const ext = strrchr(find_file_data.cFileName, '.');
		if (!ext) {
			continue;
		}

		const char* const valid_extensions[] = { ".fbx", ".dds", ".png", ".ms3d", ".ply", ".blkmat", ".kbMat", ".blkshader", ".kbShader", ".jpg", ".tga", ".bmp", ".blkanim", ".kbAnim", ".wav", ".diablo3", ".tif", ".blkpkg", ".kbPkg" };
		const int num_extensions = sizeof(valid_extensions) / sizeof(valid_extensions[0]);

		for (int i = 0; i < num_extensions; i++) {
			if (strcmp(ext, valid_extensions[i]) != 0) {
				continue;
			}

			if (blk::is_package_extension(ext + 1)) {
				Package* const package = g_ResourceManager.get_package(file + find_file_data.cFileName, false);
				blk::error_check(package, "ResourcesPanel::FindResourcesRecursively() - Failed to load package");

				m_ResourceTree[0].m_SubFolders.push_back(ResourceEntry_t());
				ResourceEntry_t& new_package_entry = m_ResourceTree[0].m_SubFolders.back();
				new_package_entry.m_FolderName = find_file_data.cFileName;

				for (int folder_idx = 0; folder_idx < (int)package->NumFolders(); folder_idx++) {
					new_package_entry.m_SubFolders.push_back(ResourceEntry_t());
					ResourceEntry_t& new_folder_entry = new_package_entry.m_SubFolders.back();
					new_folder_entry.m_FolderName = package->GetFolderName(folder_idx);

					const std::vector<Prefab*>& prefabs_in_folder = package->GetPrefabsForFolder(folder_idx);
					for (size_t prefab_idx = 0; prefab_idx < prefabs_in_folder.size(); prefab_idx++) {
						new_folder_entry.m_Resources.push_back(ResourceEntry_t());
						ResourceEntry_t& new_prefab_entry = new_folder_entry.m_Resources.back();
						new_prefab_entry.m_pPrefab = prefabs_in_folder[prefab_idx];
						new_prefab_entry.m_FolderName = prefabs_in_folder[prefab_idx]->GetPrefabName();
					}
				}
			} else {
				new_folder.m_Resources.push_back(ResourceEntry_t());
				ResourceEntry_t& new_resource_entry = new_folder.m_Resources.back();

				std::string file_name = file + find_file_data.cFileName;
				StringToLower(file_name);

				new_resource_entry.m_pResource = g_ResourceManager.resource(file_name, false, true);
				if (new_resource_entry.m_pResource) {
					// Shows just the file name, since Resource::name() is the full '\'-separated path.
					const std::string& resource_name = new_resource_entry.m_pResource->name();
					const size_t file_name_start = resource_name.find_last_of('\\');
					new_resource_entry.m_FolderName = (file_name_start == std::string::npos) ? resource_name : resource_name.substr(file_name_start + 1);
				}
			}
			break;
		}
	} while (FindNextFileA(find_handle, &find_file_data));

	FindClose(find_handle);
}

/// ResourcesPanel::draw_imgui
void ResourcesPanel::draw_imgui() {
	ImGui::SetNextWindowPos(ImVec2(20, 440), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(260, 360), ImGuiCond_FirstUseEver);
	ImGui::Begin("Resources");

	if (ImGui::BeginTabBar("ResourcesTabBar")) {
		if (ImGui::BeginTabItem("Resources")) {
			DrawResourcesTree();
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Entities")) {
			DrawEntitiesList();
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}

	ImGui::End();
}

/// ResourcesPanel::DrawResourcesTree
void ResourcesPanel::DrawResourcesTree() {
	for (ResourceEntry_t& root : m_ResourceTree) {
		DrawResourceEntry(root, nullptr);
	}
}

/// ResourcesPanel::DrawResourceEntry
///
/// Draws folders as TreeNodes, whose open state ImGui persists by ID, and leaves as Selectables
/// that broadcast WidgetCB_ResourceSelected or WidgetCB_PrefabSelected.
void ResourcesPanel::DrawResourceEntry(ResourceEntry_t& entry, ResourceEntry_t* const owning_package) {
	const bool is_folder = (!entry.m_pPrefab && !entry.m_pResource);
	if (is_folder && entry.m_SubFolders.empty() && entry.m_Resources.empty()) {
		return;
	}

	ResourceEntry_t* const package_for_children = blk::is_package_extension(GetFileExtension(entry.m_FolderName)) ? &entry : owning_package;

	std::string label = entry.m_FolderName;
	if (entry.m_bIsDirty) {
		label += " *";
	}

	ImGui::PushID(&entry);

	if (is_folder) {
		const bool open = ImGui::TreeNode(label.c_str());
		if (ImGui::BeginPopupContextItem()) {
			DrawResourceContextMenu(package_for_children);
			ImGui::EndPopup();
		}
		if (open) {
			for (ResourceEntry_t& sub : entry.m_SubFolders) {
				DrawResourceEntry(sub, package_for_children);
			}
			for (ResourceEntry_t& res : entry.m_Resources) {
				DrawResourceEntry(res, package_for_children);
			}
			ImGui::TreePop();
		}
	} else {
		const bool is_selected = (m_pSelectedEntry == &entry);
		if (ImGui::Selectable(label.c_str(), is_selected)) {
			m_pSelectedEntry = &entry;

			if (entry.m_pResource) {
				widgetCBResourceSelected cb(WidgetCB_ResourceSelected);
				cb.resourceFileName = entry.m_pResource->full_file_name();
				g_Editor->BroadcastEvent(cb);
			} else if (entry.m_pPrefab) {
				widgetCBResourceSelected cb(WidgetCB_PrefabSelected);
				g_Editor->BroadcastEvent(cb);
			}
		}
		if (ImGui::BeginPopupContextItem()) {
			DrawResourceContextMenu(package_for_children);
			ImGui::EndPopup();
		}
	}

	ImGui::PopID();
}

/// ResourcesPanel::DrawResourceContextMenu
void ResourcesPanel::DrawResourceContextMenu(ResourceEntry_t* const owning_package) {
	const std::string save_label = owning_package ? ("Save Package " + owning_package->m_FolderName) : std::string("Save Package");
	if (ImGui::MenuItem(save_label.c_str(), nullptr, false, owning_package && owning_package->m_bIsDirty)) {
		SavePackage(owning_package);
	}
	if (ImGui::MenuItem("Save All Changed Packages")) {
		SaveAllChangedPackages();
	}
}

/// ResourcesPanel::DrawEntitiesList
///
/// Keeps m_pPickedEntity separate from the live selection because it feeds the GAMEENTITY field's Pick button
/// through GetSelectedGameEntity(). Left-click still updates the live selection too.
void ResourcesPanel::DrawEntitiesList() {
	const std::vector<EditorEntity*>& entities = g_Editor->GetGameEntities();

	for (EditorEntity* const entity : entities) {
		if (entity->IsHidden()) {
			continue;
		}

		ImGui::PushID(entity);

		const bool is_picked = (m_pPickedEntity == entity);
		const char* const entity_name = entity->GetGameEntity()->name().c_str();
		if (ImGui::Selectable(entity_name, is_picked)) {
			m_pPickedEntity = entity;
			std::vector<EditorEntity*> pick{ entity };
			g_Editor->SelectEntities(pick, false);
		}

		if (ImGui::BeginPopupContextItem()) {
			const std::string zoom_label = "Zoom to entity " + entity->GetGameEntity()->name().stl_str();
			if (ImGui::MenuItem(zoom_label.c_str())) {
				ZoomToEntity(entity);
			}

			const std::string delete_label = "Delete entity " + entity->GetGameEntity()->name().stl_str();
			if (ImGui::MenuItem(delete_label.c_str())) {
				if (MessageBoxA(nullptr, "Really delete this entity?", "Delete Entity", MB_YESNO | MB_ICONQUESTION) == IDYES) {
					std::vector<EditorEntity*> to_delete{ entity };
					g_Editor->DeleteEntities(to_delete);
					if (m_pPickedEntity == entity) {
						m_pPickedEntity = nullptr;
					}
				}
			}
			ImGui::EndPopup();
		}

		ImGui::PopID();
	}
}

/// ResourcesPanel::ZoomToEntity
void ResourcesPanel::ZoomToEntity(EditorEntity* const entity) {
	const float zoom_dist = 75.0f;

	const Vec3 cam_pos = g_Editor->GetMainCameraPos();
	Vec3 vec_to = (cam_pos - entity->position());
	vec_to.y = 0.0f;
	if (vec_to.length() < zoom_dist) {
		return;
	}

	const Vec3 final_pos = entity->position() + vec_to.normalize_safe() * zoom_dist;
	g_Editor->SetMainCameraPos(final_pos);

	Mat4 new_rot = Mat4::look_at(final_pos, entity->position(), Vec3::up);
	new_rot.inverse_fast();
	g_Editor->SetMainCameraRot(Quat4::from_mat4(new_rot));
}

/// ResourcesPanel::SavePackage
void ResourcesPanel::SavePackage(ResourceEntry_t* const package_entry) {
	if (!package_entry) {
		return;
	}
	g_ResourceManager.save_package(package_entry->m_FolderName);
	ClearDirtyFlags_Recursive(*package_entry);
}

/// ResourcesPanel::SaveAllChangedPackages
void ResourcesPanel::SaveAllChangedPackages() {
	if (m_ResourceTree.empty()) {
		return;
	}
	for (ResourceEntry_t& package : m_ResourceTree[0].m_SubFolders) {
		if (package.m_bIsDirty) {
			SavePackage(&package);
		}
	}
}

/// ResourcesPanel::ResourceManagerCB
void ResourcesPanel::ResourceManagerCB(const ResourceManager::CallbackReason reason) {
	g_pResourcesPanel->RebuildResourceTree();
}
