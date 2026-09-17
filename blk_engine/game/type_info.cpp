/// type_info.cpp
///
/// 2016 blk

#include "blk_core.h"
//#include "Quaternion.h"
#include "entity_header.h"

using namespace std;

NameToTypeInfoMap* g_NameToTypeInfoMap = nullptr;

/// NameToTypeInfoMap::NameToTypeInfoMap()
NameToTypeInfoMap::NameToTypeInfoMap() {
	RegisterVectorOperations<String>("String");
	RegisterVectorOperations<float>("float");
	RegisterVectorOperations<Vec4>("Vec4");
}

/// NameToTypeInfoMap::~NameToTypeInfoMap()
NameToTypeInfoMap::~NameToTypeInfoMap() {
}

/// NameToTypeInfoMap::AddTypeInfo()
void NameToTypeInfoMap::AddTypeInfo(const TypeInfoClass* const classToAdd) {
	m_Map[classToAdd->GetClassName()] = classToAdd;
}

/// NameToTypeInfoMap::AddEnum()
void NameToTypeInfoMap::AddEnum(const std::string& enumName, const std::vector<std::string>& enumFields) {
	m_EnumMap[enumName] = enumFields;
}

Component* ConstructClassFromName(const std::string& className) {
	// find(), not GetTypeInfoFromClassName(), which inserts a null entry for every miss.
	const std::map<std::string, const TypeInfoClass*>& class_map = g_NameToTypeInfoMap->GetClassMap();
	auto it = class_map.find(className);

	// Accepts the legacy kb-prefixed class names still found in older levels and packages.
	if (it == class_map.end() && className.compare(0, 2, "kb") == 0) {
		it = class_map.find(className.substr(2));
	}

	if (it == class_map.end() || it->second == nullptr) {
		return nullptr;
	}

	return it->second->ConstructInstance();
}


// One instance per reflected enum and class, generated from the BLK_PROPERTY markers.
#include "type_info_generated.inl"
typedef Resource* ResourcePtr;
