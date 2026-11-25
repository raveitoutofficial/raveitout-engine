local pieces = {

	upleft = {
		croptop = 0;
		cropbottom = .66;
		cropleft = 0;
		cropright = .66;
		wind = -10;
	};

	up = {
		croptop = 0;
		cropbottom = .66;
		cropleft = .66;
		cropright = .66;
		wind = math.random(-5,5);
	};

	upright = {
		croptop = 0;
		cropbottom = .66;
		cropleft = .66;
		cropright = 0;
		wind = 10;
	};

	left = {
		croptop = .33;
		cropbottom = .33;
		cropleft = 0;
		cropright = 0.66;
		wind = -10;
	};

	center = {
		croptop = .33;
		cropbottom = .33;
		cropleft = .33;
		cropright = .33;
		wind = math.random(-5,5);
	};

	right = {
		croptop = .33;
		cropbottom = .33;
		cropleft = .66;
		cropright = 0;
		wind = 10;
	};

	downleft = {
		croptop = .66;
		cropbottom = 0;
		cropleft = 0;
		cropright = .66;
		wind = -10;
	};

	down = {
		croptop = .66;
		cropbottom = 0;
		cropleft = .33;
		cropright = .33;
		wind = math.random(-5,5);
	};

	downleft = {
		croptop = .66;
		cropbottom = 0;
		cropleft = .66;
		cropright = 0;
		wind = 10;
	};

};

local t = Def.ActorFrame{};

for key,piece in pairs(pieces) do
	if Var "Button" == 'UpRight' or Var "Button" == 'DownRight' then
		piece.wind = -piece.wind;
	end

	t[#t+1] = NOTESKIN:LoadActor(Var "Button", "Tap Note")..{
		InitCommand=cmd(
			animate,false;
			setstate,3;
			croptop,piece.croptop;
			cropbottom,piece.cropbottom;
			cropleft,piece.cropleft;
			cropright,piece.cropright;
			playcommand,"Glow"
		);
		W1Command=cmd(playcommand,"Glow");
		W2Command=cmd(playcommand,"Glow");
		W3Command=cmd(playcommand,"Glow");
		W4Command=cmd();
		W5Command=cmd();

		--HitMineCommand=cmd(playcommand,"Glow");
		--HeldCommand=cmd(playcommand,"Glow");
		GlowCommand=function(self,params)
			(cmd(
				finishtweening;
				diffusealpha,1;
				y,0;
				rotationx,0;
				rotationy,0;
				rotationz,0;

				decelerate,.2;

				y,-45;
				rotationz,math.random(0,90);

				accelerate,.2;

				y,-10;
				diffusealpha,0;
				rotationz,90;
			))(self);
			(cmd(
				x,piece.wind * 2;
				linear,4;
				x,0;
			))(self);
		end
	};
end;

	--explosion
t[#t+1] = LoadActor("flash")..{
	InitCommand=cmd(blend,"BlendMode_Add";playcommand,"Glow");
	W1Command=cmd(playcommand,"Glow");
	W2Command=cmd(playcommand,"Glow");
	W3Command=cmd(playcommand,"Glow");
	W4Command=cmd();
	W5Command=cmd();
	--HoldingOnCommand=cmd(playcommand,"Glow");
	--HitMineCommand=cmd(playcommand,"Glow");
	HeldCommand=cmd(playcommand,"Glow");
	GlowCommand=cmd(setstate,0;finishtweening;diffusealpha,1;zoom,1.25;accelerate,0.25;diffusealpha,0;zoom,1.5);
};

return t;