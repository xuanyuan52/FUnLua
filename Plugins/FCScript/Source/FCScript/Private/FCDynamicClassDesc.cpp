
#include "FCDynamicClassDesc.h"
#include "UObject/Class.h"
#include "FCCallScriptFunc.h"
#include "FCDynamicOverrideFunc.h"
#include "FCDefaultParam.h"
#include "Engine/UserDefinedStruct.h"

void  FCDynamicProperty::InitProperty(const FProperty *InProperty, const char* InName)
{
    Type = GetScriptPropertyType(InProperty);
    if(!InName)
    {
        Name = TCHAR_TO_UTF8(*(InProperty->GetName()));
        Name = GetConstName(Name);
    }
    else
    {
        Name = GetConstName(InName);
    }
	ElementSize = InProperty->ElementSize;
	Offset_Internal = InProperty->GetOffset_ForInternal();
    SafePropertyPtr = GetSafeProperty(InProperty);
	bOuter = InProperty->HasAnyPropertyFlags(CPF_OutParm);

    ClassName = GetScriptPropertyClassName(Type, InProperty);

    m_WriteScriptFunc = InitDynamicPropertyWriteFunc(Type);
    m_ReadScriptFunc = InitDynamicPropertyReadFunc(Type);
    m_CopyScriptValue = InitDynamicPropertyCopyFunc(Type);

#ifdef UE_BUILD_DEBUG
    FCStringBuffer128   TempBuffer;
    TempBuffer << ClassName << ':' << Name;
    DebugDesc = GetConstName(TempBuffer.GetString());
#endif
}

void  FCDynamicProperty::InitCppType(FCPropertyType InType, const char* InClassName, int InElementSize)
{
    Name = "";
    ElementSize = InElementSize;
    Offset_Internal = 0;
    SafePropertyPtr = nullptr;
    Type = InType;
    ClassName = GetConstName(InClassName);

    m_WriteScriptFunc = InitDynamicPropertyWriteFunc(Type);
    m_ReadScriptFunc = InitDynamicPropertyReadFunc(Type);
    m_CopyScriptValue = InitDynamicPropertyCopyFunc(Type);

#ifdef UE_BUILD_DEBUG
    FCStringBuffer128   TempBuffer;
    TempBuffer << ClassName << ':' << Name << ":CppType";
    DebugDesc = GetConstName(TempBuffer.GetString());
#endif
}

void  FCDynamicFunction::InitParam(UFunction *InFunction)
{
    Name = TCHAR_TO_UTF8(*(InFunction->GetName()));
    Name = GetConstName(Name);

	Function = InFunction;
	ParmsSize = InFunction->ParmsSize;
	m_Property.resize(InFunction->NumParms);
	ReturnPropertyIndex = -1;
    LatentPropertyIndex = -1;
	bOuter = false;
	int Index = 0;
	ParamCount = 0;
    OuterParamCount = 0;
    OuterParamSize = 0;

    TMap<FName, FString>* MetaMap = UMetaData::GetMapForObject(Function);

#ifdef UE_BUILD_DEBUG
    FCStringBuffer128   TempBuffer;
#endif

    int SrcCount = InFunction->NumParms;
    FCStringBuffer128 DefaultName;
	for (TFieldIterator<FProperty> It(InFunction); It && (It->PropertyFlags & CPF_Parm) && Index < SrcCount; ++It, ++Index)
	{
		FProperty *Property = *It;

        FCDynamicFunctionParam* FCProperty = &(m_Property[Index]);
		FCProperty->InitProperty(Property);
		FCProperty->PropertyIndex = Index;

		if(FCProperty->bOuter)
		{
			bOuter = true;
            ++OuterParamCount;
            OuterParamSize += Property->ElementSize;
		}
        if(LatentPropertyIndex == -1)
        {
            if(strcmp(FCProperty->Name, "LatentInfo") == 0)
            {
                LatentPropertyIndex = Index;
            }
        }
		if(Property->HasAnyPropertyFlags(CPF_ReturnParm))
		{
			ReturnPropertyIndex = Index;
		}
		else
		{
			++ParamCount;
		}

        // 初始化默认值
        if(MetaMap)
        {
            DefaultName.Empty();
            DefaultName << "CPP_Default_" << FCProperty->Name;
            FName  Key(DefaultName.GetString());
            const FString *Result = MetaMap->Find(Key);
            if(Result)
            {
                FCProperty->DefaultParam = GetDefaultValue(FCProperty, *Result);
            }
        }

#ifdef UE_BUILD_DEBUG
        TempBuffer.Empty();
        TempBuffer << Name << ":" << FCProperty->Name;
        FCProperty->DebugDesc = GetConstName(TempBuffer.GetString());
#endif

	}
	if(ReturnPropertyIndex != -1)
	{
		int RealReturnIndex = m_Property.size() - 1;
		if(ReturnPropertyIndex != RealReturnIndex)
		{
            std::swap(m_Property[ReturnPropertyIndex], m_Property[RealReturnIndex]);
			ReturnPropertyIndex = RealReturnIndex;
		}
	}
#ifdef UE_BUILD_DEBUG
    TempBuffer.Empty();
    TempBuffer << "Function:" << Name;
    DebugDesc = GetConstName(TempBuffer.GetString());
#endif
}

