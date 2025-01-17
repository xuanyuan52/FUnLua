#pragma once
#include "FCDynamicClassDesc.h"

enum EFCObjRefType
{
	RefNone,        // 未知
	RefObject,      // UObject对象引用
	NewUObject,     // new UObject对象
	NewUStruct,     // new UStrucct对象
	NewProperty,    // new Property对象
    NewCppStruct,   // new Cpp class
	RefProperty,    // UObject的属性引用
    RefFunction,    // 引用Function
	RefStructValue, // 普通的Struct变量引用
	NewTArray,      // new TArray
	NewTMap,        // new TMap
	NewTSet,        // new TSet
	NewTLazyPtr,    // 
	NewTWeakPtr,    // 
    NewTSoftObjectPtr, // 
    NewTSoftClassPtr, // 
	CppPtr,         // 全局的Cpp对象指针
	MapIterator,    // map_iterator
    LuaDelegate,    // Lua委托对象
};

struct TMapIterator
{
    int64       MapInsID;
    int32       Index;
    TMapIterator() :MapInsID(0), Index(0) {}
};

struct FCObjRef
{
	FCObjRef  *m_pNext;
	FCObjRef  *Parent; // 父节点
	FCDynamicClassDesc* ClassDesc;
    union
    {
	    FCDynamicProperty *DynamicProperty;  // 属性描述
        FCDynamicFunction *DynamicFunction;  // 函数
    };

	int64      PtrIndex;         // Wrap对象ID
    uint8*     ThisObjAddr;     // 对象自己的地址
	int        Ref;             // 引用计数
	EFCObjRefType  RefType;
    FCObjRef *Childs; // 使用单链表
#ifdef UE_BUILD_DEBUG
    const char* DebugDesc;
#endif
	FCObjRef():m_pNext(NULL), Parent(NULL), ClassDesc(NULL), DynamicProperty(NULL), PtrIndex(0), ThisObjAddr(NULL), Ref(0), RefType(RefNone), Childs(nullptr)
	{
#ifdef UE_BUILD_DEBUG
        DebugDesc = nullptr;
#endif
	}
    void PushChild(FCObjRef* ChildRef)
    {
        ChildRef->m_pNext = Childs;
        Childs = ChildRef;
    }
    void EraseChild(FCObjRef *ChildRef)
    {
        if(Childs == ChildRef)
        {
            Childs = Childs->m_pNext;
        }
        else
        {
            FCObjRef *pList = Childs;
            FCObjRef *pNext = nullptr;
            while(pList)
            {
                pNext = pList->m_pNext;
                if(pNext == ChildRef)
                {
                    pList->m_pNext = pNext ? pNext->m_pNext : nullptr;
                    break;
                }
                pList = pNext;
            }
        }
    }
	ObjRefKey GetRefKey() const
	{
		return ObjRefKey(Parent ? Parent->ThisObjAddr : nullptr, ThisObjAddr);
	}
	UObject* GetParentObject() const
	{
		if (Parent)
		{
			return Parent->GetUObject();
		}
		return nullptr;
	}
	UObject* GetUObject() const
	{
		if (RefType == EFCObjRefType::RefObject || RefType == EFCObjRefType::NewUObject)
			return (UObject*)ThisObjAddr;
		return nullptr;
	}
	uint8* GetThisAddr() const
	{
        return ThisObjAddr;
	}
	bool IsValid() const
	{
		return Parent ? (Parent->ThisObjAddr != nullptr) : (ThisObjAddr != nullptr);
	}
	uint8*GetPropertyAddr()
	{
        return ThisObjAddr;
	}
    FCPropertyType GetPropertyType() const
    {
        if(RefType != RefFunction)
            return DynamicProperty->Type;
        else
            return FCPROPERTY_Function;
    }
	FStructProperty *GetStructProperty() const
	{
		if(EFCObjRefType::RefProperty == RefType || EFCObjRefType::RefStructValue == RefType)
		{
			return DynamicProperty->SafePropertyPtr->CastStructProperty();
		}
		return NULL;
	}
	TMapIterator *GetMapIterator() const
	{
		if(EFCObjRefType::MapIterator == RefType)
		{
			return (TMapIterator *)ThisObjAddr;
		}
		return nullptr;
	}
};

typedef  std::unordered_map<ObjRefKey, FCObjRef*>   CScriptRefObjMap;  // ptr ==> FCObjRef
typedef  std::unordered_map<int64, FCObjRef*>   CIntPtr2RefObjMap; // IntPtr ==> FCObjRef
typedef  std::unordered_map<int64, int64>   CIntPtr2IntPtrMap; // IntPtr ==> IntPtr
typedef  std::vector<int64>            CIntPtrList; // 

class FCGetObj
{	
public:
	static FCGetObj  *s_Ins;
	FCGetObj();
	~FCGetObj();

