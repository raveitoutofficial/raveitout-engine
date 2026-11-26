"""4:3 layout fixes applied to the generated SD theme.

Each patch is (path inside the theme, old text, new text). `old` must occur
exactly once in the HD file, otherwise generation fails, so a change to the HD
theme can't silently break a fix. Write "\n" line endings here; they are
converted to the file's own (CRLF) endings.
"""

PATCHES = [
    # Music wheel: at 640 wide the side items (204px covers) sat half off
    # screen and their titles were cut. Pull them in and shrink them so they fill
    # the 128px beside the 384px preview frame, with a smooth zoom from centre to side.
    ("Scripts/wheel.lua",
     """function RioWheel(self,offsetFromCenter,itemIndex,numItems)
    local spacing = 210;
	local edgeSpacing = 135;
    if math.abs(offsetFromCenter) < .5 then
        if not MUSICWHEEL_SONG_NAMES then
			self:zoom(1+math.cos(offsetFromCenter*math.pi)/3);
		end;
        self:x(offsetFromCenter*(spacing+edgeSpacing*2));
    else
        if offsetFromCenter >= .5 then
            self:x(offsetFromCenter*spacing+edgeSpacing);
        elseif offsetFromCenter <= -.5 then
            self:x(offsetFromCenter*spacing-edgeSpacing);
        end;
            --self:zoom(1);
    end;
end;""",
     """function RioWheel(self,offsetFromCenter,itemIndex,numItems)
    -- SD (4:3) version, see Utils/RaveItOut-SD/patches.py
    local spacing = 210;
	local edgeSpacing = 135;
	local o = offsetFromCenter;
	local x;
    if math.abs(o) < .5 then
        x = o*(spacing+edgeSpacing*2);
    elseif o >= .5 then
        x = o*spacing+edgeSpacing;
    else
        x = o*spacing-edgeSpacing;
    end;
	if SCREEN_WIDTH < 800 then
		-- side item centre at 263 from the middle, zoom 0.7 (143px wide):
		-- it fits between the preview frame edge (192) and the screen edge (320).
		self:x(x*0.762);
		local c = math.abs(o) < .5 and math.cos(o*math.pi) or 0;
		local top = MUSICWHEEL_SONG_NAMES and 1 or 1+1/3;
		self:zoom(0.7+(top-0.7)*c);
	else
		self:x(x);
		if math.abs(o) < .5 and not MUSICWHEEL_SONG_NAMES then
			self:zoom(1+math.cos(o*math.pi)/3);
		end;
	end;
end;"""),

    # "CURRENT SONGLIST" group title ran off the right edge on long names.
    ("BGAnimations/ScreenSelectMusic overlay/default.lua",
     "InitCommand=cmd(horizalign,left;x,SCREEN_LEFT+16;y,SCREEN_TOP+30;zoom,0.6;skewx,-0.25;uppercase,true);",
     "InitCommand=cmd(horizalign,left;x,SCREEN_LEFT+16;y,SCREEN_TOP+30;zoom,0.6;skewx,-0.25;uppercase,true;maxwidth,(SCREEN_WIDTH-40)/0.6);"),
]