void  FCDynamicFunction::SetParamRefFlags()
{
    for (int i = m_Property.size() - 1; i >= 0; --i)
    {
        SetPtrRefFlag(m_Property[i].SafePropertyPtr);
    }
}

int FCDynamicDelegateList::FindDelegate(const FCDelegateInfo &Info) const
{
	for(int i = 0; i<Delegates.size(); ++i)
	{
		if(Delegates[i] == Info)
		{
			return i;
		}
	}
	return -1;
}

bool  FCDynamicDelegateList::AddScriptDelegate(const FCDelegateInfo &Info)
{
	int Index = FindDelegate(Info);
	if( Index != -1)
	{
		return false;
	}
	Delegates.push_back(Info);
	return true;
}

bool  FCDynamicDelegateList::DelScriptDelegate(const FCDelegateInfo &Info)
{
	int Index = FindDelegate(Info);
	if( Index != -1)
	{
        Delegates.erase(Delegates.begin()+Index);
		return true;
	}
	return false;
}

FCDynamicClassDesc::FCDynamicClassDesc(const FCDynamicClassDesc &other):m_Super(nullptr), m_ClassFlags(CASTCLASS_None)
{
	CopyDesc(other);
}

FCDynamicClassDesc::~FCDynamicClassDesc()
{
	Clear();
}

void FCDynamicClassDesc::Clear()
{
	for (int i = m_Property.size() - 1; i >= 0; --i)
	{
		delete (m_Property[i]);
	}
	m_Property.clear();
	m_Name2Property.clear();

	ReleasePtrMap(m_Functions);
	ReleasePtrMap(m_LibFields);
    m_Fileds.clear();
    m_SystemFunctions.clear();
    m_UserDefinedStructFlag = 0;
}

void FCDynamicClassDesc::ClearNoneRefField()
{
    for(int i = m_Property.size() - 1; i>=0; --i)
    {
        FCDynamicProperty * FCProperty = m_Property[i];
        if(!IsRefPtr(FCProperty))
        {
            bool bValidPtr = FCProperty->SafePropertyPtr && FCProperty->SafePropertyPtr->IsValid();
            if(!bValidPtr)
            {
                m_Name2Property.erase(FCProperty->Name);
                m_Fileds.erase(FCProperty->Name);
                delete FCProperty;
                m_Property.erase(m_Property.begin() + i);
            }
            else
            {
                SetPtrRefFlag(FCProperty->SafePropertyPtr);
            }
        }
        else
        {
            SetPtrRefFlag(FCProperty->SafePropertyPtr);
        }
    }
    // 函数也清除
    for(CDynamicFunctionNameMap::iterator itFunc = m_Functions.begin(); itFunc != m_Functions.end(); ++itFunc)
    {
        FCDynamicFunction *FCFunc = itFunc->second;
        if(FCFunc)
            FCFunc->SetParamRefFlags();
    }
}

