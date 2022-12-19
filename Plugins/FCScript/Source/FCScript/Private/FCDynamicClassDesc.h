#pragma once
#include "UObject/UnrealType.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Class.h"
#include "Logging/LogCategory.h"
#include "Logging/LogMacros.h"
#include "CoreUObject.h"
#include "FCPropertyType.h"
#include "FCStringCore.h"
#include "../LuaCore/LuaHeader.h"

DECLARE_LOG_CATEGORY_EXTERN(LogFCScript, Log, All);

struct FCUnitPtr
{
	union
	{
		const void  *Ptr;
		int64  nPtr;
	};
};

struct FCDynamicField
{
	virtual ~FCDynamicField() {}
	virtual int DoGet(lua_State* L, void* ObjRefPtr, void *ClassDescPtr) { return 0; }
	virtual int DoSet(lua_State* L, void* ObjRefPtr, void *ClassDescPtr) { return 0; }
	virtual const char* GetFieldName() const { return ""; }
	virtual FCDynamicField* Clone() const { return nullptr; }
};

// 动态属性(反射)
struct FCDynamicPropertyBase : public FCDynamicField
{
	int		ElementSize;      // 元素所点字节
	int		Offset_Internal;  // 相对偏移
	int     PropertyIndex;    // 参数或属性索引(序号)
	int     ScriptParamIndex; // 脚本参数索引号
	std::string Name;        // 变量名,调试时用的(常引用)
	std::string ClassName;   // 类名
	
	FCPropertyType    Type;       // 类型
	EPropertyFlags    Flags;      // 属性类型（用于强制转换的检测)
	const FProperty  *Property;
	bool              bRef;       // 是不是引用类型
	bool              bOuter;     // 是不是输出类型
    bool              bTempNeedRef;  // 临时的上下拷贝参数标记
    bool              bTempRealRef;  // 
	
	FCDynamicPropertyBase() :ElementSize(0), Offset_Internal(0), PropertyIndex(0), ScriptParamIndex(0), Type(FCPropertyType::FCPROPERTY_Unkonw), Flags(CPF_None), Property(nullptr), bRef(false), bOuter(false), bTempNeedRef(false), bTempRealRef(false)
	{
	}
	bool  IsRef() const
	{
		return bRef;
	}
	bool  IsOuter() const
	{
		return bOuter;
	}
	const char* GetFieldName() const
	{
		return Name.c_str();
	}
	const char* GetClassName() const
	{
		return ClassName.c_str();
	}
	// 功能：得到委托的触发函数
	UFunction *GetSignatureFunction() const
	{		
		if(FCPropertyType::FCPROPERTY_MulticastDelegateProperty == Type)
		{
			FMulticastDelegateProperty* DelegateProperty = (FMulticastDelegateProperty*)Property;
			return DelegateProperty->SignatureFunction;
		}
		else if(FCPROPERTY_DelegateProperty == Type)
		{
			FDelegateProperty* DelegateProperty = (FDelegateProperty*)Property;
			return DelegateProperty->SignatureFunction;
		}
		return nullptr;
	}
};

typedef  void(*LPPushScriptValueFunc)(lua_State* L, const FCDynamicPropertyBase *DynamicProperty, uint8  *ValueAddr, UObject *ThisObj, void *ObjRefPtr);
typedef  void(*LPOuterScriptValueFunc)(lua_State* L, int ValueIdx, const FCDynamicPropertyBase *DynamicProperty, uint8  *ValueAddr, UObject *ThisObj, void* ObjRefPtr);

// 动态属性(反射)
struct FCDynamicProperty : public FCDynamicPropertyBase
{
	LPPushScriptValueFunc    m_WriteScriptFunc;   // 将UE变量写入到脚本
	LPOuterScriptValueFunc   m_ReadScriptFunc;    // 将脚本变量写入到UE对象

	FCDynamicProperty():m_WriteScriptFunc(nullptr), m_ReadScriptFunc(nullptr)
	{
	}

	void  InitProperty(const FProperty *InProperty);
	virtual FCDynamicField* Clone() const { return new FCDynamicProperty(*this); }
	virtual int DoGet(lua_State* L, void* ObjRefPtr, void* ClassDescPtr);
	virtual int DoSet(lua_State* L, void* ObjRefPtr, void* ClassDescPtr);
};

