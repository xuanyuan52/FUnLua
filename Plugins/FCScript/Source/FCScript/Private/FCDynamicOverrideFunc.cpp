#include "FCDynamicOverrideFunc.h"
#include "FCDynamicClassDesc.h"
#include "FCObjectManager.h"
#include "FCCallScriptFunc.h"
#include "Engine/World.h"
#include "FCGetObj.h"
#include "FCRunTimeRegister.h"
#include "FCInputReplace.h"

#include "../LuaCore/LuaContext.h"

#include "Animation/AnimInstance.h"


UFunction  *FirstNative(UObject* Context, FFrame& TheStack, bool &bUnpackParams)
{
	UFunction* Func = TheStack.Node;
	if (TheStack.CurrentNativeFunction)
	{
		if (Func != TheStack.CurrentNativeFunction)
		{
			Func = TheStack.CurrentNativeFunction;
			bUnpackParams = true;
		}
		else
		{
			if (Func->GetNativeFunc() == (FNativeFuncPtr)&FCDynamicOverrideNative)
			{
				TheStack.SkipCode(1);      // skip EX_CallLua only when called from native func
			}
		}
	}
	return Func;
}

UFunction* FindOrDumpFunction(UFunction* SrcFunction, UClass* OuterClass, const FName& NewFuncName)
{
    UFunction* LuaFunction = OuterClass->FindFunctionByName(NewFuncName);
    if (!LuaFunction)
    {
        UClass* SrcFuncClass = SrcFunction->GetClass();

        FObjectDuplicationParameters DuplicationParams(SrcFunction, OuterClass);
        DuplicationParams.InternalFlagMask &= ~EInternalObjectFlags::Native;
        DuplicationParams.DestName = NewFuncName;
        DuplicationParams.DestClass = SrcFuncClass; //  UFCLuaFunction::StaticClass();
        LuaFunction = static_cast<UFunction*>(StaticDuplicateObjectEx(DuplicationParams));
        LuaFunction->FunctionFlags |= FUNC_Native;
        LuaFunction->ClearInternalFlags(EInternalObjectFlags::Native);
        //LuaFunction->SetNativeFunc(InFunc);

        LuaFunction->SetSuperStruct(SrcFunction->GetSuperStruct());

        if (!FPlatformProperties::RequiresCookedData())
            UMetaData::CopyMetadata(SrcFunction, LuaFunction);

        LuaFunction->StaticLink(true);

        OuterClass->AddFunctionToFunctionMap(LuaFunction, NewFuncName);
    }

    // 如果当前函数与来源的函数参数不一样，修复一下
    if (LuaFunction->NumParms != SrcFunction->NumParms)
    {
        LuaFunction->NumParms = SrcFunction->NumParms;
        LuaFunction->ParmsSize = SrcFunction->ParmsSize;
        LuaFunction->ReturnValueOffset = SrcFunction->ReturnValueOffset;
        LuaFunction->RPCId = SrcFunction->RPCId;
        LuaFunction->RPCResponseId = SrcFunction->RPCResponseId;
    }
    return LuaFunction;
}

void FCDynamicOverrideNative(UObject* Context, FFrame& TheStack, RESULT_DECL)
{
	// 从FCScriptContext中查找到注册的Context对象, 如果有就调用对应的事件
	// 如果没有找到，就对所有的FCScriptContext广播
	UObject  *Object = TheStack.Object;

	bool bUnpackParams = false;
	UFunction* Func = FirstNative(Context, TheStack, bUnpackParams);
	FCDynamicOverrideFunction *DynamicFunction = FFCObjectdManager::GetSingleIns()->ToOverrideFunction(Object, Func, FCDynamicOverrideNative, EX_CallFCOverride);

	FCScriptContext  *ScriptContext = GetScriptContext();
	if(DynamicFunction)
	{
		if(Object)
		{
            int64 ScriptIns = FFCObjectdManager::GetSingleIns()->FindOverrideScriptIns(Object, Func);
			if (ScriptIns)
			{
				if(FCCallScriptFunc(ScriptContext->m_LuaState, Object, ScriptIns, DynamicFunction->GetLuaFunctionName(), DynamicFunction, TheStack))
                {
                    return ;
                }
			}
		}
		else
		{
			if(FCCallScriptFunc(ScriptContext->m_LuaState, Object, 0, DynamicFunction->GetLuaFunctionName(), DynamicFunction, TheStack))
            {
                return ;
            }
		}
		// 再调用基类的
		if(DynamicFunction->OleNativeFuncPtr != FCDynamicOverrideNative)
		{
			DynamicFunction->OleNativeFuncPtr(Context, TheStack, RESULT_PARAM);
		}
	}
}

