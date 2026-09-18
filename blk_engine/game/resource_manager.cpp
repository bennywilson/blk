/// ResourceManager.cpp
///
/// 2016 blk

#include "blk_containers.h"
#include "file.h"
#include "sound_manager.h"
#include "entity_header.h"

ResourceManager g_ResourceManager;

namespace fs = std::filesystem;

/// LoadResourceJob
class LoadResourceJob : public Job {
public:
	LoadResourceJob() :
		m_Resource(nullptr) {}

	virtual void Run() {
		m_Resource->load();
	}

	Resource* m_Resource;
};

/// Resource::Reload
void Resource::load() {

	const float loadStartTime = g_GlobalTimer.TimeElapsedSeconds();

	if (m_is_loaded == false) {
		if (load_internal()) {
			m_is_loaded = true;
		}
	}

	m_last_load_time = g_GlobalTimer.TimeElapsedSeconds();
	const float curLoadTime = m_last_load_time - loadStartTime;
	static float totalLoadTime = 0.0f;
	totalLoadTime += curLoadTime;
	// blk::log( "It took %f seconds to load %s.  Total resource load time = %f", curLoadTime, full_file_name().c_str(), totalLoadTime );
}

/// Resource::Release
void Resource::release() {
	release_internal();
	m_is_loaded = false;
}

/// open_directory_watch
static HANDLE open_directory_watch(const char* const path) {
	return CreateFile(path,
		GENERIC_READ | FILE_LIST_DIRECTORY,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		nullptr,
		OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
		nullptr);
}

/// ResourceManager::ResourceManager
ResourceManager::ResourceManager() {
	m_hGameAssetDirectory = INVALID_HANDLE_VALUE;
	m_hEngineAssetDirectory = INVALID_HANDLE_VALUE;

	ZeroMemory(&m_Ovl, sizeof(m_Ovl));
	//	m_Ovl.hEvent = ::CreateEvent( nullptr, FALSE, FALSE, nullptr );
}

/// ResourceManager::~ResourceManager
ResourceManager::~ResourceManager() {
	shut_down();
}

/// ResourceManager::RenderSync
void ResourceManager::render_sync() {
	for (int i = 0; i < m_resources_to_load.size(); i++) {
		m_resources_to_load[i]->load();
	}
	m_resources_to_load.clear();

	update_hot_reloads();
}

