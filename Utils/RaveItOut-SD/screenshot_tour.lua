-- Screenshot tour for SD layout checks. Installed only by make_sd_theme.py --tour (never ship it).
local orig = LoadActor("_sdshot_orig.lua")
local function shortest()
  local best, bl
  for _, s in ipairs(SONGMAN:GetAllSongs()) do
    local st = s:GetStepsByStepsType("StepsType_Pump_Single")
    local l = s:MusicLengthSeconds()
    if st and st[1] and l > 20 and (not bl or l < bl) then best, bl = s, l end
  end
  return best
end
local steps = {
  {"ScreenOpening", 9}, {"ScreenTitleMenu", 5},
  {"ScreenSelectPlayMode", 6, function()
     GAMESTATE:JoinPlayer(PLAYER_1)
     GAMESTATE:SetCurrentStyle("single")
     GAMESTATE:SetCurrentPlayMode("PlayMode_Regular")
     setenv("HeaderTitle","SELECT MUSIC")
   end},
  {"ScreenSelectMusic", 8, function()
     local s = shortest()
     GAMESTATE:SetCurrentSong(s); GAMESTATE:SetPreferredSong(s)
     local st = s:GetStepsByStepsType("StepsType_Pump_Single")
     GAMESTATE:SetCurrentSteps(PLAYER_1, st[1])
   end},
  {"ScreenGameplay", 20},
  {"WAIT:ScreenEvaluationNormal", 8},
}
local i, n, waited = 0, 0, 0
local function shot(tag)
  n = n + 1
  SaveScreenshot(nil, false, false, string.format("sd%02d_%s_", n, tag), "")
end
return Def.ActorFrame{
  orig,
  Def.Actor{
    InitCommand=function(self) self:sleep(14):queuecommand("Next") end,
    NextCommand=function(self)
      local top = SCREENMAN:GetTopScreen()
      local name = top and top:GetName() or "none"
      local st = steps[i]
      if st and st[1]:sub(1,5) == "WAIT:" and name ~= st[1]:sub(6) and waited < 240 then
        waited = waited + 5; self:sleep(5):queuecommand("Next"); return
      end
      shot(name)
      if name == "ScreenGameplay" and top then
        -- end the song early, same as the debug overlay's "Send Notes Ended" (F3 menu)
        top:PostScreenMessage("SM_NotesEnded", 0)
      end
      if st and st[1]:sub(1,5) == "WAIT:" then
        self:sleep(st[2]):queuecommand("Shot2"); return
      end
      i = i + 1
      st = steps[i]
      if not st then SCREENMAN:SetNewScreen("ScreenExit"); return end
      if st[3] then pcall(st[3]) end
      if st[1]:sub(1,5) ~= "WAIT:" then SCREENMAN:SetNewScreen(st[1]) end
      self:sleep(st[2]):queuecommand("Next")
    end,
    Shot2Command=function(self) shot("eval_later"); SCREENMAN:SetNewScreen("ScreenExit") end,
  },
}
