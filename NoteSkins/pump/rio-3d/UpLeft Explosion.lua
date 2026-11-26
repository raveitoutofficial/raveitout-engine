-- 3D version of rio's tap explosion: the note cracks into a 3x3 grid of real
-- 3D pieces (built by Utils/noteskin3d/noteskin3d_pieces.py) that fly apart
-- exactly like the 2D pieces, plus the same spark and glow sprites.
-- (The 2D version crops copies of the tap-note sprite; crop doesn't work on
-- models, which made 9 whole 3D notes streak out.)
local button = Var "Button"
local base = "UpLeft"
if button == "Center" then
	base = "Center"
elseif button == "DownLeft" or button == "DownRight" then
	base = "DownLeft"
end
local mirrored = (button == "UpRight" or button == "DownRight")

local pieces = {
	upleft = -10, up = 0, upright = 10,
	left = -10, center = 0, right = 10,
	downleft = -10, down = 0, downright = 10,
}

local t = Def.ActorFrame{};

for key, wind in pairs(pieces) do
	if wind == 0 then wind = math.random(-5,5) end
	-- Mirroring the note also mirrors the wind, so flip it back (as in 2D).
	if mirrored then wind = -wind end

	t[#t+1] = Def.ActorFrame {
		Lighting=true;	-- same light as the 3D notes
		InitCommand=function(self)
			self:SetAmbientLightColor(color("0.55,0.55,0.55,1"))
			self:SetDiffuseLightColor(color("0.6,0.6,0.6,1"))
			self:SetSpecularLightColor(color("1,1,1,1"))
			self:SetLightDirection({-0.45,-0.55,0.7})
			if mirrored then self:rotationy(180) end
		end;
		Def.Model {
			Meshes=NOTESKIN:GetPath('_'..base..'Piece_'..key,'model');
			Materials=NOTESKIN:GetPath('_'..base..'Piece_'..key,'model');
			Bones=NOTESKIN:GetPath('_'..base..'Piece_'..key,'model');
			-- Fading pieces mustn't hide each other through the depth buffer.
			InitCommand=cmd(zbuffer,false;diffusealpha,0);
			W1Command=cmd(playcommand,"Crack");
			W2Command=cmd(playcommand,"Crack");
			W3Command=cmd(playcommand,"Crack");
			W4Command=cmd();
			W5Command=cmd();
			CrackCommand=function(self)
				(cmd(
					finishtweening;
					x,0;
					y,0;
					rotationx,0;
					rotationy,0;
					rotationz,0;
					diffusealpha,1;

					decelerate,.2;

					y,-45;
					rotationz,math.random(0,90);

					accelerate,.2;

					y,-10;
					diffusealpha,0;
					rotationz,90;
				))(self);
				(cmd(
					x,wind * 2;
					linear,4;
					x,0;
				))(self);
			end
		};
	};
end

-- Sparks (same as 2D).
t[#t+1] = Def.Sprite{
	Texture=NOTESKIN:GetPath( 'Explosion', 'Spark' );
	InitCommand=cmd(blend,"BlendMode_Add";pause;diffusealpha,0);
	W1Command=cmd(playcommand,"NewExplosion");
	W2Command=cmd(playcommand,"NewExplosion");
	W3Command=cmd(playcommand,"NewExplosion");
	HoldingOnCommand=cmd(playcommand,"NewExplosion");
	HeldCommand=cmd(playcommand,"NewExplosion");

	NewExplosionCommand=function(self)
		self:finishtweening();
		self:rotationz(math.random(0,360));
		self:aux(0);
		self:playcommand("Advance");
	end;

	AdvanceCommand=function(self)
		if self:getaux() < self:GetNumStates()-1 then
			self:queuecommand("Advance");
		else
			self:diffusealpha(0);
			return;
		end
		self:aux(self:getaux()+1);
		self:setstate(self:getaux());
		self:diffusealpha(1);
		self:linear(0.01);
	end;
};

-- Glow (same as 2D).
t[#t+1] = Def.Sprite{
	Texture=NOTESKIN:GetPath( 'Explosion', 'Glow' );
	InitCommand=cmd(blend,"BlendMode_Add";diffusealpha,0);
	W1Command=cmd(playcommand,"Glow");
	W2Command=cmd(playcommand,"Glow");
	W3Command=cmd(playcommand,"Glow");
	HoldingOnCommand=cmd(playcommand,"Glow");
	HeldCommand=cmd(playcommand,"Glow");
	GlowCommand=cmd(
		finishtweening;
		rotationz,math.random(0,360);
		zoom,.9;
		diffusealpha,1;
		decelerate,.3;
		diffusealpha,0;
		zoom,.75;
	);
};

return t;
