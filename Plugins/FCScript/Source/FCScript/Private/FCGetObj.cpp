#include "FCGetObj.h"
#include "FCTemplateType.h"
#include "FCCallScriptFunc.h"
#include "FCObjectUseFlag.h"

FCGetObj* FCGetObj::s_Ins = nullptr;

FCGetObj::FCGetObj():m_nObjID(0)
{
	FCGetObj::s_Ins = this;

	m_CppProperty.ElementSize = sizeof(void*);
	m_CppProperty.m_ReadScriptFunc = ReadScriptCppPtr;
	m_CppProperty.m_WriteScriptFunc = PushScriptCppPtr;

	m_MapIteratorProperty.ElementSize = sizeof(TMapIterator);
	m_MapIteratorProperty.m_ReadScriptFunc = ReadScriptMapIterator;
	m_MapIteratorProperty.m_WriteScriptFunc = PushScriptMapIterator;
}

FCGetObj::~FCGetObj()
{
	if(FCGetObj::s_Ins == this)
	{
		FCGetObj::s_Ins = nullptr;
	}
}

void FCGetObj::Clear()
{
	while(m_IntPtrMap.size() >0)
	{
        CIntPtr2RefObjMap::iterator itObj = m_IntPtrMap.begin();
        FCObjRef* ObjRef = itObj->second;
		ObjRef->Ref = 1;
        ReleaseObjRef(ObjRef);
	}	
    if(m_ObjMap.size() > 0)
    {
        FC_ASSERT(true)
        m_ObjMap.clear();
    }
	m_nObjID = 0;
}

void FCGetObj::SetAllObjRefFlag()
{
    for(CIntPtr2RefObjMap::iterator itObjRef = m_IntPtrMap.begin(); itObjRef != m_IntPtrMap.end(); ++itObjRef)
    {
        FCObjRef* ObjRef = itObjRef->second;
        SetPtrRefFlag(ObjRef->DynamicProperty);
        SetPtrRefFlag(ObjRef->ClassDesc);
    }
}

int64  FCGetObj::PushUObject(UObject *Obj)
{
	FCObjRef  *ObjRef = PushUObjectNoneRef(Obj);
	if(ObjRef)
	{
		++(ObjRef->Ref);
		return ObjRef->PtrIndex;
	}
	else
	{
		return 0;
	}
}

FCObjRef* FCGetObj::PushUObjectNoneRef(UObject* Obj)
{
	if (!Obj)
	{
		return nullptr;
	}
	ObjRefKey  ObjKey(nullptr, Obj);
	CScriptRefObjMap::iterator itObj = m_ObjMap.find(ObjKey);
	if (itObj != m_ObjMap.end())
	{
		return itObj->second;
	}
    UClass *Class = Cast<UClass>(Obj);
    Class = Class ? Class : Obj->GetClass();
	FCScriptContext* ScriptContext = GetScriptContext();
	FCDynamicClassDesc* ClassDesc = ScriptContext->RegisterUStruct(Class);

	FCObjRef* ObjRef = NewObjRef();
	ObjRef->Ref = 0;
	ObjRef->RefType = EFCObjRefType::RefObject;
	ObjRef->Parent = nullptr;
	ObjRef->ClassDesc = ClassDesc;
	ObjRef->PtrIndex = ++m_nObjID;
	ObjRef->ThisObjAddr = (uint8 *)Obj;
    ObjRef->DynamicProperty = GetDynamicPropertyByCppType(FCPROPERTY_ObjectProperty, "UObject", sizeof(UObject*));
	m_ObjMap[ObjKey] = ObjRef;
	m_IntPtrMap[ObjRef->PtrIndex] = ObjRef;

    FCObjectUseFlag::GetIns().Ref(Obj);
#ifdef UE_BUILD_DEBUG
    FCStringBuffer128   TempBuffer;
    FString  ObjName = Obj->GetName();
    TempBuffer << "UObject:" << TCHAR_TO_UTF8(*ObjName);
    ObjRef->DebugDesc = GetConstName(TempBuffer.GetString());
#endif
	return ObjRef;
}

