/// file.cpp
///
/// 2016 blk

#include "blk_core.h"
#include "entity_header.h"
#include "file.h"

using namespace std;

/// File::File
File::File() :
	m_FileType(FT_None),
	m_CurrentReadPos(0),
	m_NextReadPos(0),
	m_bIsPackageFile(false),
	m_bLoadAssetsImmediately(true) {
}

/// File::~File
File::~File() {}

/// File::Open
bool File::Open(const string& fileName, const FileType_t fileType) {

	if (fileName.empty()) {
		blk::warn("File::Open() - Empty file name");
		return false;
	}

	if (fileType != FT_Read && fileType != FT_Write) {
		blk::warn("File::Open() - %s has an invalid file type", fileName.c_str());
		return false;
	}

	m_FileType = fileType;
	m_FileName = fileName;

	if (m_FileType == FT_Write) {
		string tempFileName = m_FileName.c_str();
		tempFileName += "_tmp";

		m_File.open(tempFileName.c_str(), fstream::out);
	} else {
		m_File.open(m_FileName.c_str(), fstream::in);

		if (m_File.fail()) {
			return false;
		}
		m_File.seekg(0, m_File.end);
		size_t length = m_File.tellg();
		m_File.seekg(0, m_File.beg);

		char* readBuffer = new char[length + 1];
		m_File.read(readBuffer, length);
		streamsize charsRead = m_File.gcount();

		readBuffer[charsRead] = '\0';
		m_Buffer = readBuffer;

		delete[] readBuffer;

		m_File.close();
	}

	if (blk::is_package_extension(GetFileExtension(fileName))) {
		m_bIsPackageFile = true;
	}
	return true;
}

/// File::Close
void File::Close() {
	if (m_FileType != FT_Write && m_FileType != FT_Read) {
		blk::warn("File::Close() - Tried to close %s with an invalid file type", m_FileName.c_str());
		return;
	}

	if (m_FileType == FT_Write) {  // note: read files are already closed
		m_File.close();

		std::string tempFileName = m_FileName.c_str();
		tempFileName += "_tmp";
		CopyFile(tempFileName.c_str(), m_FileName.c_str(), false);
		DeleteFile(tempFileName.c_str());
	}

	m_FileType = FT_None;
}

/// File::ReadGameEntity
GameEntity* File::ReadGameEntity() {
	if (m_FileType != FT_Read) {
		blk::warn("File::ReadGameEntity() - Tried to read from file %s, but the file does not have the correct type.", m_FileName.c_str());
		return nullptr;
	}
	return ReadGameEntity_Internal();
}