struct FUnluaOverrideCallInfo
{
	UObject* Object;
	FCDynamicClassDesc* ClassDesc;
	FBindObjectInfo* BindInfo;
};

bool UnLuaOverride_IsRPCName(const char *FuncName, int Len)
{
    if(Len > 4)
    {
        return FuncName[Len - 4] == '_' && FuncName[Len-3] == 'R' && FuncName[Len - 2] == 'P' && FuncName[Len - 1] == 'C';
    }
    return false;
}

bool UnLuaOverride_IsAniNotify(const char *FuncName, int Len)
{
    // AnimNotify_
    if(Len >11)
    {
        return memcmp(FuncName, "AnimNotify_", 11) == 0;
    }
    return false;
}

void UnLuaOverride_Callback(lua_State* L, int Index, void* UserData)
{
	FUnluaOverrideCallInfo* CallInfo = (FUnluaOverrideCallInfo*)UserData;
	int ValueType = lua_type(L, -2);
	if (ValueType == LUA_TFUNCTION)
	{
		const char* FuncName = lua_tostring(L, -1);
        int  Len = strlen(FuncName);
        if(UnLuaOverride_IsRPCName(FuncName, Len))
        {
            const char *RPCName = FuncName;
            FCStringBuffer128  NameBuffer;
            NameBuffer.PushStr(FuncName, Len - 4);
            FuncName = NameBuffer.GetString();
            const FCDynamicFunction* DynamicFunction = CallInfo->ClassDesc->FindFunctionByName(FuncName);
            if (DynamicFunction)
            {
                FCDynamicOverrideFunction *OverrideFunction = FFCObjectdManager::GetSingleIns()->RegisterOverrideFunc(CallInfo->Object, CallInfo->BindInfo->m_ScriptIns, FuncName);
                if(OverrideFunction)
                    OverrideFunction->LuaFunctionMame = GetConstName(RPCName);
            }
        }
        else
        {
            const FCDynamicFunction* DynamicFunction = CallInfo->ClassDesc->FindFunctionByName(FuncName);
            if (DynamicFunction)
            {                
                FFCObjectdManager::GetSingleIns()->RegisterOverrideFunc(CallInfo->Object, CallInfo->BindInfo->m_ScriptIns, FuncName);
            }
            else if (UnLuaOverride_IsAniNotify(FuncName, Len))  // 这个感觉没有必要，在蓝图添加一个接口，然后Overriden就可以了
            {
                if (CallInfo->Object->GetClass()->IsChildOf<UAnimInstance>())
                {
                    FCInputReplace::GetIns().OverridenTriggerAnimNotify((UObject*)CallInfo->Object, FuncName, CallInfo->BindInfo->m_ScriptIns);
                }
            }
        }
	}
    else if(LUA_TTABLE == ValueType)
    {
        const char* FieldName = lua_tostring(L, -1);
        if(strcmp(FieldName, "Super") == 0)
        {
            int StartTop = lua_gettop(L);
            int TableIdx = -2 + lua_gettop(L) + 1;

            lua_pushstring(L, "__ObjectRefID");
            lua_pushinteger(L, CallInfo->BindInfo->m_ObjRefID);
            lua_rawset(L, TableIdx);

            LoopTable(L, TableIdx, UnLuaOverride_Callback, CallInfo);
            int CurTop = lua_gettop(L);
            if(CurTop > StartTop)
            {
                lua_pop(L, CurTop - StartTop);
            }
        }
    }
}

