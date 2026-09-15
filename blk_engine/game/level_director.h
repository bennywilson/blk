//===================================================================================================
// level_director.h
//
// 2019 blk
//===================================================================================================
#pragma once


/// LevelDirector
template<typename T, typename C>
class LevelDirector : public IStateMachine<T, C> {

//---------------------------------------------------------------------------------------------------
public:
	LevelDirector() {
	}

	virtual ~LevelDirector() {}

	virtual void UpdateStateMachine() { IStateMachine<T, C>::UpdateStateMachine(); }
};