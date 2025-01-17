#include "FCTMapWrap.h"
#include "Containers/Map.h"
#include "FCTemplateType.h"

#include "FCObjectManager.h"
#include "FCGetObj.h"
#include "FCRunTimeRegister.h"
#include "FCTMapIteratorWrap.h"
#include "FTMapKeyValueBuffer.h"
#include "../LuaCore/LuaContext.h"
#include "FCTMapHelper.h"
#include "FCCallScriptFunc.h"

void FCTMapWrap::Register(lua_State* L)
{
	luaL_requiref(L, "TMap", LibOpen_wrap, 1);
    luaL_requiref(L, "MapProperty", LibOpen_wrap, 1);
    SetWrapClassName("TMap");
    SetWrapClassName("MapProperty");
}

int FCTMapWrap::LibOpen_wrap(lua_State* L)
{
	const LuaRegFunc LibFuncs[] =
	{
		{ "GetNumb", GetNumb_wrap },
		{ "Add", Add_wrap },
		{ "Remove", Remove_wrap },
		{ "Clear", Clear_wrap },
		{ "ToMap", ToMap_wrap },
		{ "SetMap", SetMap_wrap },
		{ "Get", GetIndex_wrap },
		{ "Set", SetIndex_wrap },
		{ "Length", GetNumb_wrap },
		{ "size", GetNumb_wrap },
		{ "push_back", Add_wrap },
		{ "begin", begin_wrap },
		{ "find", find_wrap },

		{ "__gc", FCExportedClass::obj_Delete },
		{ "__call", obj_new },
		{ "__eq", FCExportedClass::obj_equal },
        { "__pairs", obj_pairs },
        { "__len", GetNumb_wrap },
		{ nullptr, nullptr }
	};
    const char* ClassName = lua_tostring(L, 1);
	FCExportedClass::RegisterLibClass(L, ClassName, LibFuncs);
	return 1;
}

// pair 的生命周期要短于Map对象，所以可以这里面直接保存TMap吧
struct FCTMap_Pairs : public FCTMapHelper
{
    FCObjRef *ObjRef;
    int    PairIndex; // TMap第一个有效的是0

    FCDynamicProperty  KeyProperty;
    FCDynamicProperty  ValueProperty;
    FCTMap_Pairs(FCObjRef* InObjRef):FCTMapHelper(InObjRef), ObjRef(InObjRef), PairIndex(-1)
    {        
    }
    void InitProperty()
    {
        if(IsValid())
        {
            const FScriptMapLayout& MapLayout = MapProperty->MapLayout;

            KeyProperty.InitProperty(MapProperty->KeyProp);
            ValueProperty.InitProperty(MapProperty->ValueProp);
        }        
    }
};


int FCTMapWrap::pair_gc(lua_State* L)
{
    FCTMap_Pairs *Pairs = (FCTMap_Pairs *)lua_touserdata(L, 1);
    if(Pairs)
    {
        Pairs->~FCTMap_Pairs(); // 只需要执行析构就行了
    }
    return 0;
}

int FCTMapWrap::obj_NextPairs(lua_State* L)
{
    // for k, v in pairs(t) do
    FCTMap_Pairs* Pairs = (FCTMap_Pairs*)lua_touserdata(L, 1);
    if (Pairs->IsValid())
    {
        int NextIndex = FCTMapIteratorWrap::ToNextValidIterator(Pairs->ScriptMap, Pairs->PairIndex + 1);
        Pairs->PairIndex = NextIndex;
        if(Pairs->IsValidIndex(NextIndex))
        {
            FMapProperty* MapProperty = Pairs->ObjRef->DynamicProperty->SafePropertyPtr->CastMapProperty();
            const FScriptMapLayout& MapLayout = MapProperty->MapLayout;

            uint8* PairPtr = (uint8*)Pairs->ScriptMap->GetData(NextIndex, MapLayout);
            uint8* KeyAddr = PairPtr;
            Pairs->KeyProperty.m_WriteScriptFunc(L, &Pairs->KeyProperty, KeyAddr, nullptr, nullptr);  // 只可以引用，不可修改

            uint8* ValueAddr = PairPtr + MapLayout.ValueOffset;
            Pairs->ValueProperty.m_WriteScriptFunc(L, &Pairs->ValueProperty, ValueAddr, nullptr, nullptr);  // 这里只要引用，不可以修改
            return 2;
        }
    }
    return 0;
}

