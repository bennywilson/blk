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

#include "resource_manager.h"

/// ResourceEntry_t
///
/// One node in the Resources tree: a folder, a loaded Resource, or a
/// kbPrefab. Ported from ResourceTab's (FLTK) ResourceTabFile_t -- same
/// tri-state shape, no FLTK dependency of its own.
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
/// Phase 3, Milestone 5: ImGui replacement for ResourceTab (FLTK). Browses
/// on-disk resource packages/folders/prefabs, and is the source of
/// "currently selected prefab/resource/entity" for the rest of the editor
/// (PropertiesPanel, kbEditor's prefab workflows, kbPropertiesTab).
///
/// The Entities section reads g_Editor->GetGameEntities() live every frame,
/// following OutlinerPanel's precedent -- no cached entity list to keep in
/// sync. The Resources tree IS real cached state (it mirrors disk, built
/// once via PostRendererInit() and mutated in place by AddPrefab/
/// MarkPrefabDirty) since rescanning disk every frame isn't viable; ImGui's
/// own ID-based TreeNode open/closed state replaces the manual expanded-flag
/// bookkeeping the FLTK version needed to survive a full tree rebuild -- only
/// the dirty flag (real "unsaved changes" state, not UI state) still needs to
/// be carried across a rebuild.
class ResourcesPanel : public EditorPanel {
public:
	ResourcesPanel(const int x, const int y, const int w, const int h);
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