/// ResourceManager::update_hot_reloads
void ResourceManager::update_hot_reloads() {

	static std::vector<std::wstring> queuedFiles;

	static float lastUpdateTimeSecs = 0;
	const float totalSeconds = g_GlobalTimer.TimeElapsedSeconds();
	if (totalSeconds < lastUpdateTimeSecs + 0.05f) {
		return;
	}
	lastUpdateTimeSecs = totalSeconds;

	// Opened on first use, not in the constructor: g_ResourceManager is a global, constructed before
	// initialize_engine() moves the working directory to the game directory these paths are relative to.
	static bool watches_opened = false;
	if (!watches_opened) {
		watches_opened = true;
		m_hGameAssetDirectory = open_directory_watch("./assets/");
		m_hEngineAssetDirectory = open_directory_watch("../blk_engine/assets/");
	}

	// Handle queued up modified files
	if (queuedFiles.size() > 0) {

		for (int i = 0; i < queuedFiles.size(); i++) {
			const std::wstring& fileName = queuedFiles[i];
			file_modified_cb(fileName);
		}

		for (int i = 0; i < m_FunctionCallbacks.size(); i++) {
			m_FunctionCallbacks[i].m_pFunc(CBR_FileModified);
		}
	}
	queuedFiles.clear();

	static int states[] = { 0, 0 };
	HANDLE handles[] = { m_hGameAssetDirectory, m_hEngineAssetDirectory };
	static byte* buffers[2] = { new byte[2048], new byte[2048] };
	DWORD numBytes = 0;

	for (int i = 0; i < 2; i++) {
		if (states[i] == 0) {
			BOOL result = ReadDirectoryChangesW(handles[i],
				buffers[i],
				2048,
				TRUE,
				FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME,
				&numBytes,
				&m_Ovl[i],
				nullptr);

			if (result == false) {
				continue;
			}

			states[i] = 1;
		}

		FILE_NOTIFY_INFORMATION* pCurInfo = (FILE_NOTIFY_INFORMATION*)buffers[i];
		byte* pByteCurInfo = buffers[i];

		if (GetOverlappedResult(handles[i], &m_Ovl[i], &numBytes, FALSE)) {
			while (pCurInfo->Action != 0) {
				states[i] = 0;

				const DWORD FileNameLength = (pCurInfo->FileNameLength) / 2;
				std::wstring fileName;

				fileName.resize(FileNameLength);
				bool bHasTilda = false;
				for (DWORD i = 0; i < FileNameLength; i++) {

					fileName[i] = pCurInfo->FileName[i];
					if (fileName[i] == '~') {
						bHasTilda = true;
						break;
					}
				}

				if (bHasTilda == false && GetFileExtension(fileName).empty() == false) {
					std::replace(fileName.begin(), fileName.end(), '/', '\\');
					std::wstring fullFileName;
					if (i == 0) {
						fullFileName = L".\\assets\\" + fileName;
					} else {
						fullFileName = L"..\\blk_engine\\assets\\" + fileName;
					}

					if (blk::std_contains(queuedFiles, fullFileName) == false) {
						queuedFiles.push_back(fullFileName);
					} else {
						static int breakhere = 0;
						breakhere++;
					}
				}

				pCurInfo->Action = 0;
				pByteCurInfo += pCurInfo->NextEntryOffset;
				pCurInfo = (FILE_NOTIFY_INFORMATION*)pByteCurInfo;
			}
		}
	}
}

/// ResourceManager::resource
Resource* ResourceManager::resource(const std::string& src_file_name, const bool bLoadImmediately, const bool bLoadIfNotFound) {
	if (strcmp(src_file_name.c_str(), "nullptr") == 0) {
		return nullptr;
	}

	std::string convertedFileName = src_file_name;
	std::replace(convertedFileName.begin(), convertedFileName.end(), '/', '\\');
	std::transform(convertedFileName.begin(), convertedFileName.end(), convertedFileName.begin(), ::tolower);
	const String fullFileName(convertedFileName);


	auto mapEntry = m_name_to_resource.find(fullFileName);
	if (mapEntry != m_name_to_resource.end()) {

		Resource* const pResource = mapEntry->second;
		if (bLoadImmediately && pResource->m_is_loaded == false) {
			pResource->load();
		}

		return pResource;
	} else if (bLoadIfNotFound == false) {
		return nullptr;
	}

	if (fullFileName.GetLength() < 5) {
		blk::warn("ResourceManager::AddResource() - Invalid file name %s", fullFileName.c_str());
		return nullptr;
	}

	Resource* pResource = nullptr;
	std::string fileExt = GetFileExtension(fullFileName.c_str());

	const std::string& stlFileName = fullFileName.stl_str();
	if (stlFileName.find(".blkanim.ms3d") != std::string::npos || stlFileName.find(".kbanim.ms3d") != std::string::npos) {
		pResource = new Animation();
	} else if (fileExt == "ms3d" || fileExt == "fbx" || fileExt == "diablo3" || fileExt == "ply") {
		pResource = new Model();
	} else if (fileExt == "blkshader" || fileExt == "kbshader") {
		pResource = new Shader();
	} else if (fileExt == "tif" || fileExt == "jpg" || fileExt == "tga" || fileExt == "bmp" || fileExt == "gif" || fileExt == "png" || fileExt == "dds") {
		pResource = new Texture();
	} else if (fileExt == "blkanim" || fileExt == "kbanim") {
		pResource = new Animation();
	} else if (fileExt == "wav") {
		pResource = new WaveFile();
	}

	if (pResource == nullptr) {
		return nullptr;
	}

	pResource->m_full_file_name = stlFileName;
	pResource->m_full_name = fullFileName;//String( pResource->m_full_file_name );

	size_t pos = stlFileName.find_last_of("/");
	if (pos != std::string::npos) {
		pResource->m_name = &stlFileName.c_str()[pos + 1];
	} else {
		pResource->m_name = stlFileName;
	}

	if (bLoadImmediately) {
		pResource->load();
	}

	m_name_to_resource[fullFileName] = pResource;

	return pResource;
}