struct  FCDynamicFunction : public FCDynamicField
{
	UFunction  *Function;
    int     LatentPropertyIndex;
	int     ReturnPropertyIndex;
	int     ParmsSize;        // 参数序列化后的字节大小
	int     ParamCount;       // 函数参数个数(不包括返回值)
    int     OuterParamCount;  // 
    int     OuterParamSize;
	bool    bOverride;
	bool    bOuter;
	bool    bRegister;        // 是不是在类中注册了
	bool    bDelegate;
	std::string Name;        // 函数名
	std::vector<FCDynamicProperty>   m_Property;
	FCDynamicFunction():Function(nullptr), LatentPropertyIndex(-1), ReturnPropertyIndex(-1), ParmsSize(0), ParamCount(0), OuterParamCount(0), OuterParamSize(0), bOverride(false), bOuter(false), bRegister(false), bDelegate(false)
	{
	}
	void  InitParam(UFunction *InFunction);
    bool IsLatentFunction() const 
    {
        return LatentPropertyIndex != -1; 
    }
	const char* GetFieldName() const
	{
		return Name.c_str();
	}
	virtual FCDynamicField* Clone() const { return new FCDynamicFunction(*this); }
	virtual int DoGet(lua_State* L, void* ObjRefPtr, void* ClassDescPtr);
	virtual int DoSet(lua_State* L, void* ObjRefPtr, void* ClassDescPtr);
};

struct FCDynamicOverrideFunction : public FCDynamicFunction
{
	FNativeFuncPtr   OleNativeFuncPtr;  // 原始的NativeFunc函数
	FNativeFuncPtr   CurOverrideFuncPtr;
	int              m_NativeBytecodeIndex;
	bool             m_bLockCall;
	TArray<uint8>    m_NativeScript;
	FCDynamicOverrideFunction() : OleNativeFuncPtr(nullptr), CurOverrideFuncPtr(nullptr), m_NativeBytecodeIndex(0), m_bLockCall(false)
	{
	}
};

typedef int (*LPWrapLibFunction)(lua_State* L, void *ObjRefPtr, UObject *ThisObject);
typedef int (*LPLuaLibOpenCallback)(lua_State* L);

struct FCDynamicWrapField : public FCDynamicField
{
	std::string Name;        // 函数名或属性名
	LPWrapLibFunction   m_GetFunction;
	LPWrapLibFunction   m_SetFunction;
	FCDynamicWrapField():m_GetFunction(nullptr), m_SetFunction(nullptr) {}
	FCDynamicWrapField(LPWrapLibFunction InGetFunc, LPWrapLibFunction InSetFunc) :m_GetFunction(InGetFunc), m_SetFunction(InSetFunc) {}
	const char* GetFieldName() const
	{
		return Name.c_str();
	}
};

struct FCDynamicWrapLibFunction : public FCDynamicFunction
{
	LPWrapLibFunction   m_GetFunction;
	LPWrapLibFunction   m_SetFunction;
	FCDynamicWrapLibFunction() :m_GetFunction(nullptr), m_SetFunction(nullptr) {}
	FCDynamicWrapLibFunction(LPWrapLibFunction InGetFunc, LPWrapLibFunction InSetFunc) :m_GetFunction(InGetFunc), m_SetFunction(InSetFunc) {}
	virtual FCDynamicField* Clone() const { return new FCDynamicWrapLibFunction(*this); }
	virtual int DoGet(lua_State* L, void* ObjRefPtr, void* ClassDescPtr);
	virtual int DoSet(lua_State* L, void* ObjRefPtr, void* ClassDescPtr);
};

struct FCDynamicWrapLibAttrib : public FCDynamicWrapField
{
	FCDynamicWrapLibAttrib(){}
	FCDynamicWrapLibAttrib(LPWrapLibFunction InGetFunc, LPWrapLibFunction InSetFunc) :FCDynamicWrapField(InGetFunc, InSetFunc) {}
	virtual FCDynamicField* Clone() const { return new FCDynamicWrapLibAttrib(*this); }
	virtual int DoGet(lua_State* L, void* ObjRefPtr, void* ClassDescPtr);
	virtual int DoSet(lua_State* L, void* ObjRefPtr, void* ClassDescPtr);
};

struct FCDelegateInfo
{
    enum 
    {
        CallbackParamMax = 4,
    };
	FCDynamicOverrideFunction*    DynamicFunc;
	FCDynamicProperty*    DynamicProperty;
    const void*   FunctionAddr;
    int           FunctionRef;  // lua 引用的函数
    int           ParamCount;  // lua 回调参数个数
    int           CallbackParams[CallbackParamMax];  // 回调参数，最多4个
	FCDelegateInfo():DynamicFunc(nullptr), DynamicProperty(nullptr), FunctionRef(0){}
	FCDelegateInfo(const FCDynamicOverrideFunction *InDynamicFunc, const FCDynamicProperty *InDynamicProperty, const void *InFunctionAddr, int InFunctionRef, const int *InCallbackParams, int InCallbackParamCount)
		: DynamicFunc((FCDynamicOverrideFunction*)InDynamicFunc)
		, DynamicProperty((FCDynamicProperty*)InDynamicProperty)
        , FunctionAddr(InFunctionAddr)
		, FunctionRef(InFunctionRef)
		, ParamCount(InCallbackParamCount)
	{
        for(int i = 0; i< CallbackParamMax && i< InCallbackParamCount; ++i)
        {
            CallbackParams[i] = InCallbackParams[i];
        }
	}
	bool operator == (const FCDelegateInfo &Other) const
	{
		return DynamicFunc == Other.DynamicFunc
			&& DynamicProperty == Other.DynamicProperty
			&& FunctionAddr == Other.FunctionAddr;
	}
};