/// File::ReadGameEntity_Internal
GameEntity* File::ReadGameEntity_Internal() {
	size_t nextStringPos = m_Buffer.find_first_of(" {\n\r\t", m_CurrentReadPos);
	if (nextStringPos == std::string::npos) {
		return nullptr;
	}

	// Read GUID
	m_CurrentReadPos = m_Buffer.find_first_of(" ", m_CurrentReadPos);
	if (m_CurrentReadPos == std::string::npos) {
		return nullptr;
	}
	m_CurrentReadPos += 1;

	nextStringPos = m_Buffer.find_first_of(" ", m_CurrentReadPos);
	const std::string guid1 = m_Buffer.substr(m_CurrentReadPos, nextStringPos - m_CurrentReadPos);

	m_CurrentReadPos = m_Buffer.find_first_of(" ", nextStringPos) + 1;
	nextStringPos = m_Buffer.find_first_of(" ", m_CurrentReadPos);
	const std::string guid2 = m_Buffer.substr(m_CurrentReadPos, nextStringPos - m_CurrentReadPos);

	m_CurrentReadPos = m_Buffer.find_first_of(" ", nextStringPos) + 1;
	nextStringPos = m_Buffer.find_first_of(" ", m_CurrentReadPos);
	const std::string guid3 = m_Buffer.substr(m_CurrentReadPos, nextStringPos - m_CurrentReadPos);

	m_CurrentReadPos = m_Buffer.find_first_of(" ", nextStringPos) + 1;
	nextStringPos = m_Buffer.find_first_of(" ", m_CurrentReadPos);
	const std::string guid4 = m_Buffer.substr(m_CurrentReadPos, nextStringPos - m_CurrentReadPos);

	m_CurrentReadPos = m_Buffer.find_first_of("{", m_CurrentReadPos) + 1;
	int bracketCount = 1;
	nextStringPos = m_Buffer.find_first_of(" {\n\r\t", m_CurrentReadPos);

	Guid entityGUID;
	entityGUID.m_iGuid[0] = strtoul(&guid1[0], nullptr, 0);
	entityGUID.m_iGuid[1] = strtoul(&guid2[0], nullptr, 0);
	entityGUID.m_iGuid[2] = strtoul(&guid3[0], nullptr, 0);
	entityGUID.m_iGuid[3] = strtoul(&guid4[0], nullptr, 0);

	GameEntity* const pGameEntity = new GameEntity(&entityGUID, m_bIsPackageFile);

	do {
		std::string nextToken = m_Buffer.substr(m_CurrentReadPos, nextStringPos - m_CurrentReadPos);
		char nextChar = m_Buffer[m_CurrentReadPos];
		m_CurrentReadPos = nextStringPos + 1;

		if (nextChar == '}') {
			bracketCount--;
		} else if (nextChar == '{') {
			bracketCount++;
		} else if (nextChar == ' ' || nextChar == '{' || nextChar == '\n' || nextChar == '\r' || nextChar == '\t' || nextChar == '=') {

		} else if (nextToken.find("Component", 0) != std::string::npos) {
			ReadComponent(pGameEntity, nextToken, NULL);
		}

		nextStringPos = m_Buffer.find_first_of(" {\n\r\t", m_CurrentReadPos);
	} while (bracketCount != 0 && nextStringPos != std::string::npos);

	pGameEntity->post_load();

	for (int i = 0; i < pGameEntity->num_components(); i++) {
		GameComponent* const pComponent = pGameEntity->component(i);
		if (pComponent->IsEnabled() && m_bIsPackageFile == false) {
			pComponent->Enable(false);
			pComponent->Enable(true);
		}
	}
	return pGameEntity;
}