/// ResourceManager::async_load
Resource* ResourceManager::async_load(const String& stringName) {
	auto mapEntry = m_name_to_resource.find(stringName);
	if (mapEntry != m_name_to_resource.end()) {
		Resource* const pResource = mapEntry->second;
		if (pResource->m_is_loaded) {
			return pResource;
		}

		// Check if resource is currently being loaded
		for (int j = 0; j < m_load_resource_jobs.size(); j++) {
			if (m_load_resource_jobs[j]->m_Resource == mapEntry->second) {
				return nullptr;
			}
		}

		// Create a new loading job for this resources
		LoadResourceJob* const pLoadJob = new LoadResourceJob();
		pLoadJob->m_Resource = pResource;
		m_load_resource_jobs.push_back(pLoadJob);
		g_pJobManager->RegisterJob(pLoadJob);

		return nullptr;
	}

	blk::warn("ResourceManager::AsyncLoadResource() - Failed to kick off a job for %s", stringName.c_str());
	return nullptr;
}

/// ResourceManager::AddPrefab
bool ResourceManager::add_prefab(GameEntity* pEntity, const std::string& PackageName, const std::string& Folder, const std::string& PrefabName, const bool bShouldOverwrite, Prefab** prefab) {
	const std::string fullPackageName = PackageName + (blk::is_package_extension(GetFileExtension(PackageName)) ? "" : ".blkpkg");

	Package* pPackage = nullptr;
	for (unsigned int i = 0; i < m_package_list.size(); i++) {
		if (m_package_list[i]->m_PackageName == fullPackageName) {
			pPackage = m_package_list[i];
			break;
		}
	}

	if (pPackage == nullptr) {
		pPackage = new Package();
		pPackage->m_PackageName = fullPackageName;
		m_package_list.push_back(pPackage);
	}

	Prefab* pNewPrefab = nullptr;
	Package::Folder* pFolder = nullptr;
	for (unsigned int i = 0; i < pPackage->m_Folders.size(); i++) {
		if (pPackage->m_Folders[i].m_FolderName == Folder) {
			pFolder = &pPackage->m_Folders[i];

			for (unsigned int j = 0; j < pFolder->m_pPrefabs.size(); j++) {
				if (pFolder->m_pPrefabs[j]->m_PrefabName == PrefabName) {
					if (bShouldOverwrite) {
						pNewPrefab = pFolder->m_pPrefabs[j];
						break;
					} else {
						return false;
					}
				}
			}
			break;
		}
	}

	if (pFolder == nullptr) {
		pPackage->m_Folders.push_back(Package::Folder());
		pFolder = &pPackage->m_Folders[pPackage->m_Folders.size() - 1];
		pFolder->m_FolderName = Folder;
	}

	if (pNewPrefab == nullptr) {
		pNewPrefab = new Prefab();
		pNewPrefab->m_PrefabName = PrefabName;
		pFolder->m_pPrefabs.push_back(pNewPrefab);
	} else {
		for (int i = 0; i < pNewPrefab->m_GameEntities.size(); i++) {
			delete pNewPrefab->m_GameEntities[i];
		}
		pNewPrefab->m_GameEntities.clear();
	}

	GameEntity* const pNewEntity = new GameEntity(pEntity, true);
	pNewPrefab->m_GameEntities.push_back(pNewEntity);

	if (prefab != nullptr) {
		*prefab = pNewPrefab;
	}
	return true;
}