struct FCDynamicDelegateList
{
	std::vector<FCDelegateInfo>      Delegates;  // 脚本中绑定的委托方法
	
	FCDynamicDelegateList()
	{
	}
	int FindDelegate(const FCDelegateInfo &Info) const;
		
	// 功能：添加一个委托
	bool  AddScriptDelegate(const FCDelegateInfo &Info);
	bool  DelScriptDelegate(const FCDelegateInfo &Info);
};


typedef  stdext::hash_map<const char*, FCDynamicProperty*>   CDynamicName2Property;
typedef  stdext::hash_map<int, FCDynamicProperty*>   CDynamicID2Property;
typedef  std::vector<FCDynamicProperty*>   CDynamicPropertyPtrArray;

typedef  stdext::hash_map<int, FCDynamicFunction*>   CDynamicFunctionIDMap; // id == > function
typedef  stdext::hash_map<const char*, FCDynamicFunction*>   CDynamicFunctionNameMap;  // name ==> function
typedef  stdext::hash_map<const char*, FCDynamicField*>   CDynamicFieldNameMap;  // name ==> function

typedef  stdext::hash_map<const char*, int>   CDynamicName2Int; // name ==> int

const char* GetUEClassName(const char* InName);
const char* GetConstName(const char* InName);

// 一个动态类的数据结构
struct FCDynamicClassDesc
{
	UStruct                     *m_Struct;
	UClass                      *m_Class;
    UScriptStruct               *m_ScriptStruct;
	FCDynamicClassDesc          *m_Super;
	EClassCastFlags              m_ClassFlags;  // 用于强制转换的检测
	std::string                  m_SuperName;
	std::string                  m_UEClassName; // 类名，wrap的类名
	CDynamicPropertyPtrArray     m_Property;  // 属性
	CDynamicName2Property        m_Name2Property;  // 所有的属性

	CDynamicFunctionNameMap      m_Functions;   // 所有的函数 name ==> function
	CDynamicFieldNameMap         m_LibFields;   // 扩展属性与方法
    CDynamicFieldNameMap         m_Fileds;      // 属性
	LPLuaLibOpenCallback         m_LibOpenCallback;
	
	FCDynamicClassDesc():m_Struct(nullptr), m_Class(nullptr), m_ScriptStruct(nullptr), m_Super(nullptr), m_ClassFlags(CASTCLASS_None), m_LibOpenCallback(nullptr)
	{
	}
	~FCDynamicClassDesc();
	FCDynamicClassDesc(const FCDynamicClassDesc &other);
	FCDynamicClassDesc &operator = (const FCDynamicClassDesc &other)
	{
		return CopyDesc(other);
	}

	void Clear();
	FCDynamicClassDesc &CopyDesc(const FCDynamicClassDesc &other);

	void  OnRegisterStruct(UStruct *Struct, void *Context);
	void  OnAddStructMember(UStruct* Struct, void* Context);

	// 功能：注册一个函数
	FCDynamicFunction*  RegisterUEFunc(const char *pcsFuncName);
	
	// 功能：注册一个类的属性
	FCDynamicProperty*  RegisterProperty(const char *pcsPropertyName);
	// 功能：注册一个函数
	FCDynamicFunction*  RegisterFunc(const char *pcsFuncName);

	// 添加一个自定义的扩展函数
	FCDynamicField*  RegisterWrapLibFunction(const char* pcsFuncName, LPWrapLibFunction InGetFunc, LPWrapLibFunction InSetFunc);
	// 添加一个自定义的扩展属性
	FCDynamicField*  RegisterWrapLibAttrib(const char* pcsFuncName, LPWrapLibFunction InGetFunc, LPWrapLibFunction InSetFunc);

	// table注册时的回调
	void OnLibOpen(lua_State* L)
	{
		if (m_LibOpenCallback)
			m_LibOpenCallback(L);
	}
	// 设置一个回调，就是lua表注册时调用
	void SetLibopenCallback(LPLuaLibOpenCallback callback)
	{
		m_LibOpenCallback = callback;
	}