	static FCGetObj  *GetIns()
	{
		return FCGetObj::s_Ins;
	}
public:
    void Clear();
    void SetAllObjRefFlag();
	// 功能：压入一个UObject对象
	int64  PushUObject(UObject* Obj);
	FCObjRef*  PushUObjectNoneRef(UObject* Obj);
	int64  PushNewObject(FCDynamicClassDesc* ClassDesc, const FName& Name, UObject* Outer);
	int64  PushNewStruct(FCDynamicClassDesc* ClassDesc);
    int64  PushCppStruct(FCDynamicClassDesc* ClassDesc, void *pValueAddr);
	// 功能：压入一个UObject的属性(生成周期随父对象)的引用
	int64  PushProperty(UObject *Parent, const FCDynamicProperty *DynamicProperty, void *pValueAddr);
    int64  PushTempRefProperty(const FCDynamicProperty* DynamicProperty, void* pValueAddr);
	// 功能：压入一个子属性(UObject或UStruct的成员变量)的引用
	int64  PushChildProperty(FCObjRef *Parent, const FCDynamicProperty* DynamicProperty, void* pValueAddr);
	// 功能：压入一个纯Struct对象(没有父对象，一般是临时的)
	int64  PushStructValue(const FCDynamicProperty *DynamicProperty, void *pValueAddr);
    int64  PushNewTArray(const FCDynamicProperty* DynamicProperty, void* pValueAddr);
    int64  PushNewTMap(const FCDynamicProperty* DynamicProperty, void* pValueAddr);
    int64  PushNewTSet(const FCDynamicProperty* DynamicProperty, void* pValueAddr);
	// 功能：将一个Cpp栈上的临时变量压入到对象管理器
	int64  PushCppPropery(const FCDynamicProperty* DynamicProperty, void* pValueAddr);
	int64  PushTemplate(const FCDynamicProperty *DynamicProperty, void *pValueAddr, EFCObjRefType RefType);

	// 功能：压入一个全局指针
	int64  PushCppPtr(void *CppPtr);

    // 功能：压入一个TMap的迭代器
    int64  PushMapIterator(void* IteratorPtr);

    // 功能：压入一个全局的Lua委托对象
    int64  PushLuaDelegate(const FCDelegateInfo *DelegateInfo);

	// 功能：对象释放事件
	void  NotifyDeleteUObject(const class UObjectBase* Object, int32 Index);

    // 功能：清除临时的对象(用于C++侧调用lua的临时参数，使用引用传递）
    void  ClearTempIDList(int nStart);

	// 功能：删除一个对象(由脚本是通过new创建的)
	void   DeleteValue(int64 ObjID);

	// 功能：释放一个脚本对象(通过参数传入的）
	void   ReleaseValue(int64 ObjID);

	void   ReleaseCacheRef(int64 ObjID, int nCacheRef);

	FCObjRef *FindValue(int64 ObjID)
	{
		CIntPtr2RefObjMap::iterator itObj = m_IntPtrMap.find(ObjID);
		if(itObj != m_IntPtrMap.end())
		{
			return itObj->second;
		}
		return nullptr;
	}
    FCObjRef *FindObjRefByKey(const ObjRefKey & ObjKey)
    {
        CScriptRefObjMap::iterator itObj = m_ObjMap.find(ObjKey);
        if (itObj != m_ObjMap.end())
        {
            return itObj->second;
        }
        return nullptr;
    }

	UObject *GetUObject(int64 ObjID)
	{
		CIntPtr2RefObjMap::iterator itObj = m_IntPtrMap.find(ObjID);
		if(itObj != m_IntPtrMap.end())
		{
			return itObj->second->GetUObject();
		}
		return nullptr;
	}
	void *GetPropertyAddr(int64 ObjID)
	{
		CIntPtr2RefObjMap::iterator itObj = m_IntPtrMap.find(ObjID);
		if (itObj != m_IntPtrMap.end())
		{
			return itObj->second->GetPropertyAddr();
		}
		return nullptr;
	}
	void  *GetValuePtr(int64 ObjID) const
	{
		CIntPtr2RefObjMap::const_iterator itObj = m_IntPtrMap.find(ObjID);
		if (itObj != m_IntPtrMap.end())
		{
			return itObj->second->GetPropertyAddr();
		}
		return nullptr;
	}
	bool EqualValue(int64 LeftObjID, int64 RightObjID) const
	{
		void* LeftPtr = GetValuePtr(LeftObjID);
		void* RightPtr = GetValuePtr(RightObjID);
		return LeftPtr == RightPtr;
	}
	int  GetTotalObjRef() const
	{
		return (int)m_ObjMap.size();
	}
	int  GetTotalIntPtr() const
	{
		return (int)m_IntPtrMap.size();
	}
    int  GetTempObjIDCount() const
    {
        return (int)m_TempObjIDList.size();
    }
protected:
	FCObjRef  *NewObjRef();
	void  ReleaseObjRef(FCObjRef* ObjRef);
	void  DestroyChildRef(FCObjRef* ObjRef);
	void  DestroyObjRef(FCObjRef *ObjRef);
	void  *NewStructBuffer(int nBuffSize);
	void   DelStructBuffer(void *pBuffer);
protected:
	int64			   m_nObjID;
	CScriptRefObjMap   m_ObjMap;
	CIntPtr2RefObjMap  m_IntPtrMap;
	FCDynamicProperty  m_CppProperty;
	FCDynamicProperty  m_MapIteratorProperty;
    CIntPtrList        m_TempObjIDList;
};