int64  FCGetObj::PushNewObject(FCDynamicClassDesc* ClassDesc, const FName& Name, UObject* Outer)
{
	UClass  *Class = ClassDesc->m_Class;
	UObject* Obj = nullptr;

	FCObjRef* ObjRef = NewObjRef();
	ObjRef->Ref = 1;
	ObjRef->RefType = EFCObjRefType::NewUObject;
	//ObjRef->Parent = Outer;
	ObjRef->Parent = nullptr;
	ObjRef->ClassDesc = ClassDesc;
	ObjRef->PtrIndex = ++m_nObjID;
	ObjRef->ThisObjAddr = (uint8 *)Obj;
	m_IntPtrMap[ObjRef->PtrIndex] = ObjRef;

    // 后创建对象
#if OLD_UE_ENGINE
    Obj = StaticConstructObject_Internal(Class, Outer, Name);
#else
    FStaticConstructObjectParameters ObjParams(Class);
    ObjParams.Outer = Outer;
    ObjParams.Name = Name;
    Obj = StaticConstructObject_Internal(ObjParams);
#endif

    if (Obj)
    {
		ObjRefKey  ObjKey(nullptr, Obj);
        ObjRef->ThisObjAddr = (uint8 *)Obj;
        m_ObjMap[ObjKey] = ObjRef;
    }
    FCObjectUseFlag::GetIns().Ref(Obj);
#ifdef UE_BUILD_DEBUG
    FCStringBuffer128   TempBuffer;
    FString  ObjName = Obj->GetName();
    TempBuffer << "UObject:" << TCHAR_TO_UTF8(*ObjName);
    ObjRef->DebugDesc = GetConstName(TempBuffer.GetString());
#endif

	return ObjRef->PtrIndex;
}

int64  FCGetObj::PushNewStruct(FCDynamicClassDesc* ClassDesc)
{
	UStruct  *Struct = ClassDesc->m_Struct;
	int ValueSize = Struct->GetStructureSize();
	void *pValueAddr = NewStructBuffer(ValueSize);
	Struct->InitializeStruct(pValueAddr, 1);

	FCObjRef* ObjRef = NewObjRef();
	ObjRef->Ref = 1;
	ObjRef->RefType = EFCObjRefType::NewUStruct;
	ObjRef->Parent = nullptr;
	ObjRef->ClassDesc = ClassDesc;
	ObjRef->PtrIndex = ++m_nObjID;
	ObjRef->ThisObjAddr = (uint8 *)pValueAddr;
	ObjRef->DynamicProperty = GetStructDynamicProperty(Struct);

	ObjRefKey  ObjKey(nullptr, pValueAddr);
	m_ObjMap[ObjKey] = ObjRef;
	m_IntPtrMap[ObjRef->PtrIndex] = ObjRef;
#ifdef UE_BUILD_DEBUG
    ObjRef->DebugDesc = ClassDesc->m_UEClassName;
#endif
	return ObjRef->PtrIndex;
}

int64  FCGetObj::PushCppStruct(FCDynamicClassDesc* ClassDesc, void* pValueAddr)
{
    FCObjRef* ObjRef = NewObjRef();
    ObjRef->Ref = 1;
    ObjRef->RefType = EFCObjRefType::NewCppStruct;
    ObjRef->Parent = nullptr;
    ObjRef->ClassDesc = ClassDesc;
    ObjRef->PtrIndex = ++m_nObjID;
    ObjRef->ThisObjAddr = (uint8*)pValueAddr;
    ObjRef->DynamicProperty = nullptr;

    ObjRefKey  ObjKey(nullptr, pValueAddr);
    m_ObjMap[ObjKey] = ObjRef;
    m_IntPtrMap[ObjRef->PtrIndex] = ObjRef;
#ifdef UE_BUILD_DEBUG
    ObjRef->DebugDesc = ClassDesc->m_UEClassName;
#endif
    return ObjRef->PtrIndex;    
}

