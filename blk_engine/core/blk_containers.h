/// containers.h
///
/// 2016 blk

#pragma once

#include "blk_core.h"

/// blk
namespace blk {
	/// std_remove_swap
	template<typename T, typename B>
	void std_remove_swap(T& list, B entry) {
		list.erase(std::remove(list.begin(), list.end(), entry), list.end());
	}

	/// std_remove_idx_swap
	template<typename T>
	void std_remove_idx_swap(T& list, const int i) {
		std::swap(list[i], list.back()); list.pop_back();
	}

	/// std_contains
	template<typename T, typename B>
	bool std_contains(const T& list, B entry) {
		return std::find(list.begin(), list.end(), entry) != list.end();
	}

	inline bool std_contains(const std::string& src, const char* const sub_str) {
		return src.find(sub_str) != src.npos;
	}

	/// std_find
	template<typename T, typename B>
	auto std_find(const T& list, B entry) {
		return std::find(list.begin(), list.end(), entry);
	}
}