/// ResourceManager::update_prefab
void ResourceManager::update_prefab(const Prefab* const pPrefab, std::vector<GameEntity*>& pEntityList) {
	if (pPrefab == nullptr || pEntityList.size() == 0 || pPrefab->GetGameEntity(0) == nullptr) {
		return;
	}

	const Guid guid = pPrefab->GetGameEntity(0)->guid();

	Prefab* const updatedPrefab = const_cast<Prefab*>(pPrefab);
	for (int i = 0; i < updatedPrefab->m_GameEntities.size(); i++) {
		delete updatedPrefab->m_GameEntities[i];
	}
	updatedPrefab->m_GameEntities.clear();

	for (int i = 0; i < pEntityList.size(); i++) {
		GameEntity* const pNewEntity = new GameEntity(pEntityList[i], true, &guid);
		updatedPrefab->m_GameEntities.push_back(pNewEntity);
		blk::log("Update prefab %d.  GUID is %d %d %d %d", (INT_PTR)pNewEntity, guid.m_iGuid[0], guid.m_iGuid[1], guid.m_iGuid[2], guid.m_iGuid[3]);
	}

	// Hack
	for (int i = 0; i < m_package_list.size(); i++) {
		if (m_package_list[i] == nullptr) {
			continue;
		}
		save_package(m_package_list[i]->GetPackageName());
	}
}

/// ResourceManager::get_package
Package* ResourceManager::get_package(const std::string& FullPackageName, const bool bLoadImmediately) {
	const size_t packageNamePos = FullPackageName.find_last_of("/");
	std::string packageName = FullPackageName.substr(packageNamePos + 1);
	for (int i = 0; i < m_package_list.size(); i++) {
		if (m_package_list[i]->m_PackageName == packageName) {
			return m_package_list[i];
		}
	}

	File newFile;
	newFile.Open(FullPackageName, File::FileType_t::FT_Read);
	Package* const pPackage = newFile.ReadPackage(bLoadImmediately);
	newFile.Close();

	for (int iFolder = 0; iFolder < pPackage->m_Folders.size(); iFolder++) {

		const std::vector<class Prefab*>& PrefabList = pPackage->m_Folders[iFolder].m_pPrefabs;
		for (int iPrefab = 0; iPrefab < PrefabList.size(); iPrefab++) {
			m_guid_to_entity[PrefabList[iPrefab]->GetGameEntity(0)->guid()] = PrefabList[iPrefab]->GetGameEntity(0);
		}
	}

	m_package_list.push_back(pPackage);

	return pPackage;
}

/// ResourceManager::game_entity
const GameEntity* ResourceManager::game_entity(const Guid& GUID) {
	std::map<Guid, const GameEntity*>::iterator it = m_guid_to_entity.find(GUID);
	if (it == m_guid_to_entity.end()) {
		return nullptr;
	}

	return it->second;
}

/// ResourceManager::save_package
void ResourceManager::save_package(const std::string& PackageName) {
	for (int i = 0; i < m_package_list.size(); i++) {
		if (PackageName == m_package_list[i]->GetPackageName()) {
			File newFile;
			std::string PackageName = "assets/Packages/" + m_package_list[i]->m_PackageName;
			if (!blk::is_package_extension(GetFileExtension(PackageName))) {
				PackageName += ".blkpkg";
			}
			newFile.Open(PackageName, File::FileType_t::FT_Write);
			newFile.WritePackage(*m_package_list[i]);
			newFile.Close();
			break;
		}
	}
}

/// ResourceManager::dump_package_info
void ResourceManager::dump_package_info() {
	for (int i = 0; i < m_package_list.size(); i++) {
		blk::log("Package %s", m_package_list[i]->m_PackageName.c_str());
		for (int j = 0; j < m_package_list[i]->m_Folders.size(); j++) {
			blk::log("	Folder %s", m_package_list[i]->m_Folders[j].m_FolderName.c_str());
			for (int l = 0; l < m_package_list[i]->m_Folders[j].m_pPrefabs.size(); l++) {
				blk::log("		%s ", m_package_list[i]->m_Folders[j].m_pPrefabs[l]->m_PrefabName.c_str());
			}
		}
	}
}

