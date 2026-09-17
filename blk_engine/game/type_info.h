/// type_info.h
///
/// 2016 blk

#pragma once

#include <type_traits>
#include "component.h"

/// TypeInfoVar
class TypeInfoVar {
public:
	TypeInfoVar() :
		m_Type(BLK_TYPEINFO_NONE),
		m_Offset(0),
		m_bIsArray(false),
		m_Min(0.0f),
		m_Max(0.0f),
		m_bHasMin(false),
		m_bHasMax(false) {
	}

	TypeInfoVar(const TypeInfoType_t fieldType, const size_t fieldOffset, const bool bIsArray, const std::string& structName) :
		m_Type(fieldType),
		m_Offset(fieldOffset),
		m_bIsArray(bIsArray),
		m_StructName(structName),
		m_Min(0.0f),
		m_Max(0.0f),
		m_bHasMin(false),
		m_bHasMax(false) {
	}

	const TypeInfoType_t Type() const { return m_Type; }
	const size_t Offset() const { return m_Offset; }
	const bool IsArray() const { return m_bIsArray; }
	const std::string& GetStructName() const { return m_StructName; }

	/// A property's MinVal/MaxVal. The editor clamps to them; loading and saving don't.
	bool HasMin() const { return m_bHasMin; }
	bool HasMax() const { return m_bHasMax; }
	f32 Min() const { return m_Min; }
	f32 Max() const { return m_Max; }
	void SetMin(const f32 value) {
		m_Min = value;
		m_bHasMin = true;
	}
	void SetMax(const f32 value) {
		m_Max = value;
		m_bHasMax = true;
	}

private:
	TypeInfoType_t m_Type;
	size_t m_Offset;
	std::string m_StructName;
	bool m_bIsArray;
	f32 m_Min;
	f32 m_Max;
	bool m_bHasMin;
	bool m_bHasMax;
};

/// TypeInfoClass - Maps a class' member's names to its TypeInfoField
class TypeInfoClass {
public:

	void AddMember(const std::string& memberName, const TypeInfoVar& fieldInfo) {
		memberFieldsMap[memberName] = fieldInfo;
	}

	const TypeInfoVar* GetField(const std::string& memberName) const {
		std::map<std::string, TypeInfoVar>::const_iterator it = memberFieldsMap.find(memberName);
		if (it == memberFieldsMap.end()) {
			return nullptr;
		}
		return &it->second;
	}

	const std::map<std::string, TypeInfoVar>& GetMemberFieldsMap() const { return memberFieldsMap; }

	// find(), not operator[]: a key that doesn't match its AddField would otherwise insert a
	// phantom member at offset 0 that the panel draws and the serializer writes.
	void SetMemberMin(const std::string& memberName, const f32 value) {
		std::map<std::string, TypeInfoVar>::iterator it = memberFieldsMap.find(memberName);
		blk::error_check(it != memberFieldsMap.end(), "SetFieldMin names a property that wasn't added: %s", memberName.c_str());
		it->second.SetMin(value);
	}

	void SetMemberMax(const std::string& memberName, const f32 value) {
		std::map<std::string, TypeInfoVar>::iterator it = memberFieldsMap.find(memberName);
		blk::error_check(it != memberFieldsMap.end(), "SetFieldMax names a property that wasn't added: %s", memberName.c_str());
		it->second.SetMax(value);
	}

	const std::string& GetClassName() const { return m_ClassName; }

	virtual Component* ConstructInstance() const = 0;
	virtual Component* ConstructInstance(const Component* const) const = 0;

protected:
	std::string m_ClassName;

private:
	std::map<std::string, TypeInfoVar> memberFieldsMap;
};

/// NameToTypeInfoMap - This class maps all type info class' to string names.  Code can create an instance of a Component with it's name
class NameToTypeInfoMap {
public:
	NameToTypeInfoMap();
	~NameToTypeInfoMap();