int FCTMapWrap::obj_pairs(lua_State* L)
{
    FCObjRef* ObjRef = (FCObjRef*)FCScript::GetObjRefPtr(L, 1);
    lua_pushcfunction(L, obj_NextPairs);
    FCTMap_Pairs* Pairs = (FCTMap_Pairs*)lua_newuserdata(L, sizeof(FCTMap_Pairs));
    Pairs = new (Pairs)FCTMap_Pairs(ObjRef);
    Pairs->InitProperty();

    lua_newtable(L);
    lua_pushcfunction(L, pair_gc);
    lua_setfield(L, -2, "__gc");
    lua_setmetatable(L, -2);
    lua_pushnil(L);

    return 3;
}

int FCTMapWrap::obj_new(lua_State* L)
{
	// FScriptArray *ScriptArray = new FScriptArray;
	// 这个还是不要让动态构建的好了
	// 因为不管怎么样，就算是相同的，也是需要拷贝的
    const char* KeyTypeName = GetPropertyType(L, 2);
    const char* ValueTypeName = GetPropertyType(L, 3);
	FCDynamicProperty *DynamicProperty = GetTMapDynamicProperty(KeyTypeName, ValueTypeName);
	if(DynamicProperty)
	{
		FScriptMap *ScriptMap = new FScriptMap();
		int64 ObjID = FCGetObj::GetIns()->PushTemplate(DynamicProperty, ScriptMap, EFCObjRefType::NewTMap);
		FCScript::PushBindObjRef(L, ObjID, "TMap");
	}
	else
	{
		lua_pushnil(L);
	}
	return 1;
}
int FCTMapWrap::GetNumb_wrap(lua_State* L)
{
	FCObjRef* ObjRef = (FCObjRef*)FCScript::GetObjRefPtr(L, 1);
	if (ObjRef && ObjRef->DynamicProperty && ObjRef->DynamicProperty->Type == FCPropertyType::FCPROPERTY_Map)
	{
		FScriptMap* ScriptMap = (FScriptMap*)ObjRef->GetThisAddr();
		int Num = ScriptMap->Num();
		lua_pushinteger(L, Num);
	}
	else
	{
		lua_pushinteger(L, 0);
	}
	return 1;
}

bool TMap_GetAt(FCObjRef* ObjRef, lua_State* L, int KeyIdx, int ValueIdx)
{
	FScriptMap* ScriptMap = (FScriptMap*)ObjRef->GetThisAddr();
	FMapProperty* MapProperty = ObjRef->DynamicProperty->SafePropertyPtr->CastMapProperty();
	FProperty* KeyProp = MapProperty->KeyProp;

	FTMapKeyValueBuffer KeyBuffer(KeyProp, L, KeyIdx);
	
	uint8 *ValueAddr = ScriptMap->FindValue(KeyBuffer.Buffer, MapProperty->MapLayout,			
			[KeyProp](const void* ElementKey) { return KeyProp->GetValueTypeHash(ElementKey); },
			[KeyProp](const void* A, const void* B) { return KeyProp->Identical(A, B); }
			);
	if(ValueAddr)
	{
		FCDynamicProperty* ElementProperty = GetDynamicPropertyByUEProperty(MapProperty->ValueProp);
		ElementProperty->m_WriteScriptFunc(L, ElementProperty, ValueAddr, nullptr, nullptr);
		return true;
	}
	return false;
}

