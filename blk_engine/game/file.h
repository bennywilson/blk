/// file.h
///
/// 2016 blk

#pragma once

#include <fstream>

class Package;
class GameEntity;
class Component;
enum TypeInfoType_t;
class TypeInfoVar;

/// File
class File {
public:
	enum FileType_t {
		FT_None,
		FT_Read,
		FT_Write,
	};

	File();
	virtual	~File();

	bool Open(const std::string& fileName, const FileType_t fileType);
	void Close();

	bool WritePackage(const Package& package);
	Package* ReadPackage(const bool bLoadAssetsImmediately = true);

	bool WriteGameEntity(const GameEntity* pGameEntity);
	GameEntity* ReadGameEntity();

private:
	bool WriteGameEntity_Internal(const GameEntity* pGameEntity, std::string& curTab);
	void WriteComponent(const Component* const pComponent, std::string& curTab);
	void WriteProperty(const TypeInfoType_t propertyType, const std::string& structName, unsigned char* byteOffsetToVar, std::string& writeBuffer);

	GameEntity* ReadGameEntity_Internal();
	Component* ReadComponent(GameEntity* const pEntity, const std::string& className, Component* ComponentToFill);
	void ReadProperty(const TypeInfoVar* const pTypeInfoVar, unsigned char* const byteOffset, std::string& nextToken, size_t& nextStringPos);
	void ReadToken(std::string& token);

	std::fstream m_File;

	FileType_t m_FileType;
	std::string m_FileName;

	std::string m_Buffer;
	size_t m_CurrentReadPos;
	size_t m_NextReadPos;

	bool m_bIsPackageFile;
	bool m_bLoadAssetsImmediately;
};
