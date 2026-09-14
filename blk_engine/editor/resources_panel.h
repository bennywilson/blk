/// resources_panel.h
///
/// 2026 blk

#pragma once

#include "editor_panel.h"
#include "entity_header.h"

class kbPrefab;
class Resource;
class kbEditorEntity;
class GameEntity;

/// ResourceEntry_t
///
/// One Resources-tree node: a folder, a loaded Resource, or a kbPrefab.
struct ResourceEntry_t {
	std::string m_FolderName;
	kbPrefab* m_pPrefab = nullptr;
	Resource* m_pResource = nullptr;
	bool m_bIsDirty = false;

	std::vector<ResourceEntry_t> m_SubFolders;
	std::vector<ResourceEntry_t> m_Resources;
};

/// ResourcesPanel
///
/// Browses on-disk packages, folders, and prefabs, and supplies the selected prefab, resource, and entity to the rest of the editor.
/// Caches the tree because rescanning disk per frame isn't viable. Only m_bIsDirty survives a rebuild, since ImGui keeps TreeNode open state by ID.
class ResourcesPanel : public EditorPanel {
public:
	ResourcesPanel();
	~ResourcesPanel();

	virtual void draw_imgui() override;
	virtual void EventCB(const widgetCBObject* const widget_cb_object) override;

	void PostRendererInit();

	kbPrefab* GetSelectedPrefab() const;
	GameEntityPtr GetSelectedGameEntity() const;

	void AddPrefab(kbPrefab* const prefab, const std::string& package_name, const std::string& folder_name, const std::string& prefab_name);
	void MarkPrefabDirty(kbPrefab* const prefab);

private:
	void RebuildResourceTree();
	void FindResourcesRecursively(const std::string& path, ResourceEntry_t& current_folder);

	void DrawResourcesTree();
	void DrawResourceEntry(ResourceEntry_t& entry, ResourceEntry_t* const owning_package);
	void DrawResourceContextMenu(ResourceEntry_t* const owning_package);
	void DrawEntitiesList();
	void ZoomToEntity(kbEditorEntity* const entity);

	void SavePackage(ResourceEntry_t* const package_entry);
	void SaveAllChangedPackages();

	static void ResourceManagerCB(const ResourceManager::CallbackReason reason);

	std::vector<ResourceEntry_t> m_ResourceTree;
	ResourceEntry_t* m_pSelectedEntry = nullptr;

	kbEditorEntity* m_pPickedEntity = nullptr;
};

extern ResourcesPanel* g_pResourcesPanel;