FCDynamicClassDesc &FCDynamicClassDesc::CopyDesc(const FCDynamicClassDesc &other)
{
	if(this == &other)
	{
		return *this;
	}
	Clear();
	m_Struct = other.m_Struct;
	m_Class = other.m_Class;
    m_ScriptStruct = other.m_ScriptStruct;
	m_Super = other.m_Super;
	m_ClassFlags = other.m_ClassFlags;
	m_SuperName = other.m_SuperName;
	m_UEClassName = other.m_UEClassName;
    m_UserDefinedStructFlag = other.m_UserDefinedStructFlag;
    m_SystemFunctions = other.m_SystemFunctions;

	m_Property.resize(other.m_Property.size());
	m_Name2Property.clear();
	m_Fileds.clear();
	for(int i = 0; i<m_Property.size(); ++i)
	{
		FCDynamicProperty *FCProperty = new FCDynamicProperty(*(other.m_Property[i]));
		m_Property[i] = FCProperty;
		m_Name2Property[FCProperty->Name] = FCProperty;
		m_Fileds[FCProperty->Name] = FCProperty;
	}
	m_Functions.clear();
	for(CDynamicFunctionNameMap::const_iterator it = other.m_Functions.begin(); it != other.m_Functions.end(); ++it)
	{
		FCDynamicFunction *Func = new FCDynamicFunction(*(it->second));
		m_Functions[Func->Name] = Func;
		m_Fileds[Func->Name] = Func;
	}
	m_LibFields.clear();
	for (CDynamicFieldNameMap::const_iterator it = other.m_LibFields.begin(); it != other.m_LibFields.end(); ++it)
	{
		FCDynamicField* Filed = it->second->Clone();
		m_LibFields[Filed->GetFieldName()] = Filed;
		m_Fileds[Filed->GetFieldName()] = Filed;
	}
	return *this;
}

void  FCDynamicClassDesc::OnRegisterStruct(UStruct *Struct, void *Context)
{
	// 先注册父对象	
	m_Struct = Struct;
	UStruct  *Super = Struct->GetSuperStruct();
	if(Super)
	{
		m_SuperName = TCHAR_TO_UTF8(*(Super->GetName()));
        m_SuperName = GetConstName(m_SuperName);
	}
	m_Class = Cast<UClass>(Struct);
    m_ScriptStruct = Cast<UScriptStruct>(m_Struct);

    FCScriptContext* ScriptContext = (FCScriptContext*)Context;
    UStruct* SuperStruct = Struct->GetSuperStruct();
	if(SuperStruct)
	{
		m_Super = ScriptContext->RegisterUStruct(SuperStruct);
	}
}

bool IsSamePropertyName(const char *InPropertyName, const char *InFieldName)
{
    // ID_xxxxx
    for(; *InFieldName; ++InPropertyName, ++InFieldName)
    {
        if(*InPropertyName != *InFieldName)
        {
            break;
        }
    }
    if(*InPropertyName == 0)
    {
        return false;
    }
    return InPropertyName[1] == '_' || InPropertyName[1] == 0;
}