	FCDynamicFunction  *FindFunctionByName(const char *FuncName)
	{
		CDynamicFunctionNameMap::iterator itFunction = m_Functions.find(FuncName);
		if(itFunction != m_Functions.end())
		{
			return itFunction->second;
        }
        if (m_Super)
        {
            return m_Super->FindFunctionByName(FuncName);
        }
		return nullptr;
	}
	FCDynamicProperty *FindAttribByName(const char *AttribName)
	{
		CDynamicName2Property::iterator itAttrib = m_Name2Property.find(AttribName);
		if(itAttrib != m_Name2Property.end())
		{
			return itAttrib->second;
		}
		if(m_Super)
		{
			return m_Super->FindAttribByName(AttribName);
		}
		return nullptr;
	}
	FCDynamicField* TryFindFiled(const char* FieldName)
	{
		CDynamicFieldNameMap::iterator itFiled = m_Fileds.find(FieldName);
		if (itFiled != m_Fileds.end())
		{
			return (FCDynamicWrapField*)(itFiled->second);
		}
		if (m_Super)
		{
			return m_Super->TryFindFiled(FieldName);
		}
		return nullptr;
	}
    FCDynamicField  *FindFieldByName(const char *InFieldName)
    {
        CDynamicFieldNameMap::iterator itFiled = m_Fileds.find(InFieldName);
        if(itFiled != m_Fileds.end())
        {
            return itFiled->second;
        }
		FCDynamicField* Filed = TryFindFiled(InFieldName);
		if (Filed)
		{
			m_Fileds[Filed->GetFieldName()] = Filed;
			return Filed;
		}
        // 都没有找到的情况下，尝试注册一个空的，以免再次查找
		InFieldName = GetConstName(InFieldName);
		m_Fileds[InFieldName] = nullptr;
        return nullptr;
    }
};

struct FDynamicEnum
{
    struct SDynamicEnumValue
    {
        std::string   Name;
        int           Value;
    };
    std::vector<SDynamicEnumValue>  EnumValue;
    CDynamicName2Int   m_NameToValue;
    void OnRegisterEnum(UEnum* InEnum);
    int FindEnumValue(const char *InName) const
    {
        if(!InName)
            return 0;
        CDynamicName2Int::const_iterator itValue = m_NameToValue.find(InName);
        if(itValue != m_NameToValue.end())
            return itValue->second;
        return 0;
    }
};

typedef stdext::hash_map<const char *, FCDynamicClassDesc*>   CDynamicClassNameMap;
typedef stdext::hash_map<std::string, FDynamicEnum*>   CDynamicEnumNameMap;
typedef stdext::hash_map<int, FCDynamicClassDesc*>   CDynamicClassIDMap;
typedef stdext::hash_map<UStruct*, FCDynamicClassDesc*>   CDynamicUStructMap;
typedef stdext::hash_map<FProperty*, FCDynamicClassDesc*>   CDynamicPropertyMap;
typedef  stdext::hash_map<lua_State*, int32>   ThreadToRefMap;
typedef  stdext::hash_map<int32, lua_State*>   RefToThreadMap;

struct FCScriptContext
{
	bool                  m_bInit;
	lua_State            *m_LuaState;
    UObject              *m_Ticker;

	CDynamicClassNameMap  m_ClassNameMap;   // name == > class ptr
	CDynamicClassNameMap  m_ClassFinder;   // name == > class ptr
    CDynamicEnumNameMap   m_EnumNameMap;    // name == > class ptr
	CDynamicUStructMap    m_StructMap;      // UStruct* ==> class ptr
    CDynamicPropertyMap   m_PropeytyMap;    // FPropery ==> class ptr

    ThreadToRefMap        m_ThreadToRef;
    RefToThreadMap        m_RefToThread;

	FCScriptContext():m_bInit(false), m_LuaState(nullptr), m_Ticker(nullptr)
	{
	}

    int  QueryLuaRef(lua_State* L);
    void ResumeThread(int ThreadRef);
	
	FCDynamicClassDesc*  RegisterUClass(const char *UEClassName);
	FCDynamicClassDesc*  RegisterUStruct(UStruct *Struct);
    FCDynamicClassDesc*  RegisterByProperty(FProperty *Property);
    FDynamicEnum*        RegisterEnum(const char *InEnumName);
	void Clear();
};

struct FCContextManager
{
	FCScriptContext   ClientDSContext;  // Client + DS, 不再区分DS
	static  FCContextManager  *ConextMgrInsPtr;
	FCContextManager();
	~FCContextManager();
	void Clear();
};

inline FCContextManager *GetContextManger()
{
	return FCContextManager::ConextMgrInsPtr;
}

inline FCScriptContext  *GetClientScriptContext()
{
	return &(FCContextManager::ConextMgrInsPtr->ClientDSContext);
}

inline FCScriptContext  *GetScriptContext()
{
	return &(FCContextManager::ConextMgrInsPtr->ClientDSContext);
}
