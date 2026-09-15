/// GameEntityHeader.h
///
/// 2016 blk

#pragma once

void CopyVarToComponent(const class Component* Src, class Component* Dst, const class TypeInfoVar* currentVar);

#define BLK_DEFINE_CLASS(className) \
	class className##_TypeInfo className::typeInfo; \
	std::vector<class TypeInfoClass*> className::className##_TypeInfoVar; \

#define BLK_DECLARE_COMPONENT(className, parentClassName) \
public: \
	className(const className&) = default; \
	className(className&&) = default; \
	className& operator=(const className&) = default; \
	className& operator=(className&&) = default; \
private: \
	void Constructor(); \
	typedef parentClassName Super; \
	virtual void CollectAncestorTypeInfo() { CollectAncestorTypeInfo_Internal( className##_TypeInfoVar ); } \
	friend class className##_TypeInfo; \
	static className##_TypeInfo typeInfo; \
	static std::vector< class TypeInfoClass * > className##_TypeInfoVar; \
protected: \
	virtual void CollectAncestorTypeInfo_Internal( std::vector< class TypeInfoClass * > & collection ) { Super::CollectAncestorTypeInfo_Internal( collection ); collection.push_back( ( TypeInfoClass * )( &typeInfo ) ); } \
public: \
	className() { Constructor(); if ( GetTypeInfo().size() == 0 ) { CollectAncestorTypeInfo(); }} \
	/* className( const className & componentToCopy );*/ \
	virtual const char * GetComponentClassName() const { return #className; } \
	virtual const std::vector< class TypeInfoClass * > & GetTypeInfo() const { return className##_TypeInfoVar; } \
	virtual bool IsA( const void *const type ) const { if ( type != (TypeInfoClass*)( &typeInfo ) ) { return Super::IsA( type ); } else { return true; } } \
	template<typename T> \
	T* GetAs() { if ( IsA( T::GetType() ) == false ) { return nullptr; } return (T*)this; } \
	const static className##_TypeInfo * GetType() { return &typeInfo; } \
	virtual Component * Duplicate() const { return new className( *this ); }

#include "component.h"
#include "render_component.h"
#include "breakable_component.h"
#include "model_component.h"
#include "particle_component.h"
#include "terrain_component.h"
#include "collision_manager.h"
#include "light_component.h"
#include "cloth_component.h"
#include "level_component.h"
#include "ui_component.h"
#include "sound_component.h"
#include "debug_component.h"
#include "gaussian_splat.h"
#include "type_info.h"

#define BLK_DEFINE_COMPONENT( className )