void TMap_AddBase(FCObjRef* ObjRef, lua_State* L, FTMapKeyValueBuffer &Key, FTMapKeyValueBuffer &Value)
{
	FScriptMap* ScriptMap = (FScriptMap*)ObjRef->GetThisAddr();
	FMapProperty* MapProperty = ObjRef->DynamicProperty->SafePropertyPtr->CastMapProperty();
	FProperty* KeyProp = MapProperty->KeyProp;
	FProperty* ValueProp = MapProperty->ValueProp;
	
	void *KeyBuffer = Key.Buffer;
	void *ValueBuffer = Value.Buffer;
		
	ScriptMap->Add(Key.Buffer, Value.Buffer, MapProperty->MapLayout,
			[KeyProp](const void* ElementKey) { return KeyProp->GetValueTypeHash(ElementKey); },
			[KeyProp](const void* A, const void* B) { return KeyProp->Identical(A, B); }, // KeyEqualityFn
			[KeyProp, KeyBuffer](void* Dest) { KeyProp->InitializeValue(Dest); KeyProp->CopySingleValue(Dest, KeyBuffer); },      // KeyConstructAndAssignFn
			[ValueProp, ValueBuffer](void* Dest) { ValueProp->InitializeValue(Dest); ValueProp->CopySingleValue(Dest, ValueBuffer); },  // ValueConstructAndAssignFn
			[ValueProp, ValueBuffer](void* Dest) { ValueProp->CopySingleValue(Dest, ValueBuffer); },  // ValueAssignFn 
			[KeyProp](void* Dest) { KeyProp->DestroyValue(Dest); },			// DestructKeyFn
			[ValueProp](void* Dest) { ValueProp->DestroyValue(Dest); }		// DestructValueFn
			);
}

void TMap_Add(FCObjRef* ObjRef, lua_State* L, int KeyIdx, int ValueIdx)
{
	FScriptMap* ScriptMap = (FScriptMap*)ObjRef->GetThisAddr();
	FMapProperty* MapProperty = ObjRef->DynamicProperty->SafePropertyPtr->CastMapProperty();
	FTMapKeyValueBuffer Key(MapProperty->KeyProp, L, KeyIdx);
	FTMapKeyValueBuffer Value(MapProperty->ValueProp, L, ValueIdx);

	TMap_AddBase(ObjRef, L, Key, Value);
}

// Get(map, key, value)
int FCTMapWrap::GetIndex_wrap(lua_State* L)
{
    // value = map[key]
	FCObjRef* ObjRef = (FCObjRef*)FCScript::GetObjRefPtr(L, 1);
	if (ObjRef && ObjRef->DynamicProperty)
	{
		if (ObjRef->DynamicProperty->Type == FCPropertyType::FCPROPERTY_Map)
		{
			if (TMap_GetAt(ObjRef, L, 1, 2))
				return 1;
		}
	}
	lua_pushnil(L);
	return 1;
}

int FCTMapWrap::SetIndex_wrap(lua_State* L)
{
    // map[key] = value
	FCObjRef* ObjRef = (FCObjRef*)FCScript::GetObjRefPtr(L, 1);
	if (ObjRef && ObjRef->DynamicProperty)
	{
		if (ObjRef->DynamicProperty->Type == FCPropertyType::FCPROPERTY_Map)
		{
			TMap_Add(ObjRef, L, 2, 3);
		}
	}
	return 0;
}

int FCTMapWrap::Add_wrap(lua_State* L)
{
	FCObjRef* ObjRef = (FCObjRef*)FCScript::GetObjRefPtr(L, 1);
	if (ObjRef && ObjRef->DynamicProperty)
	{
		if (ObjRef->DynamicProperty->Type == FCPropertyType::FCPROPERTY_Map)
		{
			TMap_Add(ObjRef, L, 2, 3);
		}
	}
	return 0;
}

