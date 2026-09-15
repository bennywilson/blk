/// blk_string.h
///
/// 2016 blk

#pragma once

#define INVALID_STRING_INDEX -1

///  String stores an index into a global string table for fast look ups
class String {
public:
	String() { m_StringTableIndex = INVALID_STRING_INDEX; }
	String(const std::string& src);
	String(const char* const src);

	bool operator==(const String& Op2) const;
	bool operator==(const char* string) const;

	bool operator!=(const String& Op2) const;

	String& operator=(const String& Op2);

	bool operator<(const String& op2) const { return stl_str() < op2.stl_str(); }

	bool IsEmptyString() const { return c_str()[0] == '\0'; }

	int GetStringTableIndex() const { return m_StringTableIndex; }
	size_t GetLength() const { return stl_str().length(); }

	const std::string& stl_str() const;
	const char* c_str() const;

	static void ShutDown();

	static String EmptyString;

private:
	int m_StringTableIndex;
};

/// StringHash
struct StringHash {
	size_t operator()(const String& key) const {
		const size_t hash = (size_t)key.GetStringTableIndex();
		return hash;
	}
};
