require "UnLua.UnLua"
local TestCrash = require "UnLua.UI.UnLuaCrashTest"
local UMG_MainPanel = LuaUnrealClass()

_G.handle = function  (target, func)
    return function (...)
       return func(target,  ...)
    end
end

function UMG_MainPanel:Construct()
    UEPrint("[Unlua]UMG_MainPanel:Construct, Button_0=", self.Button_0)
    self:BindButton(self.Button_0, self.OnClickButton_0)
    self:BindButton(self.ButtonGC, self.OnClickGC)
    self:BindButton(self.Button_1, self.OnClickButton_1)
    self:BindButton(self.ButtonProfile, self.OnClickProfile)
    self:BindButton(self.ButtonCrash, self.OnButtonCrashClicked)
        
	local world = self:GetWorld()
	local localPlayerControler = _G.UGameplayStatics.GetPlayerController(world, 0)
	if localPlayerControler ~= nil then
		localPlayerControler.bShowMouseCursor = 1
	end
	self.MapName:SetText(world:GetName())
end

function UMG_MainPanel:BindButton(button, func)
    button.OnClicked:Add(self, handle(self, func))
end

function UMG_MainPanel:OnClickButton_0()
    UEPrint("[Unlua]UMG_MainPanel:OnClickButton_0")
end

function UMG_MainPanel:OnClickGC()
    UEPrint("[Unlua]UMG_MainPanel:OnClickGC")
	collectgarbage("collect")
    _G.UKismetSystemLibrary.CollectGarbage()  -- 立即GC一下
end

function UMG_MainPanel:OnClickButton_1()
    UEPrint("[Unlua]UMG_MainPanel:OnClickButton_1")
end

function UMG_MainPanel:OnClickProfile()
    UEPrint("[Unlua]UMG_MainPanel:OnClickPrint")    
    local UIManager = require "UnLua.UI.UIManager"
	local ClassName = "/Game/UnLua/UI/UMG/UMG_Profile"    
	local world = self:GetWorld()
    UIManager.OpenPanel(world, ClassName)
end

function UMG_MainPanel:OnButtonCrashClicked()    
	local world = self:GetWorld()
    TestCrash:DoCrash(world)
end

function UMG_MainPanel:OnMouseButtonDown(MyGeometry, MouseEvent)
	print("[Unlua]OnMouseButtonDown")
	local world = self:GetWorld()
	local localPlayerControler = _G.UGameplayStatics.GetPlayerController(world, 0)
	if localPlayerControler ~= nil then
		localPlayerControler.bShowMouseCursor = 1
	end
	print("[Unlua]OnMouseButtonDown, world=", world,  ",localPlayerControler=", localPlayerControler, ",bShowMouseCursor=", localPlayerControler.bShowMouseCursor)
	return UWidgetBlueprintLibrary.Handled()
end

function UMG_MainPanel:OnMouseButtonUp(MyGeometry, MouseEvent)
	print("[Unlua]OnMouseButtonUp")
	return UWidgetBlueprintLibrary.Handled()
end

return UMG_MainPanel