void FCDynamicOverrideBeginBeginPlay(UObject* Context, FFrame& TheStack, RESULT_DECL)
{
	// 先检查一下有没有绑定的对象
    UObject *Object = TheStack.Object;
	FBindObjectInfo  *BindInfo = FFCObjectdManager::GetSingleIns()->FindBindObjectInfo(Object);
	// 如果有的话，就检测一下没有创建脚本, 尝试创建脚本
		//	ENetMode  NetMode = World->GetNetMode();
		//	if(NM_DedicatedServer == NetMode)

	FCScriptContext  *ScriptContext = GetScriptContext();
	// 尝试调用函数
    FNativeFuncPtr OllNativeFuncPtr = FCDynamicOverrideBeginBeginPlay;
	if(ScriptContext && BindInfo)
    {
		bool bUnpackParams = false;
		UFunction* Func = FirstNative(Context, TheStack, bUnpackParams);
		FCDynamicOverrideFunction *DynamicFunction = FFCObjectdManager::GetSingleIns()->ToOverrideFunction(Object, Func, FCDynamicOverrideBeginBeginPlay, EX_CallFCBeginPlay);
		if(DynamicFunction)
        {
            OllNativeFuncPtr = DynamicFunction->OleNativeFuncPtr;
            int64 ScriptIns = FCDynamicBindScript(Object);
			if(ScriptIns > 0)
			{
				CallAnyScriptFunc(ScriptContext, BindInfo->m_ScriptIns, DynamicFunction->Name);
				return ;
			}
		}
    }
    // 调用基类的
    if (OllNativeFuncPtr != FCDynamicOverrideBeginBeginPlay)
    {
        OllNativeFuncPtr(Context, TheStack, RESULT_PARAM);
    }
}