FCDynamicField* FCDynamicClassDesc::RegisterFieldByCString(UStruct* Struct, const char* InFieldName)
{
    CDynamicFieldNameMap::iterator itFiled = m_Fileds.find(InFieldName);
    if (itFiled != m_Fileds.end())
    {
        return itFiled->second;
    }

    FName  PropertyName(InFieldName);
    // 注册一下原生的函数
    if (m_Class)
    {
        UFunction* Function = m_Class->FindFunctionByName(PropertyName);
        if (Function)
        {
            FCDynamicFunction* DynamicFunction = new FCDynamicFunction();
            DynamicFunction->InitParam(Function);
            DynamicFunction->bInterface = m_Class->HasAnyClassFlags(CLASS_Interface);
            DynamicFunction->FieldName = InFieldName;
            const char* FieldName = DynamicFunction->Name;
            m_Functions[FieldName] = DynamicFunction;
            m_Fileds[FieldName] = DynamicFunction;
            return DynamicFunction;
        }
    }

    const FProperty* Property = Struct->FindPropertyByName(PropertyName);
    if(!Property)
    {
        if(!m_UserDefinedStructFlag)
        {
            UUserDefinedStruct* UserStruct = Cast<UUserDefinedStruct>(Struct);
            m_UserDefinedStructFlag = UserStruct ? 1 : -1;
        }
        if(m_UserDefinedStructFlag > 0)
        {
            FText  DisplayName(FText::FromName(PropertyName));
            for (FProperty* ItProperty = Struct->PropertyLink; ItProperty != NULL; ItProperty = ItProperty->PropertyLinkNext)
            {
                const char *Name = TCHAR_TO_UTF8(*(ItProperty->GetName()));
                if(IsSamePropertyName(Name, InFieldName))
                {
                    Property = ItProperty;
                    break;
                }
            }
        }
    }
    if (Property)
    {
        FCDynamicProperty* FCProperty = new FCDynamicProperty();
        FCProperty->InitProperty(Property, InFieldName);
        FCProperty->PropertyIndex = m_Property.size();
        FCProperty->bOuter = false;

        const char* FieldName = FCProperty->Name;
        m_Property.push_back(FCProperty);
        m_Name2Property[FieldName] = FCProperty;
        m_Fileds[FieldName] = FCProperty;
        return FCProperty;
    }

    if (m_Super)
    {
        return m_Super->RegisterFieldByCString(m_Super->m_Struct, InFieldName);
    }
    else
    {
        // 处理全局的
        if(m_ScriptStruct)
        {
            if(strcmp(InFieldName, "Copy") == 0)
            {
                FCDynamicWrapLibFunction* FCProperty = GetScriptContext()->m_CopyWrapFunc;
                const char* FieldName = FCProperty->Name;
                m_Fileds[FieldName] = FCProperty;
                return FCProperty;
            }
            else if (strcmp(InFieldName, "CopyFrom") == 0)
            {
                FCDynamicWrapLibFunction* FCProperty = GetScriptContext()->m_CopyFromWrapFunc;
                const char* FieldName = FCProperty->Name;
                m_Fileds[FieldName] = FCProperty;
                return FCProperty;
            }
        }
    }
    return nullptr;
}

int   FCDynamicClassDesc::GetMemSize() const
{
    int MemSize = sizeof(FCDynamicClassDesc);
    for(CDynamicPropertyPtrArray::const_iterator itProperty = m_Property.begin(); itProperty != m_Property.end(); ++itProperty)
    {
        const FCDynamicProperty *Property = (*itProperty);
        if(Property)
            MemSize += Property->GetMemSize();
    }
    for(CDynamicFunctionNameMap::const_iterator itFunc = m_Functions.begin(); itFunc != m_Functions.end(); ++itFunc)
    {
        const FCDynamicFunction *DynamicFunction = itFunc->second;
        if(DynamicFunction)
            MemSize += DynamicFunction->GetMemSize();
    }
    for(CDynamicFieldNameMap::const_iterator itExtion = m_LibFields.begin(); itExtion != m_LibFields.end(); ++itExtion)
    {
        MemSize += itExtion->second->GetMemSize();
    }
    return MemSize;
}

FCDynamicFunction*  FCDynamicClassDesc::RegisterUEFunc(const char *pcsFuncName)
{
	if(!m_Class)
	{
		return nullptr;
	}
	// UE的反射并不支持同名函数重载
	CDynamicFunctionNameMap::iterator itFunction = m_Functions.find(pcsFuncName);
	if (itFunction != m_Functions.end())
	{
		return itFunction->second;
	}

	FName  Name(pcsFuncName);
	UFunction* Function = m_Class->FindFunctionByName(Name);
	if (!Function)
	{
		if(m_Super)
			return m_Super->RegisterUEFunc(pcsFuncName);
		else
			return nullptr;
	}
	FCDynamicFunction* DynamicFunction = new FCDynamicFunction();
	DynamicFunction->InitParam(Function);
    DynamicFunction->FieldName = GetConstName(pcsFuncName);
	m_Functions[DynamicFunction->Name] = DynamicFunction;
	m_Fileds[DynamicFunction->Name] = DynamicFunction;

	return DynamicFunction;
}