/// ResourceManager::shut_down
void ResourceManager::shut_down() {
	for (auto it = m_name_to_resource.begin(); it != m_name_to_resource.end(); ++it) {
		Resource* const pResource = it->second;
		pResource->release();
		delete pResource;
	}
	m_name_to_resource.clear();

	for (unsigned int i = 0; i < m_package_list.size(); i++) {
		delete m_package_list[i];
	}
	m_package_list.clear();

	if (m_hGameAssetDirectory != INVALID_HANDLE_VALUE) {
		CloseHandle(m_hGameAssetDirectory);
		m_hGameAssetDirectory = INVALID_HANDLE_VALUE;
	}

	if (m_hEngineAssetDirectory != INVALID_HANDLE_VALUE) {
		CloseHandle(m_hEngineAssetDirectory);
		m_hEngineAssetDirectory = INVALID_HANDLE_VALUE;
	}
}

/// ResourceManager::file_modified_cb
void ResourceManager::file_modified_cb(const std::wstring& fileName) {
	/*
	std::wstring convertedFileName = fileName;
	std::transform(convertedFileName.begin(), convertedFileName.end(), convertedFileName.begin(), ::tolower);
	fs::path p = fs::canonical(convertedFileName.c_str());

	// TODO HOT RELOADING
	for (auto it = m_name_to_resource.begin(); it != m_name_to_resource.end(); ++it) {

		Resource* const pCurResource = it->second;
		fs::path resourcePath = fs::canonical(pCurResource->full_file_name());
		if (resourcePath.string() == p.string()) {
			blk::log("Hot reloading %s", p.string().c_str());
			pCurResource->release();
			pCurResource->load();
			return;
		}
	}

	resource(p.string(), true, true);*/
}

/// ResourceManager::register_cb
void ResourceManager::register_cb(ResourceManagerCB pFuncCB, const CallbackReason Reason) {
	blk::error_check(pFuncCB != nullptr && Reason >= 0 && Reason < CBR_Max_Num_Reasons, "ResourceManager::RegisterCB() - Null func passed in");

	CallbackInfo newCBInfo(pFuncCB, Reason);

	blk::error_check(blk::std_contains(m_FunctionCallbacks, newCBInfo) == false, "ResourceManager::RegisterCB() - Registering function/reason multiple times");
	m_FunctionCallbacks.push_back(newCBInfo);
}

/// ResourceManager::unregister_cb
void ResourceManager::unregister_cb(ResourceManagerCB pFuncCB) {
	blk::error_check(pFuncCB != nullptr, "ResourceManager::UnregisterCB() - Null func passed in");

	for (int i = 0; i < m_FunctionCallbacks.size(); i++) {
		if (m_FunctionCallbacks[i].m_pFunc == pFuncCB) {
			blk::std_remove_idx_swap(m_FunctionCallbacks, i);
			i--;
		}
	}
}

/// Package::~Package
Package::~Package() {
	for (int i = 0; i < m_Folders.size(); i++) {
		for (int j = 0; j < m_Folders[i].m_pPrefabs.size(); j++) {
			delete m_Folders[i].m_pPrefabs[j];
		}
		m_Folders[i].m_pPrefabs.clear();
	}
	m_Folders.clear();
}

/// Package::GetPrefab
const Prefab* Package::GetPrefab(const std::string& PrefabName) const {
	for (int iFolder = 0; iFolder < m_Folders.size(); iFolder++) {

		const std::vector<Prefab*>& PrefabList = m_Folders[iFolder].m_pPrefabs;
		for (int iPrefab = 0; iPrefab < PrefabList.size(); iPrefab++) {
			if (PrefabList[iPrefab]->GetPrefabName() == PrefabName) {
				return PrefabList[iPrefab];
			}
		}
	}

	return nullptr;
}
