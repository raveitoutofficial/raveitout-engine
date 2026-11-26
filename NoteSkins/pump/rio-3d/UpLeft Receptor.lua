-- TEST: 3D receptor. Same as pump/default's receptor, but the base is a
-- beveled 3D model built from the rio receptor art; the beat glow stays 2D
-- on top. Right-side receptors are this flipped 180 degrees on Y by
-- NoteSkin.lua (the model's back face carries the same art).
local player = Var "Player" or GAMESTATE:GetMasterPlayerNumber()
local button = Var "Button"
local model = "UpLeft"
if button == "Center" then
	model = "Center"
elseif button == "DownLeft" or button == "DownRight" then
	model = "DownLeft"
end

local function Beat(self)
	local this = self:GetChildren()
	local songposition = GAMESTATE:GetPlayerState( player ):GetSongPosition()
	local beat = songposition:GetSongBeat()
	local part = clamp(beat % 1, 0, 0.5)
	local eff = scale(part, 0, 0.5, 1, 0)
	if (songposition:GetDelay() or false) and part == 0 then eff = 0 end
	if beat < 0 then eff = 0 end
	this.Glow:diffusealpha(eff)
end

local press = cmd(finishtweening;linear,0.05;zoom,0.9;linear,0.1;zoom,1)

return Def.ActorFrame {
	InitCommand=cmd(SetUpdateFunction,Beat);

	NOTESKIN:LoadActor("Center", "Outline Receptor")..{
		Name="Outline Full";
		Condition=button == "Center" and GAMESTATE:GetCurrentStyle():GetStepsType() ~= 'StepsType_Pump_Halfdouble';
	};
	NOTESKIN:LoadActor("DownLeft", "Outline Receptor")..{
		Name="Outline Half";
		Condition=button == "DownLeft" and GAMESTATE:GetCurrentStyle():GetStepsType() == 'StepsType_Pump_Halfdouble';
	};

	Def.ActorFrame {
		Name="Base";
		Lighting=true;	-- lit like the 3D notes
		InitCommand=cmd(SetAmbientLightColor,color("0.55,0.55,0.55,1");SetDiffuseLightColor,color("0.6,0.6,0.6,1");SetSpecularLightColor,color("1,1,1,1");SetLightDirection,{-0.45,-0.55,0.7});
		PressCommand=press;
		Def.Model {
			-- No depth buffer: notes must always draw over the receptors (like the
			-- 2D skin). Back-face culling (Model default) keeps the receptor itself right.
			InitCommand=cmd(zbuffer,false);
			Meshes=NOTESKIN:GetPath('_'..model..'Receptor','model');
			Materials=NOTESKIN:GetPath('_'..model..'Receptor','model');
			Bones=NOTESKIN:GetPath('_'..model..'Receptor','model');
		};
	};
	NOTESKIN:LoadActor(button, "Ready Receptor")..{
		Name="Glow";
		Frames={ { Frame = 1 } };
		InitCommand=cmd(blend,'BlendMode_Add');
		PressCommand=press;
	};
}