/// File::ReadComponent
Component* File::ReadComponent(GameEntity* const pGameEntity, const std::string& componentType, Component* ComponentToFill) {
	Component* pComponent = nullptr;
	if (ComponentToFill != nullptr) {
		pComponent = ComponentToFill;
	} else if (componentType == "TransformComponent") {
		pComponent = (Component*)(pGameEntity->component(0));
	} else {
		pComponent = ConstructClassFromName(componentType);
		pGameEntity->add_component(pComponent);
	}

	size_t nextStringPos = m_CurrentReadPos + 1;
	int bracketState = 0;

	do {
		std::string nextToken = m_Buffer.substr(m_CurrentReadPos, nextStringPos - m_CurrentReadPos);
		char nextChar = m_Buffer[m_CurrentReadPos];
		m_CurrentReadPos = nextStringPos + 1;

		if (nextChar == '{') {
			bracketState++;
		} else if (nextChar == ' ' || nextChar == '\n' || nextChar == '\r' || nextChar == '\t' || nextChar == '=') {

		} else if (nextChar == '}') {
			bracketState--;
			if (bracketState == 0) {
				break;
			}
		} else {
			const std::vector<class TypeInfoClass*>& typeInfo = pComponent->GetTypeInfo();
			const TypeInfoVar* currentVar = nullptr;

			for (int i = 0; i < typeInfo.size(); i++) {
				const TypeInfoVar* typeInfoVar = typeInfo[i]->GetField(nextToken);
				if (typeInfoVar != nullptr) {
					currentVar = typeInfoVar;
					break;
				}
			}

			// Unrecognized var.  Go to the next line
			if (currentVar == nullptr) {
				while (m_Buffer[m_CurrentReadPos] != '\n') {
					m_CurrentReadPos++;
				}
				while (m_Buffer[m_CurrentReadPos] == ' ' || m_Buffer[m_CurrentReadPos] == '{' || m_Buffer[m_CurrentReadPos] == '\n' || m_Buffer[m_CurrentReadPos] == '\r' || m_Buffer[m_CurrentReadPos] == '\t') {
					m_CurrentReadPos++;
				}
				nextStringPos = m_Buffer.find_first_of(" {\n\r\t", m_CurrentReadPos);
				continue;
			}

			m_CurrentReadPos++;
			while (m_Buffer[m_CurrentReadPos] == ' ') {
				m_CurrentReadPos++;
			}

			if (m_Buffer[m_CurrentReadPos] == '"') {
				// Reading a name in quuotes, get the whole thing
				nextStringPos = m_Buffer.find_first_of("\"", m_CurrentReadPos + 1);
			} else {
				nextStringPos = m_Buffer.find_first_of(" {\n\r\t", m_CurrentReadPos);
			}

			u8* pCurrentComponentAsBytePtr = ((u8*)pComponent);

			nextToken = m_Buffer.substr(m_CurrentReadPos, nextStringPos - m_CurrentReadPos);
			if (currentVar->IsArray()) {
				switch (currentVar->Type()) {
					case BLK_TYPEINFO_SHADER: {
						std::vector<class Shader*>& shaderList = *(std::vector<class Shader*>*)(&pCurrentComponentAsBytePtr[currentVar->Offset()]);

						shaderList.resize(atoi(nextToken.c_str()));
						int size = (int)shaderList.size();
						while (size > 0) {
							m_CurrentReadPos++;
							size /= 10;
						}

						for (int i = 0; i < shaderList.size(); i++) {
							while (m_Buffer[m_CurrentReadPos] == ' ' || m_Buffer[m_CurrentReadPos] == '\n' || m_Buffer[m_CurrentReadPos] == '\r' || m_Buffer[m_CurrentReadPos] == '\t') {
								m_CurrentReadPos++;
							}
							nextStringPos = m_Buffer.find_first_of(" {\n\r\t", m_CurrentReadPos);
							nextToken = m_Buffer.substr(m_CurrentReadPos, nextStringPos - m_CurrentReadPos);

							const TypeInfoVar* pVar = currentVar;
							shaderList[i] = (Shader*)g_ResourceManager.resource(nextToken, m_bLoadAssetsImmediately, true);
							m_CurrentReadPos = nextStringPos;
						}

						currentVar = nullptr;
						break;
					}

					case BLK_TYPEINFO_TEXTURE: {
						std::vector<class Texture*>& textureList = *(std::vector<Texture*>*)(&pCurrentComponentAsBytePtr[currentVar->Offset()]);

						textureList.resize(atoi(nextToken.c_str()));
						int size = (int)textureList.size();
						while (size > 0) {
							m_CurrentReadPos++;
							size /= 10;
						}

						for (int i = 0; i < textureList.size(); i++) {
							while (m_Buffer[m_CurrentReadPos] == ' ' || m_Buffer[m_CurrentReadPos] == '\n' || m_Buffer[m_CurrentReadPos] == '\r' || m_Buffer[m_CurrentReadPos] == '\t') {
								m_CurrentReadPos++;
							}
							nextStringPos = m_Buffer.find_first_of(" {\n\r\t", m_CurrentReadPos);
							nextToken = m_Buffer.substr(m_CurrentReadPos, nextStringPos - m_CurrentReadPos);

							const TypeInfoVar* pVar = currentVar;
							textureList[i] = (Texture*)g_ResourceManager.resource(nextToken, m_bLoadAssetsImmediately, true);
							m_CurrentReadPos = nextStringPos;
						}

						currentVar = nullptr;
						break;
					}
					default: {
						u8* const arrayBytePtr = &pCurrentComponentAsBytePtr[currentVar->Offset()];

						const size_t arraySize = atoi(nextToken.c_str());
						g_NameToTypeInfoMap->ResizeVector(arrayBytePtr, currentVar->GetStructName(), arraySize);
						for (int i = 0; i < arraySize; i++) {

							u8* const arrayElem = (u8*)g_NameToTypeInfoMap->GetVectorElement(arrayBytePtr, currentVar->GetStructName(), i);

							if (currentVar->Type() == BLK_TYPEINFO_STRUCT) {
								while (m_Buffer[m_CurrentReadPos] != '{') {
									m_CurrentReadPos++;
								}
								Component* const pNewComponent = ReadComponent(pGameEntity, currentVar->GetStructName(), (Component*)arrayElem);
								pNewComponent->SetOwningComponent(pComponent);
							} else {
								// hack
								if (currentVar->Type() == BLK_TYPEINFO_FLOAT) {
									m_CurrentReadPos = nextStringPos + 2;
								} else {
									m_CurrentReadPos = nextStringPos + 1;
								}
								nextStringPos = m_Buffer.find_first_of(" {\n\r\t", m_CurrentReadPos);
								nextToken = m_Buffer.substr(m_CurrentReadPos, nextStringPos - m_CurrentReadPos);
								ReadProperty(currentVar, arrayElem, nextToken, nextStringPos);
								nextStringPos = m_Buffer.find_first_of(" {\n\r\t", m_CurrentReadPos);
							}
							nextStringPos = m_Buffer.find_first_of(" }{\n\r\t", m_CurrentReadPos);
						}
						break;
					}
				}
			} else {
				ReadProperty(currentVar, &pCurrentComponentAsBytePtr[currentVar->Offset()], nextToken, nextStringPos);
			}

			if (m_Buffer[m_CurrentReadPos] == '}') {
				bracketState--;
				if (bracketState == 0) {
					m_CurrentReadPos++;
					break;
				}
			}
			m_CurrentReadPos = nextStringPos + 1;
		}

		while (m_Buffer[m_CurrentReadPos] == ' ' || m_Buffer[m_CurrentReadPos] == '{' || m_Buffer[m_CurrentReadPos] == '\n' || m_Buffer[m_CurrentReadPos] == '\r' || m_Buffer[m_CurrentReadPos] == '\t') {
			m_CurrentReadPos++;
		}
		nextStringPos = m_Buffer.find_first_of(" {\n\r\t", m_CurrentReadPos);
	} while (bracketState != 0);

	return pComponent;
}