// 功能：注册一个类的属性
FCDynamicProperty*  FCDynamicClassDesc::RegisterProperty(const char * InPropertyName)
{
    CDynamicName2Property::iterator itAttrib = m_Name2Property.find(InPropertyName);
    if (itAttrib != m_Name2Property.end())
    {
        return itAttrib->second;
    }
    FName InFieldName(InPropertyName);
    const FProperty* Property = m_Struct->FindPropertyByName(InFieldName);
    if (Property)
    {
        FCDynamicProperty* FCProperty = new FCDynamicProperty();
        FCProperty->InitProperty(Property);
        FCProperty->PropertyIndex = m_Property.size();
        FCProperty->bOuter = false;

        const char* FieldName = FCProperty->Name;
        m_Property.push_back(FCProperty);
        m_Name2Property[FieldName] = FCProperty;
        m_Fileds[FieldName] = FCProperty;
        return FCProperty;
    }

    if(m_Super)
    {
        FCDynamicProperty* FCProperty = m_Super->RegisterProperty(InPropertyName);
        if (FCProperty)
        {
            const char* FieldName = FCProperty->Name;
            m_Name2Property[FieldName] = FCProperty;
            return FCProperty;
        }
    }
	return nullptr;
}
// 功能：注册一个函数
FCDynamicFunction*  FCDynamicClassDesc::RegisterFunc(const char *pcsFuncName)
{
	FCDynamicFunction *DynamicFunction = RegisterUEFunc(pcsFuncName);
	if (!DynamicFunction)
	{
		UE_LOG(LogFCScript, Warning, TEXT("failed register function: %s, class name: %s"), UTF8_TO_TCHAR(pcsFuncName), UTF8_TO_TCHAR(m_UEClassName));
	}
	return DynamicFunction;
}

FCDynamicField* FCDynamicClassDesc::RegisterWrapLibFunction(const char* pcsFuncName, LPWrapLibFunction InGetFunc, LPWrapLibFunction InSetFunc)
{
	CDynamicFieldNameMap::iterator itFiled = m_LibFields.find(pcsFuncName);
	if (itFiled != m_LibFields.end())
	{
		return itFiled->second;
	}
	FCDynamicWrapLibFunction* LibField = new FCDynamicWrapLibFunction(InGetFunc, InSetFunc);
	LibField->Name = GetConstName(pcsFuncName);
	m_LibFields[LibField->Name] = LibField;
	m_Fileds[LibField->Name] = LibField;
	return LibField;
}

FCDynamicField* FCDynamicClassDesc::RegisterWrapLibAttrib(const char* pcsFuncName, LPWrapLibFunction InGetFunc, LPWrapLibFunction InSetFunc)
{
	CDynamicFieldNameMap::iterator itFiled = m_LibFields.find(pcsFuncName);
	if (itFiled != m_LibFields.end())
	{
		return itFiled->second;
	}
	FCDynamicWrapLibAttrib* LibField = new FCDynamicWrapLibAttrib(InGetFunc, InSetFunc);
	LibField->Name = GetConstName(pcsFuncName);
	m_LibFields[LibField->Name] = LibField;
	m_Fileds[LibField->Name] = LibField;
	return LibField;
}

void FCDynamicClassDesc::AddSystemFunction(const char* InFuncName, LPLuaLibOpenCallback InFuncPtr)
{
    InFuncName = GetConstName(InFuncName);
    FCDynamicWrapSystemFunction  Function(InFuncName, InFuncPtr);
    m_SystemFunctions.push_back(Function);
}