int64  FCGetObj::PushProperty(UObject *Parent, const FCDynamicProperty *DynamicProperty, void *pValueAddr)
{
	if(!Parent)
	{
		return 0;
	}
	FCObjRef *ParentRef = PushUObjectNoneRef(Parent);
	return PushChildProperty(ParentRef, DynamicProperty, pValueAddr);
}

int64  FCGetObj::PushTempRefProperty(const FCDynamicProperty* DynamicProperty, void* pValueAddr)
{
    int64 ObjID = PushChildProperty(nullptr, DynamicProperty, pValueAddr);
    m_TempObjIDList.push_back(ObjID);
    return ObjID;
}

int64  FCGetObj::PushChildProperty(FCObjRef* Parent, const FCDynamicProperty* DynamicProperty, void* pValueAddr)
{
	uint8  *ParentAddr = Parent ? Parent->GetThisAddr() : nullptr;
	ObjRefKey  ObjKey(ParentAddr, (const void*)(pValueAddr));

	CScriptRefObjMap::iterator itObj = m_ObjMap.find(ObjKey);
	if (itObj != m_ObjMap.end())
	{
		++(itObj->second->Ref);
		return itObj->second->PtrIndex;
	}
	uint64 CastFlags = DynamicProperty->SafePropertyPtr->GetCastFlags();
    FCScriptContext* ScriptContext = GetScriptContext();
	FCDynamicClassDesc* ClassDesc = NULL;
	if (CASTCLASS_FStructProperty & CastFlags)
	{
        UStruct* PropertyStruct = DynamicProperty->SafePropertyPtr->GetPropertyStruct();
		ClassDesc = ScriptContext->RegisterUStruct(PropertyStruct);
	}
    else
    {
        ClassDesc = ScriptContext->RegisterByProperty((FProperty*)DynamicProperty->SafePropertyPtr->GetProperty());
    }

	FCObjRef* ObjRef = NewObjRef();
	ObjRef->Ref = 1;
	ObjRef->RefType = EFCObjRefType::RefProperty;
	ObjRef->Parent = nullptr;
	ObjRef->ClassDesc = ClassDesc;
	ObjRef->PtrIndex = ++m_nObjID;
	ObjRef->DynamicProperty = (FCDynamicProperty*)DynamicProperty;
	ObjRef->ThisObjAddr = (uint8*)pValueAddr;
	m_ObjMap[ObjKey] = ObjRef;
	m_IntPtrMap[ObjRef->PtrIndex] = ObjRef;

    if(Parent)
    {
        ++(Parent->Ref);
        ObjRef->Parent = Parent;
        Parent->PushChild(ObjRef);
    }
#ifdef UE_BUILD_DEBUG
    ObjRef->DebugDesc = DynamicProperty->DebugDesc;
#endif
	return ObjRef->PtrIndex;
}

int64  FCGetObj::PushStructValue(const FCDynamicProperty *DynamicProperty, void *pValueAddr)
{
	ObjRefKey  ObjKey(nullptr, pValueAddr);
	CScriptRefObjMap::iterator itObj = m_ObjMap.find(ObjKey);
	if (itObj != m_ObjMap.end())
	{
		++(itObj->second->Ref);
		return itObj->second->PtrIndex;
	}
	void *SrcValueAddr = pValueAddr;
	// FStructPropertyDesc
	// 这种没有爹的，就拷贝一个吧
	UStruct* Struct = DynamicProperty->SafePropertyPtr->GetPropertyStruct();
    int ValueSize = Struct->GetStructureSize();
    int ArrayDim = DynamicProperty->SafePropertyPtr->ArrayDim;
	pValueAddr = NewStructBuffer(ValueSize);
	Struct->InitializeStruct(pValueAddr, ArrayDim);
    DynamicProperty->SafePropertyPtr->CopyValuesInternal(pValueAddr, SrcValueAddr, ArrayDim);
	
	ObjKey = ObjRefKey(nullptr, pValueAddr);
	FCScriptContext *ScriptContext = GetScriptContext();
	FCDynamicClassDesc  *ClassDesc = ScriptContext->RegisterUStruct(Struct);

	FCObjRef  *ObjRef = NewObjRef();
	ObjRef->Ref = 1;
	ObjRef->RefType = EFCObjRefType::RefStructValue;
	ObjRef->Parent = nullptr;
	ObjRef->ClassDesc = ClassDesc;
	ObjRef->PtrIndex = ++m_nObjID;
	ObjRef->DynamicProperty = (FCDynamicProperty *)DynamicProperty;
	ObjRef->ThisObjAddr = (uint8* )pValueAddr;
	m_ObjMap[ObjKey] = ObjRef;
	m_IntPtrMap[ObjRef->PtrIndex] = ObjRef;

#ifdef UE_BUILD_DEBUG
    ObjRef->DebugDesc = DynamicProperty->DebugDesc;
#endif
	return ObjRef->PtrIndex;
}