int FCTMapWrap::Remove_wrap(lua_State* L)
{
	FCObjRef* ObjRef = (FCObjRef*)FCScript::GetObjRefPtr(L, 1);
	if (ObjRef && ObjRef->DynamicProperty)
	{
		if (ObjRef->DynamicProperty->Type == FCPropertyType::FCPROPERTY_Map)
		{
			FScriptMap* ScriptMap = (FScriptMap*)ObjRef->GetThisAddr();
			FMapProperty* MapProperty = ObjRef->DynamicProperty->SafePropertyPtr->CastMapProperty();
			FProperty* KeyProp = MapProperty->KeyProp;
			FProperty* ValueProp = MapProperty->ValueProp;
	
			FTMapKeyValueBuffer Key(KeyProp, L, 2);
			void *KeyBuffer = Key.Buffer;
			const FScriptMapLayout &MapLayout = MapProperty->MapLayout;

			int32 PairIndex = ScriptMap->FindPairIndex(KeyBuffer, MapLayout,
				[KeyProp](const void* ElementKey) { return KeyProp->GetValueTypeHash(ElementKey); }, // GetKeyHash
				[KeyProp](const void* A, const void* B) { return KeyProp->Identical(A, B); } // KeyEqualityFn
				);
			if(ScriptMap->IsValidIndex(PairIndex))
			{
				uint8* PairPtr = (uint8*)ScriptMap->GetData(PairIndex, MapLayout);
				uint8* Result  = PairPtr + MapLayout.ValueOffset;
				KeyProp->DestroyValue(PairPtr);
				ValueProp->DestroyValue(Result);
				
				ScriptMap->RemoveAt(PairIndex, MapProperty->MapLayout);
			}
		}
	}
	return 0;
}

int FCTMapWrap::Clear_wrap(lua_State* L)
{
	FCObjRef* ObjRef = (FCObjRef*)FCScript::GetObjRefPtr(L, 1);
	if (ObjRef && ObjRef->DynamicProperty)
	{
		if (ObjRef->DynamicProperty->Type == FCPropertyType::FCPROPERTY_Map)
		{
			FScriptMap* ScriptMap = (FScriptMap*)ObjRef->GetThisAddr();
			FMapProperty* MapProperty = ObjRef->DynamicProperty->SafePropertyPtr->CastMapProperty();
			TMap_Clear(ScriptMap, MapProperty);
		}
	}
	return 0;
}

int FCTMapWrap::ToMap_wrap(lua_State* L)
{
	FCObjRef* ObjRef = (FCObjRef*)FCScript::GetObjRefPtr(L, 1);
	if (ObjRef && ObjRef->DynamicProperty)
	{
		if (ObjRef->DynamicProperty->Type == FCPropertyType::FCPROPERTY_Map)
		{
			FScriptMap* ScriptMap = (FScriptMap*)ObjRef->GetThisAddr();
			FMapProperty* MapProperty = ObjRef->DynamicProperty->SafePropertyPtr->CastMapProperty();
			FProperty* KeyProp = MapProperty->KeyProp;
			FProperty* ValueProp = MapProperty->ValueProp;
			const FScriptMapLayout &MapLayout = MapProperty->MapLayout;

			int32  MaxIndex = ScriptMap->GetMaxIndex();			
			FCDynamicProperty  KeyProperty, ValueProperty;
			KeyProperty.InitProperty(KeyProp);
			ValueProperty.InitProperty(ValueProp);

			lua_createtable(L, 0, 0);
			int nTableIdx = lua_gettop(L);

			for(int32 PairIndex = 0; PairIndex < MaxIndex; ++PairIndex)
			{
				if(ScriptMap->IsValidIndex(PairIndex))
				{
					uint8* PairPtr = (uint8*)ScriptMap->GetData(PairIndex, MapLayout);
					uint8* Result  = PairPtr + MapLayout.ValueOffset;
					KeyProperty.m_WriteScriptFunc(L, &KeyProperty, PairPtr, nullptr, nullptr);
					ValueProperty.m_WriteScriptFunc(L, &ValueProperty, Result, nullptr, nullptr);
					lua_rawset(L, -3);
				}
			}
			return 1;
		}
	}
	lua_pushnil(L);
	return 1;
}

struct SetMapCallbackInfo
{
	FCObjRef* ObjRef;
	FTMapKeyValueBuffer* KeyBuffer;
	FTMapKeyValueBuffer* ValueBuffer;
};

void  SetMap_Callback(lua_State* L, int Index, void* UserData)
{
	SetMapCallbackInfo* Info = (SetMapCallbackInfo*)UserData;

	Info->KeyBuffer->ReadScriptValue(L, -1);
	Info->ValueBuffer->ReadScriptValue(L, -2);
	TMap_AddBase(Info->ObjRef, L, *(Info->KeyBuffer), *(Info->ValueBuffer));
}