void FCDynamicClassDesc::OnLibOpen(lua_State* L)
{
    if (m_LibOpenCallback)
        m_LibOpenCallback(L);
    for(size_t i = 0; i< m_SystemFunctions.size(); ++i)
    {
        const FCDynamicWrapSystemFunction &Function = m_SystemFunctions[i];
        lua_pushstring(L, Function.FuncName);
        lua_pushcfunction(L, Function.m_FunctionPtr);
        lua_rawset(L, -3);        
    }
}

//---------------------------------------------------------------------------

void FDynamicEnum::OnRegisterEnum(UEnum* InEnum)
{
    EnumValue.clear();
    m_NameToValue.clear();

    int32 NumEntries = InEnum->NumEnums();
    EnumValue.resize(NumEntries);
    FString Name;
    FString DisplayName;
    for (int32 i = 0; i < NumEntries; ++i)
    {
        SDynamicEnumValue &Value = EnumValue[i];
        DisplayName = InEnum->GetDisplayNameTextByIndex(i).ToString();
        Name = InEnum->GetNameStringByIndex(i);
        Value.Name = TCHAR_TO_UTF8(*Name);
        Value.DisplayName = TCHAR_TO_UTF8(*DisplayName);
        Value.Value = InEnum->GetValueByIndex(i);
        m_NameToValue[Value.Name.c_str()] = Value.Value;
        m_NameToValue[Value.DisplayName.c_str()] = Value.Value;
    }
}

//---------------------------------------------------------------------------
const char* GetUEClassName(const char* InName)
{
	const char* Name = (InName[0] == 'U' || InName[0] == 'A' || InName[0] == 'F' || InName[0] == 'E') ? InName + 1 : InName;
	return Name;
}

int  FCScriptContext::QueryLuaRef(lua_State* Thread)
{
    ThreadToRefMap::iterator itRef = m_ThreadToRef.find(Thread);
    int32 ThreadRef = 0;
    if (itRef == m_ThreadToRef.end())
    {
        int32 Value = lua_pushthread(Thread);
        if (Value == 1)
        {
            lua_pop(Thread, 1);
            UE_LOG(LogFCScript, Warning, TEXT("%s: Can't call latent action in main lua thread!"), ANSI_TO_TCHAR(__FUNCTION__));
            return 0;
        }
        ThreadRef = luaL_ref(Thread, LUA_REGISTRYINDEX);
        m_ThreadToRef[Thread] = ThreadRef;
        m_RefToThread[ThreadRef] = Thread;
    }
    else
    {
        ThreadRef = itRef->second;
    }
    return ThreadRef;
}

void FCScriptContext::ResumeThread(int ThreadRef)
{
    RefToThreadMap::iterator itThread = m_RefToThread.find(ThreadRef);
    if (itThread != m_RefToThread.end())
    {
        lua_State* L = m_LuaState;

        lua_State* Thread = itThread->second;
#if 504 == LUA_VERSION_NUM
        int NResults = 0;
        int32 Status = lua_resume(Thread, L, 0, &NResults);
#else
        int32 Status = lua_resume(Thread, L, 0);
#endif
        if (Status == LUA_OK)
        {
            m_ThreadToRef.erase(Thread);
            m_RefToThread.erase(itThread);
            luaL_unref(L, LUA_REGISTRYINDEX, ThreadRef);    // remove the reference if the coroutine finishes its execution
        }
    }
}

FCDynamicClassDesc* FCScriptContext::RegisterCppClass(const char* UEClassName)
{
    CDynamicClassNameMap::iterator itClass = m_ClassFinder.find(UEClassName);
    if (itClass != m_ClassFinder.end())
    {
        return itClass->second;
    }
    UEClassName = GetConstName(UEClassName);

    FCDynamicClassDesc* ScriptClassDesc = new FCDynamicClassDesc();
    ScriptClassDesc->m_UEClassName = UEClassName;
    m_ClassNameMap[UEClassName] = ScriptClassDesc;
    m_ClassFinder[UEClassName] = ScriptClassDesc;
    return ScriptClassDesc;
}