int64  FCGetObj::PushNewTArray(const FCDynamicProperty* DynamicProperty, void* pValueAddr)
{
    FScriptArray* DesContent = new FScriptArray();
    int64 ObjID = PushTemplate(DynamicProperty, DesContent, EFCObjRefType::NewTArray);

    // 拷贝吧
    FScriptArray* SrcContent = (FScriptArray*)pValueAddr;
    DynamicProperty->SafePropertyPtr->CopyValuesInternal(DesContent, SrcContent, DynamicProperty->SafePropertyPtr->ArrayDim);
    return ObjID;
}

int64  FCGetObj::PushNewTMap(const FCDynamicProperty* DynamicProperty, void* pValueAddr)
{
    FScriptMap* DesContent = new FScriptMap();
    int64 ObjID = PushTemplate(DynamicProperty, DesContent, EFCObjRefType::NewTMap);

    // 拷贝吧
    FScriptMap* SrcContent = (FScriptMap*)pValueAddr;
    DynamicProperty->SafePropertyPtr->CopyValuesInternal(DesContent, SrcContent, DynamicProperty->SafePropertyPtr->ArrayDim);
    return ObjID;

}

int64  FCGetObj::PushNewTSet(const FCDynamicProperty* DynamicProperty, void* pValueAddr)
{
    FScriptSet* DesContent = new FScriptSet();
    int64 ObjID = PushTemplate(DynamicProperty, DesContent, EFCObjRefType::NewTSet);

    // 拷贝吧
    FScriptSet* SrcContent = (FScriptSet*)pValueAddr;
    DynamicProperty->SafePropertyPtr->CopyValuesInternal(DesContent, SrcContent, DynamicProperty->SafePropertyPtr->ArrayDim);

    return ObjID;
}
int64  FCGetObj::PushCppPropery(const FCDynamicProperty* DynamicProperty, void* pValueAddr)
{
	ObjRefKey  ObjKey(nullptr, pValueAddr);
	CScriptRefObjMap::iterator itObj = m_ObjMap.find(ObjKey);
	if (itObj != m_ObjMap.end())
	{
		++(itObj->second->Ref);
		return itObj->second->PtrIndex;
	}
    void* SrcValueAddr = pValueAddr;
    // FStructPropertyDesc
    // 这种没有爹的，就拷贝一个吧
    const FCSafeProperty* PropertyPtr = DynamicProperty->SafePropertyPtr;
    int ValueSize = PropertyPtr->ElementSize;
    int ArrayDim = PropertyPtr->ArrayDim;
    pValueAddr = NewStructBuffer(ValueSize);
    PropertyPtr->InitializeValue(pValueAddr);
    PropertyPtr->CopySingleValue(pValueAddr, SrcValueAddr);


    FCScriptContext* ScriptContext = GetScriptContext();

    FCObjRef* ObjRef = NewObjRef();
    ObjRef->Ref = 1;
    ObjRef->RefType = EFCObjRefType::NewProperty;
    ObjRef->Parent = nullptr;
    ObjRef->ClassDesc = nullptr;
    ObjRef->PtrIndex = ++m_nObjID;
    ObjRef->DynamicProperty = (FCDynamicProperty*)DynamicProperty;
    ObjRef->ThisObjAddr = (uint8*)pValueAddr;
    m_ObjMap[ObjKey] = ObjRef;
    m_IntPtrMap[ObjRef->PtrIndex] = ObjRef;

#ifdef UE_BUILD_DEBUG
    ObjRef->DebugDesc = DynamicProperty->DebugDesc;
#endif
    return ObjRef->PtrIndex;
}

