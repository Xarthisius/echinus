/* See LICENSE file for copyright and license details. */
#define BARPOS			StrutsOn /* BarTop, BarOff */
#define BORDERPX		"1"
#define NF_OPACITY "0.9"
#define FONT		"fixed-9"
Bool NOTITLES=0;
#define NORMBORDERCOLOR		"#cccccc"
#define NORMBUTTONCOLOR		"#cccccc"
#define NORMBGCOLOR		"#cccccc"
#define NORMFGCOLOR		"#000000"
#define SELBORDERCOLOR		"#0066ff"
#define SELBUTTONCOLOR		"#0066ff"
#define SELBGCOLOR		"#0066ff"
#define SELFGCOLOR		"#ffffff"
#define BLEFTPIXMAP "min.xbm"
#define BRIGHTPIXMAP "min.xbm"
#define BCENTERPIXMAP "min.xbm"
#define MINWIDTH 12
#define MINHEIGHT 12
#define BARHEIGHT "12"
#define TITLEHEIGHT "12"
#define TERMINAL "xterm"
#define MWFACT			0.6	/* master width factor [0.1 .. 0.9] */
#define SNAP			5	/* snap pixel */
Layout layouts[] = {
	/* symbol		function */
	{ "i",		ifloating }, /* first entry is default */
	{ "t",		tile }, 
	{ "m",		monocle }, 
	{ "b",		bstack }, 
	{ "f",		floating },
};
#define NMASTER 1 /* number of windows in master area */
#define MODKEY Mod1Mask /* key to press while moving */
#define DECORATETILED "0" /* set to 1 to draw titles in tiled layouts */
#define TITLEPOSITION "0"
#define TAGBAR "0"