FCDynamicClassDesc*  FCScriptContext::RegisterUClass(const char *UEClassName)
{
	CDynamicClassNameMap::iterator itClass = m_ClassFinder.find(UEClassName);
	if( itClass != m_ClassFinder.end())
	{
		return itClass->second;
	}
	int  NameOffset = (UEClassName[0] == 'U' || UEClassName[0] == 'A' || UEClassName[0] == 'F' || UEClassName[0] == 'E') ? 1 : 0;
    if(NameOffset > 0)
    {
        itClass = m_ClassFinder.find(UEClassName + NameOffset);
        if (itClass != m_ClassFinder.end())
        {
            UEClassName = GetConstName(UEClassName) + NameOffset;
            m_ClassFinder[UEClassName] = itClass->second;
            return itClass->second;
        }
    }

	const TCHAR *InName = UTF8_TO_TCHAR(UEClassName);
	const TCHAR *Name = InName + NameOffset;
	UStruct *Struct = FindObject<UStruct>(ANY_PACKAGE, Name);       // find first
	if (!Struct)
	{
		Struct = LoadObject<UStruct>(nullptr, Name);                // load if not found
	}
	if (!Struct)
	{
		Struct = FindObject<UStruct>(ANY_PACKAGE, InName);        // find first
		if(!Struct)
			Struct = LoadObject<UStruct>(nullptr, InName);        // load if not found
		if(!Struct)
			return nullptr;
		Name = InName;
		NameOffset = 0;
	}

    UEClassName = GetConstName(UEClassName);

	FCDynamicClassDesc *ScriptClassDesc = new FCDynamicClassDesc();
	ScriptClassDesc->m_UEClassName = UEClassName;
	ScriptClassDesc->OnRegisterStruct(Struct, this);

	m_ClassNameMap[UEClassName] = ScriptClassDesc;
	m_StructMap[Struct] = ScriptClassDesc;

	m_ClassFinder[UEClassName] = ScriptClassDesc;
	if(NameOffset > 0)
		m_ClassFinder[UEClassName+1] = ScriptClassDesc;

	return ScriptClassDesc;
}

FCDynamicClassDesc*  FCScriptContext::RegisterUStruct(UStruct *Struct)
{
    if(!Struct)
        return nullptr;
	CDynamicUStructMap::iterator itStruct = m_StructMap.find(Struct);
	if(itStruct != m_StructMap.end())
	{
		return itStruct->second;
	}
	FCScriptContext *ScriptContext = GetScriptContext();

	FString   UEClassName = Struct->GetName();
	FCDynamicClassDesc *ScriptClassDesc = new FCDynamicClassDesc();
	ScriptClassDesc->m_UEClassName = TCHAR_TO_UTF8(*UEClassName);
    ScriptClassDesc->m_UEClassName = GetConstName(ScriptClassDesc->m_UEClassName);
	ScriptClassDesc->OnRegisterStruct(Struct, this);

	const char* ClassName = ScriptClassDesc->m_UEClassName;
	m_ClassNameMap[ClassName] = ScriptClassDesc;
	m_StructMap[Struct] = ScriptClassDesc;

	m_ClassFinder[ClassName] = ScriptClassDesc;
	return ScriptClassDesc;
}

FCDynamicClassDesc* FCScriptContext::RegisterByProperty(FProperty* Property)
{
    CDynamicPropertyMap::iterator itClass = m_PropeytyMap.find(Property);
    if(itClass != m_PropeytyMap.end())
    {
        return itClass->second;
    }
    FFieldClass* FieldClass = Property->GetClass();
    FString  Name = FieldClass->GetName();

    FCDynamicClassDesc *ClassDesc = RegisterUClass(TCHAR_TO_UTF8(*Name));
    m_PropeytyMap[Property] = ClassDesc;
    
    return ClassDesc;
}