	void AddTypeInfo(const TypeInfoClass* const classToAdd);
	void AddEnum(const std::string& enumName, const std::vector<std::string>& enumFields);

	const TypeInfoClass* GetTypeInfoFromClassName(const std::string& name) { return m_Map[name]; }

	const std::map<std::string, const TypeInfoClass*>& GetClassMap() const { return m_Map; }

	const std::vector<std::string>* GetEnum(const std::string& name) { return &m_EnumMap[name]; }

	template<typename t>
	void RegisterVectorOperations(const std::string& vectorTypeString) {
		std::map<std::string, void (NameToTypeInfoMap::*)(const void*, const size_t)>::const_iterator it = m_ResizeVectorPtr.find(vectorTypeString);
		if (it == m_ResizeVectorPtr.end()) {
			m_ResizeVectorPtr[vectorTypeString] = &NameToTypeInfoMap::ResizeVector_Internal<t>;
			m_GetVectorElementPtr[vectorTypeString] = &NameToTypeInfoMap::GetVectorElement_Internal<t>;
			m_GetVectorSizePtr[vectorTypeString] = &NameToTypeInfoMap::GetVectorSize_Internal<t>;
			m_InsertVectorElementPtr[vectorTypeString] = &NameToTypeInfoMap::InsertVectorElement_Internal<t>;
			m_RemoveVectorElementPtr[vectorTypeString] = &NameToTypeInfoMap::RemoveVectorElement_Internal<t>;
		}
	}

	void ResizeVector(const void* const vectorPtr, const std::string& vectorStringType, const size_t newVectorSize) {
		void (NameToTypeInfoMap::*pFunc)(const void*, const size_t) = nullptr;

		std::map<std::string, void (NameToTypeInfoMap::*)(const void* const, const size_t)>::const_iterator it = m_ResizeVectorPtr.find(vectorStringType);
		if (it != m_ResizeVectorPtr.end()) {
			pFunc = it->second;
			(this->*pFunc)(vectorPtr, newVectorSize);
		}
	}

	void* GetVectorElement(const void* const vectorPtr, const std::string& vectorStringType, const size_t index) {
		void* (NameToTypeInfoMap::*pFunc)(const void*, const size_t) = nullptr;

		std::map<std::string, void* (NameToTypeInfoMap::*)(const void*, const size_t)>::const_iterator it = m_GetVectorElementPtr.find(vectorStringType);
		if (it != m_GetVectorElementPtr.end()) {
			pFunc = it->second;
			return (this->*pFunc)(vectorPtr, index);
		}
		return nullptr;
	}

	size_t GetVectorSize(const void* const vectorPtr, const std::string& vectorStringType) {
		size_t (NameToTypeInfoMap::*pFunc)(const void*) = nullptr;

		std::map<std::string, size_t (NameToTypeInfoMap::*)(const void*)>::const_iterator it = m_GetVectorSizePtr.find(vectorStringType);
		if (it != m_GetVectorSizePtr.end()) {
			pFunc = it->second;
			return (this->*pFunc)(vectorPtr);
		}
		return 0;
	}

	void InsertVectorElement(const void* const vectorPtr, const std::string& vectorStringType, const size_t index) {
		void (NameToTypeInfoMap::*pFunc)(const void*, const size_t) = nullptr;

		std::map<std::string, void (NameToTypeInfoMap::*)(const void*, const size_t)>::const_iterator it = m_InsertVectorElementPtr.find(vectorStringType);
		if (it != m_InsertVectorElementPtr.end()) {
			pFunc = it->second;
			(this->*pFunc)(vectorPtr, index);
		}
	}

	void RemoveVectorElement(const void* const vectorPtr, const std::string& vectorStringType, const size_t index) {
		void (NameToTypeInfoMap::*pFunc)(const void*, const size_t) = nullptr;

		std::map<std::string, void (NameToTypeInfoMap::*)(const void*, const size_t)>::const_iterator it = m_RemoveVectorElementPtr.find(vectorStringType);
		if (it != m_RemoveVectorElementPtr.end()) {
			pFunc = it->second;
			(this->*pFunc)(vectorPtr, index);
		}
	}

private:
	std::map<std::string, const TypeInfoClass*> m_Map;
	std::map<std::string, std::vector<std::string>> m_EnumMap;