/// File::ReadProperty
void File::ReadProperty(const TypeInfoVar* const pTypeInfoVar, u8* const byteOffset, std::string& nextToken, size_t& nextStringPos) {
	switch (pTypeInfoVar->Type()) {
		case BLK_TYPEINFO_BOOL: {
			bool& pComponentBool = *(bool*)byteOffset;
			pComponentBool = (nextToken[0] - '0') == 1;
			break;
		}

		case BLK_TYPEINFO_FLOAT: {
			float& pComponentFloat = *(float*)byteOffset;
			pComponentFloat = (float)atof(nextToken.c_str());
			break;
		}

		case BLK_TYPEINFO_INT: {
			int& pComponentInt = *(int*)byteOffset;
			pComponentInt = atoi(nextToken.c_str());
			break;
		}

		case BLK_TYPEINFO_STRING: {
			String& string = *(String*)byteOffset;
			std::string strippedString = nextToken;
			strippedString.erase(std::remove(strippedString.begin(), strippedString.end(), '"'), strippedString.end());
			string = strippedString;
			break;
		}

		case BLK_TYPEINFO_STD_STRING: {
			std::string& theString = *(std::string*)byteOffset;
			break;
		}

		case BLK_TYPEINFO_VECTOR4: {
			Vec4& theVec = *(Vec4*)byteOffset;

			theVec[0] = (float)atof(nextToken.c_str());

			m_CurrentReadPos = nextStringPos + 1;
			nextStringPos = m_Buffer.find_first_of(" {\n\r\t", m_CurrentReadPos);
			nextToken = m_Buffer.substr(m_CurrentReadPos, nextStringPos - m_CurrentReadPos);
			theVec[1] = (float)atof(nextToken.c_str());

			m_CurrentReadPos = nextStringPos + 1;
			nextStringPos = m_Buffer.find_first_of(" {\n\r\t", m_CurrentReadPos);
			nextToken = m_Buffer.substr(m_CurrentReadPos, nextStringPos - m_CurrentReadPos);
			theVec[2] = (float)atof(nextToken.c_str());

			m_CurrentReadPos = nextStringPos + 1;
			nextStringPos = m_Buffer.find_first_of(" {\n\r\t", m_CurrentReadPos);
			nextToken = m_Buffer.substr(m_CurrentReadPos, nextStringPos - m_CurrentReadPos);
			theVec[3] = (float)atof(nextToken.c_str());

			break;
		}

		case BLK_TYPEINFO_VECTOR: {
			Vec3& theVec = *(Vec3*)byteOffset;

			theVec[0] = (float)atof(nextToken.c_str());

			m_CurrentReadPos = nextStringPos + 1;
			nextStringPos = m_Buffer.find_first_of(" {\n\r\t", m_CurrentReadPos);
			nextToken = m_Buffer.substr(m_CurrentReadPos, nextStringPos - m_CurrentReadPos);
			theVec[1] = (float)atof(nextToken.c_str());

			m_CurrentReadPos = nextStringPos + 1;
			nextStringPos = m_Buffer.find_first_of(" {\n\r\t", m_CurrentReadPos);
			nextToken = m_Buffer.substr(m_CurrentReadPos, nextStringPos - m_CurrentReadPos);
			theVec[2] = (float)atof(nextToken.c_str());


			break;
		}

		case BLK_TYPEINFO_GAMEENTITY: {
			GameEntityPtr& entityPtr = *(GameEntityPtr*)byteOffset;

			// Read GUID
			const std::string guid1 = nextToken;

			m_CurrentReadPos = m_Buffer.find_first_of(" ", nextStringPos) + 1;
			nextStringPos = m_Buffer.find_first_of(" ", m_CurrentReadPos);
			const std::string guid2 = m_Buffer.substr(m_CurrentReadPos, nextStringPos - m_CurrentReadPos);

			m_CurrentReadPos = m_Buffer.find_first_of(" ", nextStringPos) + 1;
			nextStringPos = m_Buffer.find_first_of(" ", m_CurrentReadPos);
			const std::string guid3 = m_Buffer.substr(m_CurrentReadPos, nextStringPos - m_CurrentReadPos);

			m_CurrentReadPos = m_Buffer.find_first_of(" ", nextStringPos) + 1;
			nextStringPos = m_Buffer.find_first_of(" {\n\r\t", m_CurrentReadPos);
			const std::string guid4 = m_Buffer.substr(m_CurrentReadPos, nextStringPos - m_CurrentReadPos);

			Guid entityGUID;
			entityGUID.m_iGuid[0] = strtoul(&guid1[0], nullptr, 0);
			entityGUID.m_iGuid[1] = strtoul(&guid2[0], nullptr, 0);
			entityGUID.m_iGuid[2] = strtoul(&guid3[0], nullptr, 0);
			entityGUID.m_iGuid[3] = strtoul(&guid4[0], nullptr, 0);
			entityPtr.SetEntity(entityGUID);
			break;
		}

		case BLK_TYPEINFO_SOUNDWAVE:
		case BLK_TYPEINFO_ANIMATION:
		case BLK_TYPEINFO_PTR:
		case BLK_TYPEINFO_TEXTURE:
		case BLK_TYPEINFO_STATICMODEL:
		case BLK_TYPEINFO_SHADER: {
			INT_PTR* intPtr = (INT_PTR*)byteOffset;
			INT_PTR& intRef = *intPtr;
			if (nextToken != "NULL") {
				intRef = (INT_PTR)(g_ResourceManager.resource(nextToken, m_bLoadAssetsImmediately, true));
			}
			break;
		}

		case BLK_TYPEINFO_ENUM: {
			int& pComponentInt = *(int*)byteOffset;

			const std::vector<std::string>* enumList = g_NameToTypeInfoMap->GetEnum(pTypeInfoVar->GetStructName());

			pComponentInt = 0;
			int i = 0;
			for (i = 0; i < enumList->size(); i++) {
				if ((*enumList)[i] == nextToken) {
					pComponentInt = i;
					break;
				}
			}

			if (i == enumList->size()) {
				blk::warn("Enum value out of range");
			}
		}
	}
}