FDynamicEnum* FCScriptContext::RegisterEnum(const char* InEnumName)
{
    if(!InEnumName)
    {
        return nullptr;
    }
    CDynamicEnumNameMap::iterator itEnum = m_EnumNameMap.find(InEnumName);
    if (itEnum != m_EnumNameMap.end())
    {
        return itEnum->second;
    }

    FDynamicEnum  *ScriptEnumDesc = new FDynamicEnum();
    m_EnumNameMap[InEnumName] = ScriptEnumDesc;

    const TCHAR *Name = UTF8_TO_TCHAR(InEnumName);
    UEnum* Enum = FindObject<UEnum>(ANY_PACKAGE, Name);
    if (!Enum)
    {
        Enum = LoadObject<UEnum>(nullptr, Name);
    }
    if(Enum)
    {
        ScriptEnumDesc->OnRegisterEnum(Enum);
    }
    return ScriptEnumDesc;
}

int FCScriptContext::GetMemSize() const
{
    int MemSize = 0;
    for(CDynamicClassNameMap::const_iterator itClass = m_ClassNameMap.begin(); itClass != m_ClassNameMap.end(); ++itClass)
    {
        MemSize += itClass->second->GetMemSize();
    }
    return MemSize;
}

int FCScriptContext::GetClassMemSize(const char* InClassName) const
{
    CDynamicClassNameMap::const_iterator itClass = m_ClassFinder.find(InClassName);
    if (itClass != m_ClassFinder.end())
    {
        return itClass->second->GetMemSize();
    }
    return 0;
}

void FCScriptContext::Init()
{
    if(!m_ManualObjectReference)
    {
        m_ManualObjectReference = new FCObjectReferencer();
        m_ManualObjectReference->SetName("FUnLua_ManualReference");
    }
    if(!m_CopyWrapFunc)
    {
        m_CopyWrapFunc = new FCDynamicWrapLibFunction(ScriptStruct_Copy_wrap, nullptr);
        m_CopyWrapFunc->Name = "Copy";
        m_CopyWrapFunc->FieldName = m_CopyWrapFunc->Name;
    }
    if(!m_CopyFromWrapFunc)
    {
        m_CopyFromWrapFunc = new FCDynamicWrapLibFunction(ScriptStruct_CopyFrom_wrap, nullptr);
        m_CopyFromWrapFunc->Name = "CopyFrom";
        m_CopyFromWrapFunc->FieldName = m_CopyFromWrapFunc->Name;
    }
}

void FCScriptContext::Clear()
{
	m_bInit = false;
    if(m_LuaState)
    {
        lua_close(m_LuaState);
        m_LuaState = nullptr;
    }
    m_Ticker = nullptr;
    m_DelegateObject = nullptr;

	ReleasePtrMap(m_ClassNameMap);
    ReleasePtrMap(m_EnumNameMap);
	m_StructMap.clear();
    m_PropeytyMap.clear();
	m_ClassFinder.clear();
    m_ThreadToRef.clear();
    m_RefToThread.clear();

    if(m_ManualObjectReference)
    {
        m_ManualObjectReference->Clear();
        delete m_ManualObjectReference;
        m_ManualObjectReference = nullptr;
    }
    if(m_CopyWrapFunc)
    {
        delete m_CopyWrapFunc;
        m_CopyWrapFunc = nullptr;
    }
    if(m_CopyFromWrapFunc)
    {
        delete m_CopyFromWrapFunc;
        m_CopyFromWrapFunc = nullptr;
    }
}

void FCScriptContext::ClearNoneRefField()
{
    for(CDynamicClassNameMap::iterator itClass = m_ClassFinder.begin(); itClass != m_ClassFinder.end(); ++itClass)
    {
        itClass->second->ClearNoneRefField();
    }
}

//--------------------------------------------------------

FCContextManager* FCContextManager::ConextMgrInsPtr = nullptr;
FCContextManager::FCContextManager()
{
	ConextMgrInsPtr = this;
}

FCContextManager::~FCContextManager()
{
	ConextMgrInsPtr = nullptr;
	Clear();
}

void FCContextManager::Init()
{
    ClientDSContext.Init();
}

void  FCContextManager::Clear()
{
	ClientDSContext.Clear();
}

FCContextManager  GContextNgr;