	template<typename t>
	void ResizeVector_Internal(const void* const vectorPtr, const size_t vectorSize = 0) {
		// todo: Components are reconstructed on resize
		std::vector<t>& vec = *(std::vector<t>*)vectorPtr;
		std::vector<t> backUp = vec;

		for (size_t i = 0; i < vec.size() && i < backUp.size(); i++) {
			backUp[i] = vec[i];
		}

		vec.resize(vectorSize);

		for (size_t i = 0; i < vec.size() && i < backUp.size(); i++) {
			vec[i] = backUp[i];
		}
	}

	template<typename t>
	void* GetVectorElement_Internal(const void* const vectorPtr = nullptr, const size_t index = 0) {
		std::vector<t>& vec = *(std::vector<t>*)vectorPtr;
		return (void*)&vec[index];
	}

	template<typename t>
	size_t GetVectorSize_Internal(const void* const vectorPtr = nullptr) {
		std::vector<t>& vec = *(std::vector<t>*)vectorPtr;
		return vec.size();
	}

	template<typename t>
	void InsertVectorElement_Internal(const void* const vectorPtr = nullptr, const size_t index = 0) {
		// todo: Components are reconstructed on insert
		std::vector<t>& vec = *(std::vector<t>*)vectorPtr;
		std::vector<t> backUp = vec;

		for (size_t i = 0; i < vec.size() && i < backUp.size(); i++) {
			backUp[i] = vec[i];
		}

		vec.insert(vec.begin() + index, t());

		for (size_t i = 0; i < vec.size() && i < backUp.size(); i++) {
			if (i == index) {
				continue;
			} else if (i < index) {
				vec[i] = backUp[i];
			} else {
				vec[i] = backUp[i - 1];
			}
		}
	}

	template<typename t>
	void RemoveVectorElement_Internal(const void* const vectorPtr = nullptr, const size_t index = 0) {
		std::vector<t>& vec = *(std::vector<t>*)vectorPtr;
		vec.erase(vec.begin() + index);
	}

	std::map<std::string, void (NameToTypeInfoMap::*)(const void*, const size_t)> m_ResizeVectorPtr;
	std::map<std::string, void* (NameToTypeInfoMap::*)(const void*, const size_t)> m_GetVectorElementPtr;
	std::map<std::string, size_t (NameToTypeInfoMap::*)(const void*)> m_GetVectorSizePtr;
	std::map<std::string, void (NameToTypeInfoMap::*)(const void*, const size_t)> m_InsertVectorElementPtr;
	std::map<std::string, void (NameToTypeInfoMap::*)(const void*, const size_t)> m_RemoveVectorElementPtr;
};
extern NameToTypeInfoMap* g_NameToTypeInfoMap;

Component* ConstructClassFromName(const std::string& className);

#define AddEnumField(ENUM_FIELD_NAME, ENUM_STRING_NAME) \
	enumFields.push_back(ENUM_STRING_NAME);

