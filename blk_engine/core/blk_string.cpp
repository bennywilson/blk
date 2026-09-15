/// blk_string.cpp
///
/// 2016 blk

#include <map>
#include <string>
#include <vector>
#include "blk_string.h"

std::map<std::string, int>* g_StringTable = nullptr;
std::vector<std::string>* g_StringList = nullptr;
std::string* g_EmptyString = nullptr;

String String::EmptyString("");

/// String::ShutDown
void String::ShutDown() {
	delete g_StringTable;
	g_StringTable = nullptr;

	delete g_StringList;
	g_StringList = nullptr;

	delete g_EmptyString;
	g_EmptyString = nullptr;
}

/// String::String
String::String(const std::string& InString) {
	if (g_StringTable == nullptr) {
		g_StringTable = new std::map<std::string, int>();
		g_StringList = new std::vector<std::string>();
		g_EmptyString = new std::string;
	}

	std::map<std::string, int>::const_iterator it = g_StringTable->find(InString);
	if (it != g_StringTable->end()) {
		m_StringTableIndex = it->second;
	} else {
		g_StringList->push_back(InString);
		m_StringTableIndex = (int)(g_StringList->size() - 1);
		(*g_StringTable)[InString] = m_StringTableIndex;
	}
}

/// String::String
String::String(const char* src) {
	if (g_StringTable == nullptr) {
		g_StringTable = new std::map<std::string, int>();
		g_StringList = new std::vector<std::string>();
		g_EmptyString = new std::string;
	}

	const std::string src_string = src;
	std::map<std::string, int>::const_iterator it = g_StringTable->find(src_string);
	if (it != g_StringTable->end()) {
		m_StringTableIndex = it->second;
	} else {
		g_StringList->push_back(src_string);
		m_StringTableIndex = (int)(g_StringList->size() - 1);
		(*g_StringTable)[src_string] = m_StringTableIndex;
	}
}

/// String::operator==
bool String::operator==(const String& Op2) const {
	return m_StringTableIndex == Op2.m_StringTableIndex;
}

/// String::operator==
bool String::operator==(const char* op2) const {
	return *this == String(op2);
}

/// String::operator!=
bool String::operator!=(const String& Op2) const {
	return m_StringTableIndex != Op2.m_StringTableIndex;
}

/// String::operator=
String& String::operator=(const String& Op2) {
	m_StringTableIndex = Op2.m_StringTableIndex;
	return *this;
}


/// String::stl_str
const std::string& String::stl_str() const {
	if (m_StringTableIndex < 0 || m_StringTableIndex >= g_StringList->size()) {
		return *g_EmptyString;
	}

	return (*g_StringList)[m_StringTableIndex];
}

/// String::c_str
const char* String::c_str() const {
	if (m_StringTableIndex < 0 || m_StringTableIndex >= g_StringList->size()) {
		return g_EmptyString->c_str();
	}

	return (*g_StringList)[m_StringTableIndex].c_str();
}