int64  FCGetObj::PushTemplate(const FCDynamicProperty *DynamicProperty, void *pValueAddr, EFCObjRefType RefType)
{	
	ObjRefKey  ObjKey(nullptr, pValueAddr);
	CScriptRefObjMap::iterator itObj = m_ObjMap.find(ObjKey);
	if (itObj != m_ObjMap.end())
	{
		++(itObj->second->Ref);
		return itObj->second->PtrIndex;
	}

	FCScriptContext *ScriptContext = GetScriptContext();

	FCObjRef  *ObjRef = NewObjRef();
	ObjRef->Ref = 1;
	ObjRef->RefType = RefType;
	ObjRef->Parent = nullptr;
	ObjRef->ClassDesc = nullptr;
	ObjRef->PtrIndex = ++m_nObjID;
	ObjRef->DynamicProperty = (FCDynamicProperty *)DynamicProperty;
	ObjRef->ThisObjAddr = (uint8 *)pValueAddr;
	m_ObjMap[ObjKey] = ObjRef;
	m_IntPtrMap[ObjRef->PtrIndex] = ObjRef;

#ifdef UE_BUILD_DEBUG
    ObjRef->DebugDesc = DynamicProperty->DebugDesc;
#endif
	return ObjRef->PtrIndex;
}

int64  FCGetObj::PushCppPtr(void* CppPtr)
{
	ObjRefKey  ObjKey(nullptr, CppPtr);
	CScriptRefObjMap::iterator itObj = m_ObjMap.find(ObjKey);
	if (itObj != m_ObjMap.end())
	{
		++(itObj->second->Ref);
		return itObj->second->PtrIndex;
	}

	FCScriptContext* ScriptContext = GetScriptContext();

	FCObjRef* ObjRef = NewObjRef();
	ObjRef->Ref = 1;
	ObjRef->RefType = EFCObjRefType::CppPtr;
	ObjRef->Parent = nullptr;
	ObjRef->ClassDesc = nullptr;
	ObjRef->PtrIndex = ++m_nObjID;
	ObjRef->DynamicProperty = &m_CppProperty;
	ObjRef->ThisObjAddr = (uint8*)CppPtr;
	m_ObjMap[ObjKey] = ObjRef;
	m_IntPtrMap[ObjRef->PtrIndex] = ObjRef;
	return ObjRef->PtrIndex;
}

int64  FCGetObj::PushMapIterator(void* IteratorPtr)
{
    ObjRefKey  ObjKey(nullptr, IteratorPtr);
    CScriptRefObjMap::iterator itObj = m_ObjMap.find(ObjKey);
    if (itObj != m_ObjMap.end())
    {
        ++(itObj->second->Ref);
        return itObj->second->PtrIndex;
    }

    FCScriptContext* ScriptContext = GetScriptContext();

    FCObjRef* ObjRef = NewObjRef();
    ObjRef->Ref = 1;
    ObjRef->RefType = EFCObjRefType::MapIterator;
    ObjRef->Parent = nullptr;
    ObjRef->ClassDesc = nullptr;
    ObjRef->PtrIndex = ++m_nObjID;
    ObjRef->DynamicProperty = &m_MapIteratorProperty;
    ObjRef->ThisObjAddr = (uint8*)IteratorPtr;
    m_ObjMap[ObjKey] = ObjRef;
    m_IntPtrMap[ObjRef->PtrIndex] = ObjRef;
    return ObjRef->PtrIndex;
}