/// File::WriteGameEntity
bool File::WriteGameEntity(const GameEntity* pGameObject) {
	std::string curTab = "";
	if (this->m_bIsPackageFile) {
		curTab += "\t";
	}

	return WriteGameEntity_Internal(pGameObject, curTab);
}

/// File::WriteGameEntity_Internal
bool File::WriteGameEntity_Internal(const GameEntity* pGameObject, std::string& curTab) {
	if (m_FileType != FT_Write) {
		blk::warn("File::WriteGameEntity() - Tried to write to file %s, but the file does not have the correct type.", m_FileName.c_str());
		return false;
	}

	if (pGameObject == NULL) {
		blk::warn("File::WriteGameEntity() - Tried to write to file %s, but the game object passed in is null.", m_FileName.c_str());
		return false;
	}

	const Guid& guid = pGameObject->guid();
	m_Buffer += curTab + "GameEntity " + std::to_string(guid.m_iGuid[0]) + " " + std::to_string(guid.m_iGuid[1]) + " " + std::to_string(guid.m_iGuid[2]) + " " + std::to_string(guid.m_iGuid[3]) + " {\n";

	curTab += "\t";

	for (int i = 0; i < pGameObject->num_components(); i++) {
		const Component* const pCurComponent = pGameObject->component(i);
		WriteComponent(pCurComponent, curTab);
		m_Buffer += "\n";
	}

	curTab.resize(curTab.size() - 1);
	m_Buffer += curTab + "}\n";

	// hack
	if (curTab.size() > 0) {
		curTab.resize(curTab.size() - 1);
		m_Buffer += curTab + "}\n";
	}

	m_File.write(m_Buffer.c_str(), m_Buffer.length());
	m_Buffer.clear();

	return true;
}

