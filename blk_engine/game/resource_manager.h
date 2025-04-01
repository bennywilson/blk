/// ResourceManager.h
///
/// 2016-2025 blk 1.0

#pragma once

/// Resource
class Resource {
	friend class ResourceManager;

public:
	Resource() { m_last_load_time = -1.0f, m_is_loaded = false; }
	virtual	~Resource() = 0 { }

	virtual kbTypeInfoType_t type() const = 0;

	float last_load_time() const { return m_last_load_time; }

	void load();
	void release();		// note: It's preferable to use SAFE_RELEASE( ResourceInstance ) instead of calling this directly

	const std::string& name() const { return m_name; }
	const std::string& full_file_name() const { return m_full_file_name; }	// todo: deprecate
	const kbString& full_name() const { return m_full_name; }

protected:
	// todo: make pure virtual
	virtual bool load_internal() { blk::warn("Make pure virtual"); return false; }
	virtual void release_internal() { blk::warn("Make pure virtual"); }

	std::string	m_name;
	std::string	m_full_file_name;
	kbString m_full_name;

	float m_last_load_time;

	bool m_is_loaded;
};

/// kbPackage
class kbPackage {
	friend class ResourceManager;
	friend class kbFile;

public:
	const size_t NumFolders() const { return m_Folders.size(); }
	const std::string& GetFolderName(const int idx) const { return m_Folders[idx].m_FolderName; }
	const std::vector< class kbPrefab* >& GetPrefabsForFolder(const int idx) const { return m_Folders[idx].m_pPrefabs; }
	const std::string& GetPackageName() const { return m_PackageName; }
	const kbPrefab* GetPrefab(const std::string& PrefabName) const;

private:
	kbPackage() { }
	~kbPackage();

	std::string	m_PackageName;

	struct kbFolder {
		std::string	m_FolderName;
		std::vector<class kbPrefab*> m_pPrefabs;
	};
	std::vector<kbFolder> m_Folders;
};

/// ResourceManager
class ResourceManager {
public:
	ResourceManager();
	~ResourceManager();

	void render_sync();

	Resource* resource(const std::string& fullFileName, const bool bLoadImmediately, const bool bLoadIfNotFound);

	Resource* async_load(const kbString& stringName);

	bool add_prefab(class GameEntity* pEntity, const std::string& package, const std::string& folder, const std::string& file, const bool bOverwrite, kbPrefab** prefab = NULL);
	void update_prefab(const kbPrefab* const pPrefab, std::vector<GameEntity*>& pEntityList);

	kbPackage* get_package(const std::string& FullPackageName, const bool bLoadImmediately = true);
	void save_package(const std::string& PackageName);
	const GameEntity* game_entity(const kbGUID& GUID);

	const std::vector<kbPackage*>& package_list() const { return m_package_list; }

	void dump_package_info();

	void shut_down();

	enum CallbackReason {
		CBR_None = 0,
		CBR_FileModified,
		CBR_Max_Num_Reasons
	};
	typedef void (*ResourceManagerCB)(const CallbackReason reason);
	void register_cb(ResourceManagerCB func_cb, const CallbackReason reason);
	void unregister_cb(ResourceManagerCB func_cnb);

private:
	void update_hot_reloads();

	void file_modified_cb(const std::wstring& fileName);

	std::unordered_map<kbString, Resource*, kbStringHash>	m_name_to_resource;
	std::vector<Resource*> m_resources_to_load;

	std::vector<kbPackage*>	m_package_list;
	std::map<kbGUID, const GameEntity*> m_guid_to_entity;

	std::vector<class kbLoadResourceJob*> m_load_resource_jobs;

	// Hot reloading
	HANDLE m_hGameAssetDirectory;
	HANDLE m_hEngineAssetDirectory;
	OVERLAPPED m_Ovl[2];

	struct CallbackInfo {
		CallbackInfo(ResourceManagerCB inFunc, const CallbackReason reason) : m_pFunc(inFunc), m_CBReason(reason) { }

		ResourceManagerCB m_pFunc;
		CallbackReason m_CBReason;

		bool operator==(const CallbackInfo& rhs) const { return m_pFunc == rhs.m_pFunc && m_CBReason == rhs.m_CBReason; }
	};
	std::vector<CallbackInfo> m_FunctionCallbacks;
};

extern ResourceManager g_ResourceManager;