int64 FCDynamicBindScript(UObject* InObject)
{
	FBindObjectInfo  *BindInfo = FFCObjectdManager::GetSingleIns()->FindBindObjectInfo(InObject);
	if(!BindInfo)
	{
		return 0;
	}
	FCScriptContext  *ScriptContext = GetScriptContext();
	if(!BindInfo->m_ScriptIns)
	{
		lua_State* L = ScriptContext->m_LuaState;
		FCDynamicClassDesc * ClassDesc = ScriptContext->RegisterUStruct(InObject->GetClass());
		const char* ClassName = ClassDesc->m_UEClassName;

		int StartTop = lua_gettop(L);
		int TableIdx = lua_gettop(L);
		bool bValidTable = false;
        lua_pushcfunction(L, ReportLuaCallError);
		FLuaRetValues  RetValue = CallGlobalLua(L, "require", BindInfo->m_ScriptName);
		if (RetValue.size() == 1)
		{
			int Type = lua_type(L, RetValue[0]);
			if (Type == LUA_TTABLE)
			{
				FLuaValue CloneValue = CloneLuaTable(L, RetValue[0]);
				if (CloneValue.bValid)
				{
					bValidTable = true;
					TableIdx = CloneValue.ValueIdx;
				}
			}
		}
		if (!bValidTable)
		{
			int CurTop = lua_gettop(L);
			lua_pop(L, CurTop - StartTop);
			return 0;
		}

		if (ClassDesc->m_Super)
		{
			const char* InSuperClassName = ClassDesc->m_SuperName;
			lua_pushstring(L, "ParentClass");                   // 2
			int Type = luaL_getmetatable(L, InSuperClassName);
			if (Type != LUA_TTABLE)
			{
				printf("Invalid super class, className:%s, SupperClassName:%s", ClassName, InSuperClassName);
			}
			lua_rawset(L, -3);
		}
		BindInfo->m_ObjRefID = FCGetObj::GetIns()->PushUObject(InObject);
		lua_pushvalue(L, TableIdx);
		BindInfo->m_ScriptIns = luaL_ref(L, LUA_REGISTRYINDEX);  // 将这个参数添加到全局引用表

		// 读取lua的函数，检查需要覆写的类, 这里可以做一个优化，记录一下需要overriden的名字列表，按绑定脚本做一个查询优化，避免每次去查lua table
		FUnluaOverrideCallInfo  CallInfo = { InObject, ClassDesc , BindInfo };
		LoopTable(L, TableIdx, UnLuaOverride_Callback, &CallInfo);
		lua_rawgeti(L, LUA_REGISTRYINDEX, BindInfo->m_ScriptIns);

		lua_pushstring(L, "__index");                   // 2  对不存在的索引(成员变量)访问时触发
		lua_pushlightuserdata(L, (void*)BindInfo->m_ObjRefID);
		lua_pushcclosure(L, BindScript_Index, 1);      // closure
		lua_rawset(L, -3);

		lua_pushstring(L, "__newindex");                // 2  对不存在的索引(成员变量)赋值时触发
		lua_pushlightuserdata(L, (void*)BindInfo->m_ObjRefID);
		lua_pushcclosure(L, BindScript_NewIndex, 1);           // 3
		lua_rawset(L, -3);

		lua_pushstring(L, "__eq");
		lua_pushcfunction(L, BindScript_Equal);
		lua_rawset(L, -3);

        // StaticClass, Overridden 转到UObject对象了

		lua_pushstring(L, "__ObjectRefID");             // 不要脚本层修改这个变量
		lua_pushinteger(L, BindInfo->m_ObjRefID);
		lua_rawset(L, -3);

		const void* TableAddr = lua_topointer(L, -1);   // 测试table的地址会不会变, 经过测试，这个地址，就是table本身的地址，是不会变的，可以用这个来反查
		lua_pushstring(L, "__TableAddr");
		lua_pushlightuserdata(L, (void*)TableAddr);  
		lua_rawset(L, -3);

		lua_pushvalue(L, -1);                           // set metatable to self
		lua_setmetatable(L, -2);

		//SetTableForClass(L, ClassName);                // 测试发现，这一步是不需要的, 就不能设置

		int CurTop = lua_gettop(L);
		if (CurTop > StartTop)
		{
			lua_pop(L, CurTop - StartTop);
		}

        FFCObjectdManager::GetSingleIns()->OnBindScript(BindInfo->m_ObjRefID, BindInfo->m_ScriptIns);

        // Initialize
        if(BindInfo->m_InitializerTableRef != LUA_NOREF)
        {
            FLuaTableRef  InitParam(BindInfo->m_InitializerTableRef);
            CallAnyScriptFunc(ScriptContext, BindInfo->m_ScriptIns, "Initialize", InitParam);

            luaL_unref(L, LUA_REGISTRYINDEX, BindInfo->m_InitializerTableRef);
            BindInfo->m_InitializerTableRef = LUA_NOREF;
        }
        else
        {
            CallAnyScriptFunc(ScriptContext, BindInfo->m_ScriptIns, "Initialize");
        }
	}
	return BindInfo->m_ScriptIns;
}

void FCDynamicOverrideDelegate(UObject* Context, FFrame& TheStack, RESULT_DECL)
{
	// 先检查一下有没有绑定的对象
	UObject* Object = TheStack.Object;
	// 再检查一下，没有当前函数
	FCScriptContext  *ScriptContext = GetScriptContext();
	if(ScriptContext)
	{
		UClass* Class = Object->GetClass();
		bool bUnpackParams = false;
		UFunction* Func = FirstNative(Context, TheStack, bUnpackParams);
		FCDynamicDelegateList *DelegateList = FFCObjectdManager::GetSingleIns()->FindDelegateFunction(Object);
		if(!DelegateList)
		{
			return ;
		}
		int Count = DelegateList->Delegates.size();
		for( int i = 0; i<Count; ++i)
		{
			FCDelegateInfo &Info = DelegateList->Delegates[i];
			if(Info.DynamicFunc->Function == Func)
			{
				FCCallScriptDelegate(ScriptContext->m_LuaState, Object, 0, Info, Info.DynamicFunc, TheStack);
			}
		}
	}
}