/// File::WriteComponent
void File::WriteComponent(const Component* const pCurComponent, std::string& curTab) {
	m_Buffer += curTab + pCurComponent->GetComponentClassName() + " { \n";
	curTab += "\t";

	TypeInfoHierarchyIterator iterator(pCurComponent);
	u8* componentBytePtr = (u8*)pCurComponent;

	// Write out variables
	for (TypeInfoHierarchyIterator::iteratorType pNextField = iterator.Begin(); iterator.IsDone() == false; pNextField = iterator.GetNextTypeInfoField()) {
		u8* byteOffsetToVar = componentBytePtr + pNextField->second.Offset();

		m_Buffer += curTab + pNextField->first.c_str();  // Write out var name
		m_Buffer += " = ";

		// Write out arrays
		if (pNextField->second.IsArray()) {
			switch (pNextField->second.Type()) {

				case BLK_TYPEINFO_SHADER: {
					std::vector<class Shader*>* shaderList = (std::vector<class Shader*>*)(byteOffsetToVar);
					m_Buffer += std::to_string(shaderList->size()) + "\n\t" + curTab;

					for (int i = 0; i < shaderList->size(); i++) {
						WriteProperty(pNextField->second.Type(), pNextField->second.GetStructName(), (u8*)&(*shaderList)[i], m_Buffer);
						m_Buffer += "\n";
					}
					break;
				}

				case BLK_TYPEINFO_TEXTURE: {
					std::vector<class Texture*>* textureList = (std::vector<class Texture*>*)(byteOffsetToVar);
					m_Buffer += std::to_string(textureList->size()) + "\n\t" + curTab;

					for (int i = 0; i < textureList->size(); i++) {
						WriteProperty(pNextField->second.Type(), pNextField->second.GetStructName(), (u8*)&(*textureList)[i], m_Buffer);
						m_Buffer += "\n";
					}
					break;
				}
				default: {
					const size_t vectorSize = g_NameToTypeInfoMap->GetVectorSize(byteOffsetToVar, pNextField->second.GetStructName());
					m_Buffer += std::to_string(vectorSize);
					for (int i = 0; i < vectorSize; i++) {
						m_Buffer += "\n";
						u8* const arrayElem = (u8*)g_NameToTypeInfoMap->GetVectorElement(byteOffsetToVar, pNextField->second.GetStructName(), i);
						if (pNextField->second.Type() == BLK_TYPEINFO_STRUCT) {
							curTab += "\t";
							WriteComponent((Component*)arrayElem, curTab);
							curTab.resize(curTab.size() - 1);
						} else {
							WriteProperty(pNextField->second.Type(), pNextField->second.GetStructName(), arrayElem, m_Buffer);
						}
					}
					break;
				}
			}
		} else {
			WriteProperty(pNextField->second.Type(), pNextField->second.GetStructName(), byteOffsetToVar, m_Buffer);
		}

		m_Buffer += "\n";
	}
	curTab.resize(curTab.size() - 1);
	m_Buffer += curTab + "}";
}