/// The trailing argument is the member's declared type as generate_type_info.py read it.
/// FIELD_TYPE is derived from that type, so a misread declaration fails to compile here
/// instead of registering the wrong tag. It's variadic so types containing commas work.
#define AddField(FIELD_NAME, FIELD_TYPE, CLASS_TYPE, MEMBER_NAME, IS_ARRAY, STRUCT_NAME, ...) \
	{ \
		static_assert(std::is_same_v<decltype(CLASS_TYPE::MEMBER_NAME), __VA_ARGS__>, #CLASS_TYPE "::" #MEMBER_NAME " is not the type generate_type_info.py read from its declaration"); \
		TypeInfoVar newField(FIELD_TYPE, (size_t)&((CLASS_TYPE*)(0))->MEMBER_NAME, IS_ARRAY, STRUCT_NAME); \
		AddMember(FIELD_NAME, newField); \
		if (g_NameToTypeInfoMap == nullptr) { \
			g_NameToTypeInfoMap = new NameToTypeInfoMap(); \
		} \
		g_NameToTypeInfoMap->RegisterVectorOperations<CLASS_TYPE>(#CLASS_TYPE); \
	}


/// Emitted right after the field's AddField when its BLK_PROPERTY gives MinVal/MaxVal.
#define SetFieldMin(FIELD_NAME, VALUE) SetMemberMin(FIELD_NAME, VALUE);
#define SetFieldMax(FIELD_NAME, VALUE) SetMemberMax(FIELD_NAME, VALUE);

#define GenerateEnum(ENUM_TYPE, ENUM_NAME, ADD_ENUM_FIELDS) \
	class ENUM_TYPE##_Enum { \
	public: \
		ENUM_TYPE##_Enum() { \
			std::vector<std::string> enumFields; \
			ADD_ENUM_FIELDS \
			if (g_NameToTypeInfoMap == nullptr) { \
				g_NameToTypeInfoMap = new NameToTypeInfoMap(); \
			} \
			g_NameToTypeInfoMap->AddEnum(ENUM_NAME, enumFields); \
		} \
	};

#define GenerateClass(CLASS_TYPE, ADD_FIELDS) \
	class CLASS_TYPE##_TypeInfo : public TypeInfoClass { \
	public: \
		CLASS_TYPE##_TypeInfo() { \
			ADD_FIELDS \
			m_ClassName = #CLASS_TYPE; \
			if (g_NameToTypeInfoMap == nullptr) { \
				g_NameToTypeInfoMap = new NameToTypeInfoMap(); \
			} \
			g_NameToTypeInfoMap->AddTypeInfo(this); \
		} \
		virtual ~CLASS_TYPE##_TypeInfo() { \
        /* Go ahead and delete the name-to-typeinfo mapping here.  All typeinfos are deleted together anyways when the program terminates. */ \
			delete g_NameToTypeInfoMap; \
			g_NameToTypeInfoMap = nullptr; \
		} \
		virtual Component* ConstructInstance() const { return new CLASS_TYPE; } \
		virtual Component* ConstructInstance(const Component* const pComponentToCopy) const { return new CLASS_TYPE(*static_cast<const CLASS_TYPE*>(pComponentToCopy)); } \
	};

/// Helper for iterating over a class and its ancestor's type info
class TypeInfoHierarchyIterator {
public:
	typedef std::map<std::string, TypeInfoVar>::const_iterator iteratorType;

	TypeInfoHierarchyIterator(const Component* pComponent) :
		m_pComponent(pComponent),
		m_CurrentIndex(0) {
		const std::vector<class TypeInfoClass*>& pClass = m_pComponent->GetTypeInfo();
		m_Iterator = pClass[0]->GetMemberFieldsMap().begin();
	}


	iteratorType Begin() {
		m_CurrentIndex = 0;
		m_Iterator = m_pComponent->GetTypeInfo()[0]->GetMemberFieldsMap().begin();

		return m_Iterator;
	}

	bool IsDone() const {
		return m_CurrentIndex >= m_pComponent->GetTypeInfo().size();
	}

	const iteratorType GetNextTypeInfoField() {
		m_Iterator++;

		if (m_Iterator == m_pComponent->GetTypeInfo()[m_CurrentIndex]->GetMemberFieldsMap().end()) {
			m_CurrentIndex++;

			if (m_CurrentIndex < m_pComponent->GetTypeInfo().size()) {
				m_Iterator = m_pComponent->GetTypeInfo()[m_CurrentIndex]->GetMemberFieldsMap().begin();
			}
		}

		return m_Iterator;
	}

private:
	const Component* m_pComponent;
	iteratorType m_Iterator;
	int m_CurrentIndex;
};

#include "type_info_generated.h"
