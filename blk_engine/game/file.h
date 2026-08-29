/// kbFile.h
///
/// 2016 blk

#pragma once

#include <fstream>

class kbPackage;
class GameEntity;
class kbComponent;
enum kbTypeInfoType_t;
class kbTypeInfoVar;

/// kbFile
class kbFile {
public:
	enum kbFileType_t {
		FT_None,
		FT_Read,
		FT_Write,
	};

	kbFile();
	virtual	~kbFile();

	bool Open(const std::string& fileName, const kbFileType_t fileType);
	void Close();

	bool WritePackage(const kbPackage& Package);
	kbPackage* ReadPackage(const bool bLoadAssetsImmediately = true);

	bool WriteGameEntity(const GameEntity* pGameEntity);
	GameEntity* ReadGameEntity();

private:
	bool WriteGameEntity_Internal(const GameEntity* pGameEntity, std::string& curTab);
	void WriteComponent(const kbComponent* const pComponent, std::string& curTab);
	void WriteProperty(const kbTypeInfoType_t propertyType, const std::string& structName, unsigned char* byteOffsetToVar, std::string& writeBuffer);

	GameEntity* ReadGameEntity_Internal();
	kbComponent* ReadComponent(GameEntity* const pEntity, const std::string& className, kbComponent* ComponentToFill);
	void ReadProperty(const kbTypeInfoVar* const pTypeInfoVar, unsigned char* const byteOffset, std::string& nextToken, size_t& nextStringPos);
	void ReadToken(std::string& token);

	std::fstream m_File;

	kbFileType_t m_FileType;
	std::string m_FileName;

	std::string m_Buffer;
	size_t m_CurrentReadPos;
	size_t m_NextReadPos;

	bool m_bIsPackageFile;
	bool m_bLoadAssetsImmediately;
};