/// File::WriteComponent
void File::WriteProperty(const TypeInfoType_t propertyType, const std::string& structName, u8* byteOffsetToVar, std::string& writeBuffer) {
	static char charBuffer[256];

	switch (propertyType) {
		case BLK_TYPEINFO_BOOL: {
			bool* const boolVal = (bool*)byteOffsetToVar;
			if (*boolVal == 0) {
				writeBuffer += "0";
			} else {
				writeBuffer += "1";
			}
			break;
		}

		case BLK_TYPEINFO_STD_STRING: {
			std::string& string = *((std::string*)byteOffsetToVar);
			writeBuffer += "\"";
			writeBuffer += string.c_str();
			writeBuffer += "\"";
			break;
		}

		case BLK_TYPEINFO_STRING: {
			String& string = *((String*)byteOffsetToVar);
			writeBuffer += "\"";
			writeBuffer += string.c_str();
			writeBuffer += "\"";
			break;
		}

		case BLK_TYPEINFO_VECTOR4: {
			Vec4& vector = *(Vec4*)byteOffsetToVar;

			sprintf_s(charBuffer, "%f %f %f %f", vector.x, vector.y, vector.z, vector.w);
			writeBuffer += charBuffer;
			break;
		}

		case BLK_TYPEINFO_VECTOR: {
			Vec3& vector = *(Vec3*)byteOffsetToVar;
			sprintf_s(charBuffer, "%f %f %f", vector.x, vector.y, vector.z);
			writeBuffer += charBuffer;
			break;
		}

		case BLK_TYPEINFO_FLOAT: {
			float theFloat = *(float*)byteOffsetToVar;
			sprintf_s(charBuffer, " %f", theFloat);
			writeBuffer += charBuffer;
			break;
		}

		case BLK_TYPEINFO_INT: {
			int theInt = *(int*)byteOffsetToVar;
			sprintf_s(charBuffer, "%d", theInt);
			writeBuffer += charBuffer;
			break;
		}

		case BLK_TYPEINFO_SOUNDWAVE:
		case BLK_TYPEINFO_ANIMATION:
		case BLK_TYPEINFO_PTR:
		case BLK_TYPEINFO_TEXTURE:
		case BLK_TYPEINFO_STATICMODEL:
		case BLK_TYPEINFO_SHADER: {
			Resource* pResource = *((Resource**)byteOffsetToVar);
			if (pResource != NULL) {
				const char* fullFileName = pResource->full_file_name().c_str();
				sprintf_s(charBuffer, "%s", fullFileName);
				writeBuffer += charBuffer;
			} else {
				writeBuffer += "NULL";
			}
			writeBuffer += "\0";
			break;
		}

		case BLK_TYPEINFO_GAMEENTITY: {
			const GameEntityPtr entityPtr = *(GameEntityPtr*)byteOffsetToVar;

			if (entityPtr.GetEntity() != nullptr) {
				const Guid entityGUID = entityPtr.GetGUID();
				writeBuffer += std::to_string(entityGUID.m_iGuid[0]) + " ";
				writeBuffer += std::to_string(entityGUID.m_iGuid[1]) + " ";
				writeBuffer += std::to_string(entityGUID.m_iGuid[2]) + " ";
				writeBuffer += std::to_string(entityGUID.m_iGuid[3]);
			} else {
				writeBuffer += "0 0 0 0";
			}
			break;
		}

		case BLK_TYPEINFO_ENUM: {
			const std::vector<std::string>* enumList = g_NameToTypeInfoMap->GetEnum(structName);
			int& enumIntValue = *((int*)byteOffsetToVar);

			if (enumIntValue < 0 || enumIntValue >= enumList->size()) {
				blk::warn("Enum value out of range! for %s", structName.c_str());
				enumIntValue = 0;
			}

			writeBuffer += (*enumList)[enumIntValue].c_str();
			break;
		}
	}
}