int64  FCGetObj::PushLuaDelegate(const FCDelegateInfo* DelegateInfo)
{
    ObjRefKey  ObjKey(nullptr, DelegateInfo);
    CScriptRefObjMap::iterator itObj = m_ObjMap.find(ObjKey);
    if (itObj != m_ObjMap.end())
    {
        ++(itObj->second->Ref);
        return itObj->second->PtrIndex;
    }

    FCScriptContext* ScriptContext = GetScriptContext();

    FCObjRef* ObjRef = NewObjRef();
    ObjRef->Ref = 1;
    ObjRef->RefType = EFCObjRefType::LuaDelegate;
    ObjRef->Parent = nullptr;
    ObjRef->ClassDesc = nullptr;
    ObjRef->PtrIndex = ++m_nObjID;
    ObjRef->DynamicProperty = &m_MapIteratorProperty;
    ObjRef->ThisObjAddr = (uint8*)DelegateInfo;
    m_ObjMap[ObjKey] = ObjRef;
    m_IntPtrMap[ObjRef->PtrIndex] = ObjRef;
    return ObjRef->PtrIndex;
}

void   FCGetObj::NotifyDeleteUObject(const class UObjectBase* Object, int32 Index)
{
	ObjRefKey  ObjKey(nullptr, Object);
	CScriptRefObjMap::iterator itObj = m_ObjMap.find(ObjKey);
	if (itObj != m_ObjMap.end())
	{
		FCObjRef *ObjRef = itObj->second;
		DestroyChildRef(ObjRef);
	}
}

void  FCGetObj::ClearTempIDList(int nStart)
{
    if(nStart < 0)
        nStart = 0;
    while(m_TempObjIDList.size() > nStart)
    {
        int64 ObjID = m_TempObjIDList.back();
        m_TempObjIDList.pop_back();
        FCObjRef *ObjRef = FindValue(ObjID);
        if(ObjRef)
        {
            ObjRef->Ref = 1;    // 强制删除
            ReleaseObjRef(ObjRef);
        }
    }
}

void   FCGetObj::DeleteValue(int64 ObjID)
{
	ReleaseValue(ObjID);
}

void   FCGetObj::ReleaseValue(int64 ObjID)
{
	CIntPtr2RefObjMap::iterator itObj = m_IntPtrMap.find(ObjID);
	if (itObj != m_IntPtrMap.end())
	{
		FCObjRef* ObjRef = itObj->second;
		ReleaseObjRef(ObjRef);
	}
}

void   FCGetObj::ReleaseCacheRef(int64 ObjID, int nCacheRef)
{
	CIntPtr2RefObjMap::iterator itObj = m_IntPtrMap.find(ObjID);
	if (itObj != m_IntPtrMap.end())
	{
		FCObjRef* ObjRef = itObj->second;
		ObjRef->Ref -= nCacheRef - 1;
		ReleaseObjRef(ObjRef);
	}
}

FCObjRef  *FCGetObj::NewObjRef()
{
	return new FCObjRef();
}

void  FCGetObj::ReleaseObjRef(FCObjRef* ObjRef)
{
    ObjRef->Ref--;
    if (ObjRef->Ref <= 0)
    {
        FCObjRef* ParentRef = ObjRef->Parent;
        if (ParentRef)
        {
            ParentRef->EraseChild(ObjRef);
        }
        DestroyChildRef(ObjRef);

		// 减少父对象的引用计数(递归)
		if(ParentRef)
		{
			ReleaseObjRef(ParentRef);
		}
    }
}

void  FCGetObj::DestroyChildRef(FCObjRef* ObjRef)
{
	// 父节点没有，儿子也没有必要留下, 儿子的儿子也不能留, 斩草要除根, 一脉不留存
	while (ObjRef->Childs)
	{
		FCObjRef* ChildPtr = ObjRef->Childs;
        ObjRef->Childs = ObjRef->Childs->m_pNext;
		DestroyChildRef(ChildPtr);
	}
	DestroyObjRef(ObjRef);
}