int FCTMapWrap::SetMap_wrap(lua_State* L)
{
	FCObjRef* ObjRef = (FCObjRef*)FCScript::GetObjRefPtr(L, 1);
	if (ObjRef && ObjRef->DynamicProperty)
	{
		if (ObjRef->DynamicProperty->Type == FCPropertyType::FCPROPERTY_Map)
		{
			int32 Type = lua_rawget(L, 2);
			if (Type != LUA_TTABLE)
			{
				return 0;
			}
			int TableIdx = 2;

			FScriptMap* ScriptMap = (FScriptMap*)ObjRef->GetThisAddr();
			FMapProperty* MapProperty = ObjRef->DynamicProperty->SafePropertyPtr->CastMapProperty();
			TMap_Clear(ScriptMap, MapProperty);

			FTMapKeyValueBuffer Key(MapProperty->KeyProp);
			FTMapKeyValueBuffer Value(MapProperty->ValueProp);
						
			SetMapCallbackInfo  Info = { ObjRef, &Key, &Value };
			LoopTable(L, TableIdx, SetMap_Callback, &Info);
		}
	}
	return 0;
}

int FCTMapWrap::begin_wrap(lua_State* L)
{
	FCObjRef* ObjRef = (FCObjRef*)FCScript::GetObjRefPtr(L, 1);
    if (ObjRef && ObjRef->DynamicProperty)
    {
        if (ObjRef->DynamicProperty->Type == FCPropertyType::FCPROPERTY_Map)
        {
            FScriptMap* ScriptMap = (FScriptMap*)ObjRef->GetThisAddr();
			TMapIterator  *Iterator = new TMapIterator();
			Iterator->MapInsID = ObjRef->PtrIndex;
			Iterator->Index = FCTMapIteratorWrap::ToNextValidIterator(ScriptMap, 0);
			int64 ItInsID = FCGetObj::GetIns()->PushMapIterator(Iterator);
			FCScript::PushBindObjRef(L, ItInsID, "TMapIterator");
			return 1;
		}
	}
	lua_pushnil(L);
	return 1;
}

int FCTMapWrap::find_wrap(lua_State* L)
{
	FCObjRef* ObjRef = (FCObjRef*)FCScript::GetObjRefPtr(L, 1);
    int64 ItInsID = 0;
    if (ObjRef && ObjRef->DynamicProperty)
    {
        if (ObjRef->DynamicProperty->Type == FCPropertyType::FCPROPERTY_Map)
        {
            FScriptMap* ScriptMap = (FScriptMap*)ObjRef->GetThisAddr();
            FMapProperty* MapProperty = ObjRef->DynamicProperty->SafePropertyPtr->CastMapProperty();
            FProperty* KeyProp = MapProperty->KeyProp;

            FTMapKeyValueBuffer Key(KeyProp, L, 2);
            void* KeyBuffer = Key.Buffer;
            const FScriptMapLayout& MapLayout = MapProperty->MapLayout;

            int32 PairIndex = ScriptMap->FindPairIndex(KeyBuffer, MapLayout,
                [KeyProp](const void* ElementKey) { return KeyProp->GetValueTypeHash(ElementKey); }, // GetKeyHash
                [KeyProp](const void* A, const void* B) { return KeyProp->Identical(A, B); } // KeyEqualityFn
            );
            if (ScriptMap->IsValidIndex(PairIndex))
            {
                TMapIterator* Iterator = new TMapIterator();
                Iterator->MapInsID = ObjRef->PtrIndex;
                Iterator->Index = FCTMapIteratorWrap::ToNextValidIterator(ScriptMap, 0);
                ItInsID = FCGetObj::GetIns()->PushMapIterator(Iterator);
            }
        }
    }
	if (ItInsID)
	{
		FCScript::PushBindObjRef(L, ItInsID, "TMapIterator");
	}
	else
	{
		lua_pushnil(L);
	}
	return 1;
}