/// File::WritePackage
bool File::WritePackage(const Package& package) {
	if (m_FileType != FT_Write) {
		blk::warn("File::WritePackage() - Tried to write to file %s, but the file does not have the correct type.", m_FileName.c_str());
		return false;
	}

	if (package.NumFolders() == 0) {
		blk::warn("File::WritePackage() - Tried to write to file %s with no folders, m_FileName.c_str() ");
		return false;
	}

	std::string curTab = "\t";

	blk::log("Writing package %s", package.GetPackageName().c_str());

	for (int i = 0; i < package.NumFolders(); i++) {
		const std::vector<class Prefab*>& prefabs = package.GetPrefabsForFolder(i);

		m_Buffer += package.GetFolderName(i) + " " + std::to_string(prefabs.size()) + "\n";

		for (int j = 0; j < prefabs.size(); j++) {
			m_Buffer += "Prefab ";
			m_Buffer += std::to_string(prefabs[j]->NumGameEntities());
			m_Buffer += " {\n";

			m_Buffer += "\t" + prefabs[j]->GetPrefabName() + "\n";
			for (int l = 0; l < prefabs[j]->NumGameEntities(); l++) {
				// Refresh guid table
				GameEntityPtr entityPtr;
				entityPtr.SetEntity(const_cast<GameEntity*>(prefabs[j]->GetGameEntity(l)));
				WriteGameEntity_Internal(prefabs[j]->GetGameEntity(l), curTab);
			}
			m_Buffer += "}\n";
		}
	}

	m_File.write(m_Buffer.c_str(), m_Buffer.length());
	m_Buffer.clear();

	return true;
}

/// File::ReadToken
void File::ReadToken(std::string& token) {
	// Find Start of next token
	m_NextReadPos = m_Buffer.find_first_not_of(" \t{\n\r}", m_CurrentReadPos);
	if (m_NextReadPos == std::string::npos) {
		return;
	}
	m_CurrentReadPos = m_NextReadPos;

	// Get next token
	m_NextReadPos = m_Buffer.find_first_of(" {\n\r\t", m_CurrentReadPos);
	token = m_Buffer.substr(m_CurrentReadPos, m_NextReadPos - m_CurrentReadPos);
	m_CurrentReadPos = m_NextReadPos + 1;
}

/// File::ReadPackage
Package* File::ReadPackage(const bool bLoadAssetsImmediately) {
	m_bLoadAssetsImmediately = bLoadAssetsImmediately;

	if (m_FileType != FT_Read) {
		blk::warn("File::ReadPackage() - Tried to read to file %s, but the file does not have the correct type.", m_FileName.c_str());
		return nullptr;
	}

	std::string nextToken;
	ReadToken(nextToken);

	Package* newPackage = new Package();
	const size_t packageNamePos = m_FileName.find_last_of("/");
	newPackage->m_PackageName = m_FileName.substr(packageNamePos + 1);
	int folderIdx = -1;

	while (m_NextReadPos != std::string::npos) {
		Package::Folder newFolder;
		newFolder.m_FolderName = nextToken;

		ReadToken(nextToken);

		const unsigned int NumPrefabsInFolder = std::stoi(nextToken);

		if (NumPrefabsInFolder > 256) {
			blk::error("Too many prefabs in folder");
		}

		for (unsigned int prefabIdx = 0; prefabIdx < NumPrefabsInFolder; prefabIdx++) {
			ReadToken(nextToken);
			// Accepts the legacy "kbPrefab" tag.
			if (nextToken != "Prefab" && nextToken != "kbPrefab") {
				blk::error("Expected 'Prefab' while reading file");
			}

			ReadToken(nextToken);
			const unsigned int NumEntitiesInPrefab = std::stoi(nextToken);
			if (NumEntitiesInPrefab > 16) {
				blk::error("Too many entities in prefab");
			}

			Prefab* pPrefab = new Prefab();
			ReadToken(pPrefab->m_PrefabName);
			newFolder.m_pPrefabs.push_back(pPrefab);

			for (unsigned entityIdx = 0; entityIdx < NumEntitiesInPrefab; entityIdx++) {
				GameEntity* pEntity = ReadGameEntity();
				pPrefab->m_GameEntities.push_back(pEntity);
			}
		}

		newPackage->m_Folders.push_back(newFolder);
		folderIdx++;

		ReadToken(nextToken);
	}

	return newPackage;
}