void  FCGetObj::DestroyObjRef(FCObjRef *ObjRef)
{
	if(ObjRef)	
	{
		ObjRefKey ObjKey = ObjRef->GetRefKey();
		m_ObjMap.erase(ObjKey);
		m_IntPtrMap.erase(ObjRef->PtrIndex);
        FC_ASSERT(m_ObjMap.size() != m_IntPtrMap.size())

		switch(ObjRef->RefType)
		{
			case EFCObjRefType::NewUObject:
            case EFCObjRefType::RefObject:
			{
                GetScriptContext()->m_ManualObjectReference->Remove(ObjRef->GetUObject());
			}
			break;
			case EFCObjRefType::NewUStruct:
			case EFCObjRefType::RefStructValue:
			{
				UStruct* Struct = ObjRef->ClassDesc->m_Struct;
				UScriptStruct *ScriptStruct = ObjRef->ClassDesc->m_ScriptStruct;
				int ValueSize = Struct->GetStructureSize();
				int ArrayDim = ObjRef->DynamicProperty ? ObjRef->DynamicProperty->SafePropertyPtr->ArrayDim : 1;
                bool bNeedDestory = true;
				if (ScriptStruct)
				{
					if ((ScriptStruct->StructFlags & (STRUCT_IsPlainOldData | STRUCT_NoDestructor)))
					{
                        bNeedDestory = false;
					}
				}
                if(bNeedDestory)
                {
				    Struct->DestroyStruct(ObjRef->GetPropertyAddr(), ArrayDim);
                }
                DelStructBuffer(ObjRef->GetThisAddr());
			}
			break;
			case EFCObjRefType::NewProperty:
			{
                ObjRef->DynamicProperty->SafePropertyPtr->DestroyValue(ObjRef->GetThisAddr());
                DelStructBuffer(ObjRef->GetThisAddr());
			}
			break;
			case EFCObjRefType::NewTArray:
			{
				FScriptArray *ScriptArray = (FScriptArray*)ObjRef->GetThisAddr();
				FArrayProperty  *ArrayProperty = ObjRef->DynamicProperty->SafePropertyPtr->CastArrayProperty();
				FProperty *Inner = ArrayProperty->Inner;
				TArray_Clear(ScriptArray, Inner);
				delete ScriptArray;
			}
			break;
			case EFCObjRefType::NewTMap:
			{
				FScriptMap *ScriptMap = (FScriptMap*)ObjRef->GetThisAddr();
				TMap_Clear(ScriptMap, ObjRef->DynamicProperty->SafePropertyPtr->CastMapProperty());
				delete ScriptMap;
			}
            break;
            case EFCObjRefType::NewTSet:
            {
				FScriptSet* ScriptMap = (FScriptSet*)ObjRef->GetThisAddr();
				TSet_Clear(ScriptMap, ObjRef->DynamicProperty->SafePropertyPtr->CastSetProperty());
                delete ScriptMap;
            }
            break;
            case EFCObjRefType::NewTLazyPtr:
            {
                FLazyObjectPtr* LazyPtr = (FLazyObjectPtr*)ObjRef->GetThisAddr();
                delete LazyPtr;
            }
            break;
            case EFCObjRefType::NewTWeakPtr:
            {
                FWeakObjectPtr* WeakPtr = (FWeakObjectPtr*)ObjRef->GetThisAddr();
                delete WeakPtr;
            }
            break;
            case EFCObjRefType::NewTSoftObjectPtr:
            {
                FSoftObjectPtr *SoftPtr = (FSoftObjectPtr *)ObjRef->GetThisAddr();
                delete SoftPtr;
            }
            break;
            case EFCObjRefType::NewTSoftClassPtr:
            {
                FSoftObjectPtr* SoftPtr = (FSoftObjectPtr*)ObjRef->GetThisAddr();
                delete SoftPtr;
            }
            break;
			case EFCObjRefType::MapIterator:
			{
				TMapIterator *pIteratorBuffer = (TMapIterator*)ObjRef->GetThisAddr();
				if(pIteratorBuffer)
				{
					delete pIteratorBuffer;
				}
			}
			break;
			default:
			break;
		}

		delete ObjRef;
	}
}

void  *FCGetObj::NewStructBuffer(int nBuffSize)
{
	return new char[nBuffSize];
}

void   FCGetObj::DelStructBuffer(void *pBuffer)
{
	if(pBuffer)
	{
		char *pBuf = (char *)pBuffer;
		delete []pBuf;
	}
}

static FCGetObj   s_GEtObjIns;
