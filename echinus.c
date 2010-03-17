/* See LICENSE file for copyright and license details.
 *
 * echinus window manager is designed like any other X client as well. It is
 * driven through handling X events. In contrast to other X clients, a window
 * manager selects for SubstructureRedirectMask on the root window, to receive
 * events about window (dis-)appearance.  Only one X connection at a time is
 * allowed to select for this event mask.
 *
 * The event handlers of echinus are organized in an
 * array which is accessed whenever a new event has been fetched. This allows
 * event dispatching in O(1) time.
 *
 * Each child of the root window is called a client, except windows which have
 * set the override_redirect flag.  Clients are organized in a global
 * doubly-linked client list, the focus history is remembered through a global
 * stack list. Each client contains an array of Bools of the same size as the
 * global tags array to indicate the tags of a client.	
 *
 * Keys and tagging rules are organized as arrays and defined in config.h.
 *
 * To understand everything else, start reading main().
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <ctype.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <locale.h>
#include <stdarg.h>
#include <stdio.h>
#include <strings.h>
#include <unistd.h>
#include <regex.h>
#include <signal.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <X11/XF86keysym.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>
#include <X11/Xresource.h>
#include <X11/Xft/Xft.h>
#ifdef XRANDR
#include <X11/extensions/Xrandr.h>
#include <X11/extensions/randr.h>
#endif

/* macros */
#define BUTTONMASK		(ButtonPressMask | ButtonReleaseMask)
#define CLEANMASK(mask)		(mask & ~(numlockmask | LockMask))
#define MOUSEMASK		(BUTTONMASK | PointerMotionMask)
#define CLIENTMASK	        (PropertyChangeMask | EnterWindowMask)
#define FRAMEMASK               (MOUSEMASK | SubstructureRedirectMask | SubstructureNotifyMask | PointerMotionMask | EnterWindowMask | LeaveWindowMask)
#define LENGTH(x)		(sizeof x / sizeof x[0])
#define RESNAME			       "echinus"
#define RESCLASS	        "Echinus"
#define OPAQUE			0xffffffff
#ifdef DEBUG
#define DPRINT			fprintf(stderr, "%s: %s() %d\n",__FILE__,__func__, __LINE__);
#define DPRINTF(format, ...)	fprintf(stderr, "%s %s():%d " format, __FILE__, __func__, __LINE__, __VA_ARGS__)
#else
#define DPRINT			;
#define DPRINTF(format, ...)
#endif
#define ISLTFLOATING(m) (m && ((layouts[ltidxs[m->curtag]].arrange == floating) || (layouts[ltidxs[m->curtag]].arrange == ifloating)))

/* enums */
enum { LeftStrut, RightStrut, TopStrut, BotStrut, LastStrut };
enum { StrutsOn, StrutsOff, StrutsHide };			/* struts position */
enum { AlignLeft, AlignCenter, AlignRight };			/* title position */
enum { CurNormal, CurResize, CurMove, CurLast };	/* cursor */
enum { ColBorder, ColFG, ColBG, ColButton, ColLast };		/* color */
enum { Clk2Focus, SloppyFloat, AllSloppy, SloppyRaise }; /* focus model */
enum { ClientWindow, ClientTitle, ClientFrame }; /* client parts */

/* typedefs */
typedef struct Monitor Monitor;
struct Monitor {
	int sx, sy, sw, sh, wax, way, waw, wah;
	int curtag;
	unsigned long struts[LastStrut];
	Bool *seltags;
	Bool *prevtags;
	Monitor *next;
};

typedef struct Client Client;
struct Client {
	char name[256];
	int x, y, w, h;
	int th; /* title window */
	int rx, ry, rw, rh; /* revert geometry */
	int sfx, sfy, sfw, sfh; /* stored float geometry, used on mode revert */
	int basew, baseh, incw, inch, maxw, maxh, minw, minh;
	int minax, maxax, minay, maxay;
	long flags;
	int border, oldborder;
	Monitor *m;
	Bool isbanned, isfixed, ismax, isfloating, wasfloating, isicon;
	Bool isplaced, isbastard, isfocusable;
	Bool *tags;
	Client *next;
	Client *prev;
	Client *snext;
	Window win;
	Window title;
	Window frame;
	XftDraw *xftdraw;
};

typedef struct {
	Pixmap pm;
	int px, py;
	unsigned int pw, ph;
	int x;
	void (*action)(const char *arg);
} Button;

typedef struct { 
	int borderpx;
	float uf_opacity;
	int drawoutline;
	char *titlelayout;
	Button bleft;
	Button bcenter;
	Button bright;
} Look;

typedef struct {
	int x, y, w, h;
	unsigned long norm[ColLast];
	unsigned long sel[ColLast];
	XftColor *xftnorm;
	XftColor *xftsel;
	GC gc;
	struct {
	   XftFont *xftfont;
	   XGlyphInfo *extents;
	   int ascent;
	   int descent;
	   int height;
	   int width;
	} font;
} DC; /* draw context */

typedef struct {
	unsigned long mod;
	KeySym keysym;
	void (*func)(const char *arg);
	const char *arg;
} Key;

typedef struct {
	const char *symbol;
	void (*arrange)(Monitor *m);
} Layout;

typedef struct {
	const char *prop;
	const char *tags;
	Bool isfloating;
	Bool hastitle;
} Rule;

typedef struct {
	regex_t *propregex;
	regex_t *tagregex;
} Regs;

/* function declarations */
void applyrules(Client *c);
void arrange(Monitor *m);
void attach(Client *c);
void attachstack(Client *c);
void ban(Client *c);
void buttonpress(XEvent *e);
void bstack(Monitor *m);
void checkotherwm(void);
void cleanup(void);
void compileregs(void);
void configure(Client *c);
void configurenotify(XEvent *e);
void configurerequest(XEvent *e);
void destroynotify(XEvent *e);
void detach(Client *c);
void detachstack(Client *c);
void drawclient(Client *c);
void *emallocz(unsigned int size);
void enternotify(XEvent *e);
void eprint(const char *errstr, ...);
void expose(XEvent *e);
void floating(Monitor *m); /* default floating layout */
void ifloating(Monitor *m); /* intellectual floating layout try */
void iconifyit(const char *arg);
void incnmaster(const char *arg);
void focus(Client *c);
void focusnext(const char *arg);
void focusprev(const char *arg);
Client *getclient(Window w, Client *list, int part);
unsigned long getcolor(const char *colstr);
const char *getresource(const char *resource, const char *defval);
long getstate(Window w);
Bool gettextprop(Window w, Atom atom, char *text, unsigned int size);
void grabbuttons(Client *c, Bool focused);
void getpointer(int *x, int *y);
Monitor* getmonitor(int x, int y);
Monitor* curmonitor();
Monitor* clientmonitor(Client *c);
int idxoftag(const char *tag);
Bool isoccupied(unsigned int t);
Bool isprotodel(Client *c);
Bool isvisible(Client *c, Monitor *m);
void initmonitors(XEvent *e);
void keypress(XEvent *e);
void killclient(const char *arg);
void leavenotify(XEvent *e);
void manage(Window w, XWindowAttributes *wa);
void mappingnotify(XEvent *e);
void monocle(Monitor *m);
void maprequest(XEvent *e);
void movemouse(Client *c);
void moveresizekb(const char *arg);
Client *nexttiled(Client *c, Monitor *m);
Client *prevtiled(Client *c, Monitor *m);
void propertynotify(XEvent *e);
void reparentnotify(XEvent *e);
void quit(const char *arg);
void restart(const char *arg);
void resize(Client *c, Monitor *m, int x, int y, int w, int h, Bool sizehints);
void resizemouse(Client *c);
void restack(Monitor *m);
void run(void);
void scan(void);
void setclientstate(Client *c, long state);
void setlayout(const char *arg);
void setmwfact(const char *arg);
void setup(void);
void spawn(const char *arg);
void tag(const char *arg);
unsigned int textnw(const char *text, unsigned int len);
unsigned int textw(const char *text);
void tile(Monitor *m);
void togglestruts(const char *arg);
void togglefloating(const char *arg);
void togglemax(const char *arg);
void toggletag(const char *arg);
void toggleview(const char *arg);
void togglemonitor(const char *arg);
void focusview(const char *arg);
void saveconfig(Client *c);
void unban(Client *c);
void unmanage(Client *c);
void updategeom(Monitor *m);
void unmapnotify(XEvent *e);
void updatesizehints(Client *c);
void updatetitle(Client *c);
void view(const char *arg);
void viewprevtag(const char *arg);	/* views previous selected tags */
void viewlefttag(const char *arg);
void viewrighttag(const char *arg);
int xerror(Display *dpy, XErrorEvent *ee);
int xerrordummy(Display *dsply, XErrorEvent *ee);
int xerrorstart(Display *dsply, XErrorEvent *ee);
void zoom(const char *arg);

/* variables */
char **cargv;
Bool wasfloating = True;
int screen;
int (*xerrorxlib)(Display *, XErrorEvent *);
unsigned int numlockmask = 0;
Bool domwfact = True;
Bool dozoom = True;
Bool otherwm;
Bool running = True;
Bool selscreen = True;
Bool notitles = False;
Bool sloppy = False;
Client *clients = NULL;
Monitor *monitors = NULL;
Client *sel = NULL;
Client *stack = NULL;
#define curseltags curmonitor()->seltags
#define curprevtags curmonitor()->prevtags
#define cursx curmonitor()->sx
#define cursy curmonitor()->sy
#define cursh curmonitor()->sh
#define cursw curmonitor()->sw
#define curwax curmonitor()->wax
#define curway curmonitor()->way
#define curwaw curmonitor()->waw
#define curwah curmonitor()->wah
#define curmontag curmonitor()->curtag
#define curstruts curmonitor()->struts
int *nmasters;
int *bpos;
int *ltidxs;
double *mwfacts;
Cursor cursor[CurLast];
Display *dpy;
DC dc = {0};
Look look = {0};
Window root;
Regs *regs = NULL;
XrmDatabase xrdb = NULL;

char terminal[255];
char **tags;
Key **keys;
Rule **rules;
int ntags = 0;
int nkeys = 0;
int nrules = 0;
Bool hidebastards = 0;
Bool dectiled = 0;
unsigned int modkey = 0;
/* configuration, allows nested code to access above variables */
#include "config.h"
#include "ewmh.c"
#include "parse.c"
#include "draw.c"

void (*handler[LASTEvent]) (XEvent *) = {
	[ButtonPress] = buttonpress,
	[ConfigureRequest] = configurerequest,
	[ConfigureNotify] = configurenotify,
	[DestroyNotify] = destroynotify,
	[EnterNotify] = enternotify,
	[LeaveNotify] = leavenotify,
	[Expose] = expose,
	[KeyPress] = keypress,
	[MappingNotify] = mappingnotify,
	[MapRequest] = maprequest,
	[PropertyNotify] = propertynotify,
	[ReparentNotify] = reparentnotify,
	[UnmapNotify] = unmapnotify,
	[ClientMessage] = clientmessage,
#ifdef XRANDR
	[RRScreenChangeNotify] = initmonitors,
#endif
};

/* function implementations */
void
applyrules(Client *c) {
    static char buf[512];
    int i, j;
    regmatch_t tmp;
    Bool matched = False;
    XClassHint ch = { 0 };

    /* rule matching */
    XGetClassHint(dpy, c->win, &ch);
    snprintf(buf, sizeof buf, "%s:%s:%s",
		    ch.res_class ? ch.res_class : "",
		    ch.res_name ? ch.res_name : "", c->name);
    for(i = 0; i < nrules; i++)
	    if(regs[i].propregex && !regexec(regs[i].propregex, buf, 1, &tmp, 0)) {
		    c->isfloating = rules[i]->isfloating;
		    c->title = rules[i]->hastitle;
		    for(j = 0; regs[i].tagregex && j < ntags; j++) {
			    if(!regexec(regs[i].tagregex, tags[j], 1, &tmp, 0)) {
				    matched = True;
				    c->tags[j] = True;
			    }
		    }
	    }
    if(ch.res_class)
	    XFree(ch.res_class);
    if(ch.res_name)
	    XFree(ch.res_name);
    if(!matched) {
	    memcpy(c->tags, curseltags, ntags*(sizeof curseltags));
    }
}

void
arrangemon(Monitor *m) {
    Client *c;

    layouts[ltidxs[m->curtag]].arrange(m);
    restack(m);
    for(c = stack; c; c = c->snext) {
	    if ((c->m == m) &&
		((!c->isbastard && isvisible(c, m) && !c->isicon) ||
		(c->isbastard && bpos[m->curtag] == StrutsOn))) {
		    unban(c);
		    if(c->isbastard)
			c->isicon = False;
	    } 
    }
 
    for(c = stack; c; c = c->snext) {
	    if((c->m == m) &&
	       ((!c->isbastard && (!isvisible(c, m) || c->isicon)) ||
	       (c->isbastard && bpos[m->curtag] == StrutsHide))) {
		    ban(c);
		    if(c->isbastard) 
			c->isicon = True;
	    }
    }
}

void
arrange(Monitor *m) {
    Monitor *i;

    if(!m) {
	for(i = monitors; i; i = i->next)
	    arrangemon(i);
    } else
	arrangemon(m);
}

void
attach(Client *c) {
    if(clients)
	    clients->prev = c;
    c->next = clients;
    clients = c;
}

void
attachstack(Client *c) {
    Client *t;
    if(stack && checkatom(c->win, atom[WindowType], atom[WindowTypeDesk])){
	for(t = stack; t->snext; t = t->snext);
	t->snext = c;
	c->snext = NULL;
	return;
    }
    c->snext = stack;
    stack = c;
}

void
ban(Client *c) {
    if(c->isbanned)
	    return;
    setclientstate(c, IconicState);
    XSelectInput(dpy, c->win,
		     CLIENTMASK&~(StructureNotifyMask|EnterWindowMask));
    XSelectInput(dpy, c->frame, NoEventMask);
    XUnmapWindow(dpy, c->frame);
    XSelectInput(dpy, c->win, CLIENTMASK);
    XSelectInput(dpy, c->frame, FRAMEMASK);
    c->isbanned = True;
}

void
iconifyit(const char *arg) {
    Client *c;
    if(!sel)
	return;
    c = sel;
    focusnext(NULL);
    ban(c);
    c->isicon = True;
    arrange(curmonitor());
}

void
buttonpress(XEvent *e) {
    Client *c;
    XButtonPressedEvent *ev = &e->xbutton;

    if(ev->window == root) {
	    switch(ev->button) {
		case Button3:
		    spawn(terminal);
		    break;
		case Button4:
		    viewlefttag(NULL);
		    break;
		case Button5:
		    viewrighttag(NULL);
		    break;
	    }
	    return;
    }
    if((c = getclient(ev->window, clients, ClientTitle))) {
	DPRINTF("TITLE %s: 0x%x\n", c->name, ev->window);
	focus(c);
	if((ev->x > look.bleft.x) && (ev->x < look.bleft.x + dc.h) && look.bleft.x != -1) {
		look.bleft.action(NULL);
		return;
	} else if((ev->x > look.bcenter.x) && (ev->x < look.bcenter.x + dc.h) && look.bcenter.x != -1) {
		look.bcenter.action(NULL);
		return;
	} else if((ev->x > look.bright.x) && (ev->x < look.bright.x + dc.h) && look.bright.x != -1) {
		look.bright.action(NULL);
		return;
	}
	if(ev->button == Button1) {
	    if(ISLTFLOATING(curmonitor()) || c->isfloating)
		restack(curmonitor());
	    movemouse(c);
	    arrange(NULL);
	} else if(ev->button == Button3 && !c->isfixed) {
	    resizemouse(c);
	}
    } else if((c = getclient(ev->window, clients, ClientFrame))) {
	DPRINTF("FRAME %s: 0x%x\n", c->name, ev->window);
	focus(c);
	restack(curmonitor());
	if(CLEANMASK(ev->state) != modkey) {
	   XAllowEvents(dpy, ReplayPointer, CurrentTime);
	   return;
	}
	if(ev->button == Button1) {
		if(!ISLTFLOATING(curmonitor()) && !c->isfloating)
		    togglefloating(NULL);
		movemouse(c);
		arrange(NULL);
	}
	else if(ev->button == Button2) {
		if(!ISLTFLOATING(curmonitor()) && c->isfloating)
			togglefloating(NULL);
		else
			zoom(NULL);
	}
	else if(ev->button == Button3 && !c->isfixed) {
		if(!ISLTFLOATING(curmonitor()) && !c->isfloating)
			togglefloating(NULL);
		resizemouse(c);
	}
    }
}

void
checkotherwm(void) {
    otherwm = False;
    XSetErrorHandler(xerrorstart);

    /* this causes an error if some other window manager is running */
    XSelectInput(dpy, root, SubstructureRedirectMask);
    XSync(dpy, False);
    if(otherwm)
	    eprint("echinus: another window manager is already running\n");
    XSync(dpy, False);
    XSetErrorHandler(NULL);
    xerrorxlib = XSetErrorHandler(xerror);
    XSync(dpy, False);
}

void
cleanup(void) {
    while(stack) {
	    unban(stack);
	    unmanage(stack);
    }
    free(tags);
    free(keys);
    /* free resource database */
    XrmDestroyDatabase(xrdb);
    /* free colors */
    XftColorFree(dpy, DefaultVisual(dpy, screen), DefaultColormap(dpy, screen), dc.xftnorm);
    XftColorFree(dpy, DefaultVisual(dpy, screen), DefaultColormap(dpy, screen), dc.xftsel);
    XUngrabKey(dpy, AnyKey, AnyModifier, root);
    XFreeGC(dpy, dc.gc);
    XFreeCursor(dpy, cursor[CurNormal]);
    XFreeCursor(dpy, cursor[CurResize]);
    XFreeCursor(dpy, cursor[CurMove]);
    XSetInputFocus(dpy, PointerRoot, RevertToPointerRoot, CurrentTime);
    XSync(dpy, False);
}

void
compileregs(void) {
    int i;
    regex_t *reg;

    if(regs)
	    return;
    regs = emallocz(nrules * sizeof(Regs));
    for(i = 0; i < nrules; i++) {
	    if(rules[i]->prop) {
		    reg = emallocz(sizeof(regex_t));
		    if(regcomp(reg, rules[i]->prop, REG_EXTENDED))
			    free(reg);
		    else
			    regs[i].propregex = reg;
	    }
	    if(rules[i]->tags) {
		    reg = emallocz(sizeof(regex_t));
		    if(regcomp(reg, rules[i]->tags, REG_EXTENDED))
			    free(reg);
		    else
			    regs[i].tagregex = reg;
	    }
    }
}

void
configure(Client *c) {
    XConfigureEvent ce;

    ce.type = ConfigureNotify;
    ce.display = dpy;
    ce.event = c->win;
    ce.window = c->win;
    ce.x = c->x;
    ce.y = c->y;
    ce.width = c->w;
    ce.height = c->h - c->th;
    ce.border_width = 0;
    ce.above = None;
    ce.override_redirect = False;
    XSendEvent(dpy, c->win, False, StructureNotifyMask, (XEvent *)&ce);
}

void
initmonitors(XEvent *e) {
	Monitor *m;
#ifdef XRANDR
	Monitor *t;
	XRRCrtcInfo		*ci;
	XRRScreenResources	*sr;
	int			c, n;
	int			ncrtc = 0;
	int                     dummy1, dummy2, major, minor;

	/* free */
	if(monitors) {
	    m = monitors;
	    do {
		t = m->next;
		free(m);
		m = t;
	    } while(m);
	    monitors = NULL;
	}
	/* initial Xrandr setup */
	if(XRRQueryExtension(dpy, &dummy1, &dummy2))
	     if (XRRQueryVersion(dpy, &major, &minor) && major < 1)
		 goto no_xrandr;

	/* map virtual screens onto physical screens */
	sr = XRRGetScreenResources(dpy, root);
	if (sr == NULL)
		goto no_xrandr;
	else 
		ncrtc = sr->ncrtc;

	for (c = 0 , n = 0, ci = NULL; c < ncrtc; c++) {
		ci = XRRGetCrtcInfo(dpy, sr, sr->crtcs[c]);
		if (ci->noutput == 0)
			continue;

		if (ci != NULL && ci->mode == None)
		    fprintf(stderr, "???\n");
		else {
		    /* If monitor is a mirror, we don't care about it */
		    if(n && ci->x == monitors->sx && ci->y == monitors->sy)
			continue;
		    m = emallocz(sizeof(Monitor));
		    m->sx = m->wax = ci->x;
		    m->sy = m->way = ci->y;
		    m->sw = m->waw = ci->width;
		    m->sh = m->wah = ci->height;
		    m->curtag = n;
		    m->prevtags = emallocz(ntags*sizeof(Bool));
		    m->seltags = emallocz(ntags*sizeof(Bool));
		    m->seltags[n] = True;
		    m->next = monitors;
		    monitors = m;
		    n++;
		}
	}
	if (ci)
		XRRFreeCrtcInfo(ci);
	XRRFreeScreenResources(sr);
	return;
no_xrandr:
#endif
	m = emallocz(sizeof(Monitor));
	m->sx = m->wax = 0;
	m->sy = m->way = 0;
	m->sw = m->waw = DisplayWidth(dpy, screen);
	m->sh = m->wah = DisplayHeight(dpy, screen);
	m->curtag = 0;
	m->prevtags = emallocz(ntags*sizeof(Bool));
	m->seltags = emallocz(ntags*sizeof(Bool));
	m->seltags[0] = True;
	m->next = NULL;
	monitors = m;
}

void
configurenotify(XEvent *e) {
    XConfigureEvent *ev = &e->xconfigure;
    Monitor *m;
    if(ev->window == root) {
#ifdef XRANDR
	    if(XRRUpdateConfiguration((XEvent*)ev)) {
#endif
		initmonitors(e);
		for(m = monitors; m; m = m->next)
		    updategeom(m);
		arrange(NULL);
#ifdef XRANDR
	    }
#endif
    }
}

void
configurerequest(XEvent *e) {
    Client *c;
    XConfigureRequestEvent *ev = &e->xconfigurerequest;
    XWindowChanges wc;

    if((c = getclient(ev->window, clients, ClientWindow))) {
	    c->ismax = False;
	    if(ev->value_mask & CWBorderWidth)
		    c->border = ev->border_width;
	    if(c->isfixed || c->isfloating || ISLTFLOATING(c->m)) {
		    if(ev->value_mask & CWX)
			    c->x = ev->x;
		    if(ev->value_mask & CWY)
			    c->y = ev->y;
		    if(ev->value_mask & CWWidth)
			    c->w = ev->width;
		    if(ev->value_mask & CWHeight)
			    c->h = ev->height + c->th;
		    if((c->x + c->w) > (curwax + cursw) && c->isfloating)
			    c->x = cursw / 2 - c->w / 2; /* center in x direction */
		    if((c->y + c->h) > (curway + cursh) && c->isfloating)
			    c->y = cursh / 2 - c->h / 2; /* center in y direction */
		    if((ev->value_mask & (CWX | CWY))
		    && !(ev->value_mask & (CWWidth | CWHeight)))
			    configure(c);
		    if(isvisible(c, NULL)) {
			    XMoveResizeWindow(dpy, c->frame, c->m->sx + c->x, c->m->sy + c->y, c->w, c->h);
			    if(c->title)
				XMoveResizeWindow(dpy, c->title, 0, 0, c->w, c->th);
			    XMoveResizeWindow(dpy, c->win, 0, c->th, ev->width, ev->height);
			    drawclient(c);
		    }
	    } else {
		    configure(c);
	    }
    } else {
	    wc.x = ev->x;
	    wc.y = ev->y;
	    wc.width = ev->width;
	    wc.height = ev->height;
	    wc.border_width = ev->border_width;
	    wc.sibling = ev->above;
	    wc.stack_mode = ev->detail;
	    XConfigureWindow(dpy, ev->window, ev->value_mask, &wc);
    }
    XSync(dpy, False);
}

void
destroynotify(XEvent *e) {
    Client *c;
    Monitor *m = NULL;
    XDestroyWindowEvent *ev = &e->xdestroywindow;

    if((c = getclient(ev->window, clients, ClientWindow)))
	m = clientmonitor(c);
    else
	goto end;
    if(!c->isbastard) {
	unmanage(c);
	goto end;
    }
    unmanage(c);
    m->struts[RightStrut] = m->struts[LeftStrut] = m->struts[TopStrut] = m->struts[BotStrut] = 0;
    for(c = clients; c ; c = c->next){
	if(c->isbastard)
		updatestruts(c->win);
    }
    updategeom(m);
end:
    arrange(m);
    updateatom[ClientList](NULL);
}

void
detach(Client *c) {
    if(c->prev)
	    c->prev->next = c->next;
    if(c->next)
	    c->next->prev = c->prev;
    if(c == clients)
	    clients = c->next;
    c->next = c->prev = NULL;
}

void
detachstack(Client *c) {
    Client **tc;

    for(tc=&stack; *tc && *tc != c; tc=&(*tc)->snext);
    *tc = c->snext;
}

void *
emallocz(unsigned int size) {
    void *res = calloc(1, size);

    if(!res)
	    eprint("fatal: could not malloc() %u bytes\n", size);
    return res;
}

void
enternotify(XEvent *e) {
    XCrossingEvent *ev = &e->xcrossing;
    Client *c;

    if(ev->mode != NotifyNormal || ev->detail == NotifyInferior)
	return;
    if((c = getclient(ev->window, clients, ClientFrame))){
	if(!isvisible(sel, curmonitor()))
	    focus(c);
	if(c->isbastard) {
	    grabbuttons(c, True);
	    return;
	}
	switch(sloppy) {
	case Clk2Focus:
	    XGrabButton(dpy, AnyButton, AnyModifier, c->frame, False,
		    BUTTONMASK, GrabModeSync, GrabModeSync, None, None);
	    break;
	case SloppyFloat:
	    if(ISLTFLOATING(curmonitor()) || c->isfloating)
		focus(c);
		XGrabButton(dpy, AnyButton, AnyModifier, c->frame, False,
		    BUTTONMASK, GrabModeSync, GrabModeSync, None, None);
	    break;
	case AllSloppy:
	    focus(c);
	    break;
	case SloppyRaise:
	    focus(c);
	    restack(curmonitor());
	    break;
	}
    } else if(ev->window == root) {
	selscreen = True;
	focus(NULL);
    }
}

void
eprint(const char *errstr, ...) {
    va_list ap;

    va_start(ap, errstr);
    vfprintf(stderr, errstr, ap);
    va_end(ap);
    exit(EXIT_FAILURE);
}

void
expose(XEvent *e) {
    XExposeEvent *ev = &e->xexpose;
    XEvent tmp;
    Client *c;
    while(XCheckWindowEvent(dpy, ev->window, ExposureMask, &tmp));

    if((c = getclient(ev->window, clients, ClientWindow))
    || (c = getclient(ev->window, clients, ClientTitle))) {
	drawclient(c);
    }
}

void
floating(Monitor *m) { /* default floating layout */
    Client *c;
    notitles = False;

    domwfact = dozoom = False;
    for(c = clients; c; c = c->next){
	if(isvisible(c, m) && !c->isicon) {
		if(!c->isfloating)
		    /* restore last known float dimensions */
		    resize(c, m, c->sfx, c->sfy, c->sfw, c->sfh, True);
		else
		    resize(c, m, c->x, c->y, c->w, c->h, True);
	}
    }
    wasfloating = True;
}

void
focus(Client *c) {
    Client *o;

    o = sel;
    if((!c && selscreen) || (c && (c->isbastard || !isvisible(c, curmonitor()))))
	    for(c = stack; c && (c->isbastard || !isvisible(c, curmonitor())); c = c->snext);
    if(sel && sel != c) {
	    grabbuttons(sel, False);
	    XSetWindowBorder(dpy, sel->frame, dc.norm[ColBorder]);
    }
    if(c) {
	    c->isicon = False;
	    detachstack(c);
	    attachstack(c);
	    grabbuttons(c, True);
	    unban(c);
    }
    sel = c;
    if(!selscreen)
	    return;
    if(c) {
	    setclientstate(c, NormalState);
	    if(c->isfocusable)
		XSetInputFocus(dpy, c->win, RevertToPointerRoot, CurrentTime);
	    XSetWindowBorder(dpy, sel->frame, dc.sel[ColBorder]);
	    drawclient(c);
    } else {
	    XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
    }
    if(o) 
	drawclient(o);
    updateatom[ActiveWindow](sel);
    updateatom[ClientList](NULL);
    updateatom[CurDesk](NULL);
}

void
focusnext(const char *arg) {
    Client *c;

    if(!sel)
	    return;
    for(c = sel->next; c && (c->isbastard || c->isicon || !isvisible(c, curmonitor())); c = c->next);
    if(!c)
	    for(c = clients; c && (c->isbastard || c->isicon || !isvisible(c, curmonitor())); c = c->next);
    if(c) {
	    focus(c);
	    restack(curmonitor());
    }
}

void
focusprev(const char *arg) {
    Client *c;

    if(!sel)
	    return;
    for(c = sel->prev; c && (c->isbastard || c->isicon || !isvisible(c, curmonitor())); c = c->prev);
    if(!c) {
	    for(c = clients; c && c->next; c = c->next);
	    for(; c && (c->isbastard || c->isicon || !isvisible(c, curmonitor())); c = c->prev);
    }
    if(c) {
	    focus(c);
	    restack(curmonitor());
    }
}

void
incnmaster(const char *arg) {
    int i;

    if(layouts[ltidxs[curmontag]].arrange != tile)
	    return;
    if(!arg) {
	    nmasters[curmontag] = NMASTER;
    } else {
	    i = atoi(arg);
	    if((nmasters[curmontag] + i) < 1 || curwah / (nmasters[curmontag] + i) <= 2 * look.borderpx)
		    return;
	    nmasters[curmontag] += i;
    }
    if(sel)
	    arrange(curmonitor());
}

Client *
getclient(Window w, Client *list, int part) {
    Client *c;

#define ClientPart(_c, _part) ((_part == ClientWindow) ? _c->win : ((_part == ClientTitle) ? _c->title : _c->frame))

    for(c = list; c && (ClientPart(c, part)) != w; c = c->next);

    return c;
}

unsigned long
getcolor(const char *colstr) {
    Colormap cmap = DefaultColormap(dpy, screen);
    XColor color;

    if(!XAllocNamedColor(dpy, cmap, colstr, &color, &color))
	    eprint("error, cannot allocate color '%s'\n", colstr);
    return color.pixel;
}

long
getstate(Window w) {
    int format, status;
    long result = -1;
    unsigned char *p = NULL;
    unsigned long n, extra;
    Atom real;

    status = XGetWindowProperty(dpy, w, atom[WMState], 0L, 2L, False, atom[WMState],
		    &real, &format, &n, &extra, (unsigned char **)&p);
    if(status != Success)
	    return -1;
    if(n != 0)
	    result = *p;
    XFree(p);
    return result;
}

const char *
getresource(const char *resource, const char *defval) {
   static char name[256], class[256], *type;
   XrmValue value;
   snprintf(name, sizeof(name), "%s.%s", RESNAME, resource);
   snprintf(class, sizeof(class), "%s.%s", RESCLASS, resource);
   XrmGetResource(xrdb, name, class, &type, &value);
   if(value.addr)
	   return value.addr;
   return defval;
}

Bool
gettextprop(Window w, Atom atom, char *text, unsigned int size) {
    char **list = NULL;
    int n;
    XTextProperty name;

    if(!text || size == 0)
	    return False;
    text[0] = '\0';
    XGetTextProperty(dpy, w, &name, atom);
    if(!name.nitems)
	    return False;
    if(name.encoding == XA_STRING) {
	    strncpy(text, (char *)name.value, size - 1);
    } else {
	    if(XmbTextPropertyToTextList(dpy, &name, &list, &n) >= Success
	    && n > 0 && *list) {
		    strncpy(text, *list, size - 1);
		    XFreeStringList(list);
	    }
    }
    text[size - 1] = '\0';
    XFree(name.value);
    return True;
}

void
grabbuttons(Client *c, Bool focused) {
    unsigned int Buttons[] = {Button1, Button2, Button3};	     
    unsigned int Modifiers[] = {modkey, modkey|LockMask,
	       modkey|numlockmask, modkey|numlockmask|LockMask};											      
    int i, j;	
    XUngrabButton(dpy, AnyButton, AnyModifier, c->frame);

    if(focused) {
	   for (i = 0; i < LENGTH(Buttons); i++)													  
		   for (j = 0; j < LENGTH(Modifiers); j++)												  
			   XGrabButton(dpy, Buttons[i], Modifiers[j], c->frame, False,
				   BUTTONMASK, GrabModeAsync, GrabModeSync, None, None);    
    } else {
	    XGrabButton(dpy, AnyButton, AnyModifier, c->frame, False, BUTTONMASK,
			    GrabModeAsync, GrabModeSync, None, None);
    }
}

int
idxoftag(const char *tag) {
    int i;

    for(i = 0; (i < ntags) && strcmp(tag, tags[i]); i++);
    return (i < ntags) ? i : 0;
}

Bool
isprotodel(Client *c) {
    int i, n;
    Atom *protocols;
    Bool ret = False;

    if(XGetWMProtocols(dpy, c->win, &protocols, &n)) {
	    for(i = 0; !ret && i < n; i++)
		    if(protocols[i] == atom[WMDelete])
			    ret = True;
	    XFree(protocols);
    }
    return ret;
}

Bool
isvisible(Client *c, Monitor *m) {
    int i;
    if(!c)
	return False;
    if(!m) {
	for(m = monitors; m; m = m->next) {
	    for(i = 0; i < ntags; i++)
		if(c->tags[i] && m->seltags[i])
			return True;
	}
    } else {
	for(i = 0; i < ntags; i++)
	    if(c->tags[i] && m->seltags[i])
		    return True;
	}
    return False;
}

void
keypress(XEvent *e) {
    int i;
    KeyCode code;
    KeySym keysym;
    XKeyEvent *ev;

    if(!e) { /* grabkeys */
	    XUngrabKey(dpy, AnyKey, AnyModifier, root);
	    for(i = 0; i < nkeys; i++) {
		if(code = XKeysymToKeycode(dpy, keys[i]->keysym)) {
		    XGrabKey(dpy, code, keys[i]->mod, root, True,
				    GrabModeAsync, GrabModeAsync);
		    XGrabKey(dpy, code, keys[i]->mod | LockMask, root, True,
				    GrabModeAsync, GrabModeAsync);
		    XGrabKey(dpy, code, keys[i]->mod | numlockmask, root, True,
				    GrabModeAsync, GrabModeAsync);
		    XGrabKey(dpy, code, keys[i]->mod | numlockmask | LockMask, root, True,
				    GrabModeAsync, GrabModeAsync);
		}
	    }
	    return;
    }
    ev = &e->xkey;
    keysym = XKeycodeToKeysym(dpy, (KeyCode)ev->keycode, 0);
    for(i = 0; i < nkeys; i++)
	    if(keysym == keys[i]->keysym
	    && CLEANMASK(keys[i]->mod) == CLEANMASK(ev->state))
	    {
		    if(keys[i]->func)
			    keys[i]->func(keys[i]->arg);
	    }
}

void
killclient(const char *arg) {
    XEvent ev;
    if(!sel)
	    return;
    if(isprotodel(sel)) {
	    ev.type = ClientMessage;
	    ev.xclient.window = sel->win;
	    ev.xclient.message_type = atom[WMProto];
	    ev.xclient.format = 32;
	    ev.xclient.data.l[0] = atom[WMDelete];
	    ev.xclient.data.l[1] = CurrentTime;
	    XSendEvent(dpy, sel->win, False, NoEventMask, &ev);
    } else {
	    XKillClient(dpy, sel->win);
    }
}

void
leavenotify(XEvent *e) {
    XCrossingEvent *ev = &e->xcrossing;
    Client *c;

    if((ev->window == root) && !ev->same_screen) {
	    selscreen = False;
	    focus(NULL);
    }
    if((c = getclient(ev->window, clients, ClientFrame)))
	XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
}

void
manage(Window w, XWindowAttributes *wa) {
    Client *c, *t = NULL;
    Monitor *cm;
    Window trans;
    Status rettrans;
    XWindowChanges wc;
    XSetWindowAttributes twa;
    XWMHints *wmh;
    unsigned long mask = 0;

    cm = curmonitor();
    c = emallocz(sizeof(Client));
    c->win = w;
    c->m = NULL;
    if(checkatom(c->win, atom[WindowType], atom[WindowTypeDesk]) ||
	    checkatom(c->win, atom[WindowType], atom[WindowTypeDock])){
	c->isbastard = True;
	c->isfloating = True;
	c->isfixed = True;
    }
    if(checkatom(c->win, atom[WindowType], atom[WindowTypeDialog])) {
	c->isfloating = True;
	c->isfixed = True;
    }

    c->isicon = False;
    c->title = c->isbastard ? (Window)NULL : 1;
    c->tags = emallocz(ntags*(sizeof curseltags));
    c->isfocusable = c->isbastard ? False : True;
    c->border = c->isbastard ? 0 : look.borderpx;
    mwm_process_atom(c);
    updatesizehints(c);

    if((rettrans = XGetTransientForHint(dpy, w, &trans) == Success))
	    for(t = clients; t && t->win != trans; t = t->next);
    if(t)
	    memcpy(c->tags, t->tags, ntags*(sizeof curseltags));

    updatetitle(c);
    applyrules(c);

    c->th = c->title ? dc.h : 0;

    if(!c->isfloating)
	c->isfloating = (rettrans == Success) || c->isfixed;

    if((wmh = XGetWMHints(dpy, c->win))) {
	c->isfocusable = !(wmh->flags & InputHint) || wmh->input;
	XFree(wmh);
    } 

    c->x = c->sfx = wa->x%cm->sw;
    c->y = c->sfy = wa->y%cm->sh;
    c->w = c->sfw = wa->width;
    c->h = c->sfh = wa->height + c->th;

    if(wa->x && wa->y) {
	c->isplaced = True;
    } else if(!c->isbastard && c->isfloating) {
	    getpointer(&c->x, &c->y);
	    c->x -= cm->sx;
	    c->y -= cm->sy;
    }
    if(c->isbastard) {
	c->x = wa->x;
	c->y = wa->y;
    }
    c->oldborder = c->isbastard ? 0 : wa->border_width;
    if(c->w == cursw && c->h == cursh) {
	c->x = cursx;
	c->y = cursy;
    } else {
	if(c->x + c->w > curwax + curwaw)
		c->x = curwax + curwaw - c->w;
	if(c->y + c->h > curway + curwah)
		c->y = curway + curwah - c->h;
	if(c->x < curwax)
		c->x = curwax;
	if(c->y < curway)
		c->y = curway;
    }

    wc.border_width = c->border;
    grabbuttons(c, False);
    twa.override_redirect = True;
    twa.event_mask = FRAMEMASK;
    mask = CWOverrideRedirect | CWEventMask | CWBackPixel;
    if(wa->depth == 32) {
	mask |= CWColormap | CWBorderPixel;
	twa.colormap = XCreateColormap(dpy, root, wa->visual, AllocNone);
	twa.background_pixel = BlackPixel(dpy, screen);
	twa.border_pixel = BlackPixel(dpy, screen);
    } else {
	twa.background_pixel = dc.norm[ColBG];
    }
    c->frame = XCreateWindow(dpy, root, c->x, c->y, c->w, c->h,
		    c->border, wa->depth == 32 ? 32 : DefaultDepth(dpy, screen), InputOutput,
		    wa->depth == 32 ? wa->visual : DefaultVisual(dpy, screen),
		    mask, &twa);
 
    XConfigureWindow(dpy, c->frame, CWBorderWidth, &wc);
    XSetWindowBorder(dpy, c->frame, dc.norm[ColBorder]);

    twa.event_mask = ExposureMask | MOUSEMASK;
    /* we create title as root's child as a workaround for 32bit visuals */
    if(c->title) {
       c->title = XCreateWindow(dpy, root, 0, 0, c->w, c->th,
			0, DefaultDepth(dpy, screen), CopyFromParent,
			DefaultVisual(dpy, screen),
			CWEventMask, &twa);
       c->xftdraw = XftDrawCreate(dpy, c->title, DefaultVisual(dpy, screen), DefaultColormap(dpy, screen));
    } else {
	c->title = (Window) NULL;
    }

    cm = c->isbastard ? getmonitor(c->x, c->y) : clientmonitor(c);
    if(c->isbastard)
	c->tags = cm->seltags;
    attach(c);
    attachstack(c);
#if 0
    twa.event_mask = EnterWindowMask |
			PropertyChangeMask | FocusChangeMask;
    twa.win_gravity = StaticGravity;
    twa.do_not_propagate_mask = MOUSEMASK;
    XChangeWindowAttributes(dpy, c->win,
			CWEventMask | CWWinGravity | CWDontPropagate, &twa);
#endif
    XReparentWindow(dpy, c->win, c->frame, 0, c->th);
    XReparentWindow(dpy, c->title, c->frame, 0, 0);
    XAddToSaveSet(dpy, c->win);
    XMapWindow(dpy, c->win);
    XMapRaised(dpy, c->title);
    if(!c->isbastard){
	wc.border_width = 0;
	XConfigureWindow(dpy, c->win, CWBorderWidth, &wc);
	XSetWindowBorder(dpy, c->win, dc.norm[ColBorder]);
	configure(c); /* propagates border_width, if size doesn't change */
    }
    if(checkatom(c->win, atom[WindowState], atom[WindowStateFs]))
	ewmh_process_state_atom(c, atom[WindowStateFs], 1);
    if(c->isbastard)
	XSelectInput(dpy, w, PropertyChangeMask);
    else
	XSelectInput(dpy, w, CLIENTMASK);
    resize(c, cm, c->x, c->y, c->w, c->h, True);
    ban(c);
    updateatom[ClientList](NULL);
    updateatom[WindowDesk](c);
    updatestruts(c->win);
    arrange(clientmonitor(c));
    focus(NULL);
}

void
mappingnotify(XEvent *e) {
    XMappingEvent *ev = &e->xmapping;

    XRefreshKeyboardMapping(ev);
    if(ev->request == MappingKeyboard)
	    keypress(NULL);
}

void
maprequest(XEvent *e) {
    static XWindowAttributes wa;
    Client *c;
    XMapRequestEvent *ev = &e->xmaprequest;

    if(!XGetWindowAttributes(dpy, ev->window, &wa))
	    return;
    if(wa.override_redirect)
	    return;
    if((c = getclient(ev->window, clients, ClientWindow)))
	    unban(c);
    else
	    manage(ev->window, &wa);
    arrange(curmonitor());
}

int
smartcheckarea(Monitor *m, int x, int y, int w, int h){
    Client *c;
    int n = 0;
    for(c = clients; c; c = c->next){ 
	if(isvisible(c, m) && !c->isicon && c->isplaced){
	    /* A Kind Of Magic */
	    if((c->y + c->h >= y && c->y + c->h <= y + h
	    && c->x+c->w >= x && c->x+c->w <= x+w)
	    || (c->x >= x && c->x <= x+w
		 && c->y + c->h >= y && c->y + c->h <= y + h)
	    || (c->x >= x && c->x <= x+w
		 && c->y >= y && c->y <= y+h)
	    || (c->x+c->w >= x && c->x+c->w <= x+w
		 && c->y >= y && c->y <= y+h))
		n++;
	}
    }
    return n;
}

void
ifloating(Monitor *m){
    Client *c;
    int x, y, f;
    for(c = clients; c; c = c->next){ 
	if(isvisible(c, m) && !c->isicon && !c->isbastard) {
		for(f = 0; !c->isplaced; f++){ 
		    if((c->w > m->sw/2 && c->h > m->sw/2) || c->h < 4){
			/* too big to deal with */
			c->isplaced = True; 
		    }
		    for(y = m->way; y+c->h <= m->way + m->wah && !c->isplaced ; y+=c->h/4) {
			for(x = m->wax; x+c->w <= m->wax + m->waw && !c->isplaced; x+=c->w/8) {
			    if(smartcheckarea(m, x, y, 0.8*c->w, 0.8*c->h)<=f){
				resize(c, m, x+c->th*(rand()%3), y+c->th+c->th*(rand()%3), c->w, c->h, True);
				c->isplaced = True;
			    }
			}
		    }
		}
	}
    }
}

void
monocle(Monitor *m) {
    Client *c;
    wasfloating = False;
    for(c = clients; c; c = c->next){
	if(isvisible(c, m) && !c->isicon && !c->isbastard) {
	    c->isplaced = False;
	    if(c->isfloating) {
		resize(c, m, c->x, c->y, c->w, c->h, False);
	    } else {
		if(bpos[m->curtag] != StrutsOn)
		    resize(c, m, m->wax-c->border, m->way-c->border, m->waw, m->wah, False);
		else
		    resize(c, m, m->wax, m->way, m->waw - 2*c->border, m->wah - 2*c->border, False);
	    }
	}
    }
}

void
moveresizekb(const char *arg) {
    int dw, dh, dx, dy;
    dw = dh = dx = dy = 0;
    if(!sel)
	return;
    if(!sel->isfloating)
	return;
    sscanf(arg, "%d %d %d %d", &dx, &dy, &dw, &dh);
    if(dw && (dw < sel->incw)) dw = (dw/abs(dw))*sel->incw;
    if(dh && (dh < sel->inch)) dh = (dh/abs(dh))*sel->inch;
    resize(sel, curmonitor(), sel->x+dx, sel->y+dy, sel->w+dw, sel->h+dh, True);
}

void
getpointer(int *x, int *y) {
    int di;
    unsigned int dui;
    Window dummy;

    XQueryPointer(dpy, root, &dummy, &dummy, x, y, &di, &di, &dui);
}

Monitor*
getmonitor(int x, int y) {
    Monitor *m;
    for(m = monitors; m; m = m->next) {
	if((x >= m->sx && x <= m->sx + m->sw)) {
	    break;
	}
    }
    return m;
}

Monitor*
clientmonitor(Client *c) {
    Monitor *m;
    int i;
    if(c) {
	for(m = monitors; m; m = m->next) {
	    for(i = 0; i < ntags; i++)
		if(c->tags[i] & m->seltags[i])
		    return m;
	}
    }
    return curmonitor();
}

Monitor*
curmonitor() {
    int x, y;
    getpointer(&x, &y);
    return getmonitor(x, y);
}

void
movemouse(Client *c) {
    int x1, y1, ocx, ocy, nx, ny, i;
    XEvent ev;
    Monitor *m, *nm;

    if(c->isbastard)
	return;
    m = curmonitor();
    ocx = nx = c->x + m->sx;
    ocy = ny = c->y + m->sy;
    if(XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
		    None, cursor[CurMove], CurrentTime) != GrabSuccess)
	    return;
    c->ismax = False;
    XRaiseWindow(dpy, c->frame);
    getpointer(&x1, &y1);
    for(;;) {
	    XMaskEvent(dpy, MOUSEMASK | ExposureMask | SubstructureRedirectMask, &ev);
	    switch (ev.type) {
	    case ButtonRelease:
		    XUngrabPointer(dpy, CurrentTime);
		    return;
	    case ConfigureRequest:
	    case Expose:
	    case MapRequest:
		    handler[ev.type](&ev);
		    break;
	    case MotionNotify:
		    XSync(dpy, False);
		    /* we are probably moving to a different monitor */
		    nm = curmonitor();
		    nx = ocx + (ev.xmotion.x - x1);
		    ny = ocy + (ev.xmotion.y - y1);
		    if(abs(m->wax + nx) < SNAP)
			    nx = m->wax;
		    else if(abs((m->wax + m->waw) - (nx + c->w + 2 * c->border)) < SNAP)
			    nx = m->wax + m->waw - c->w - 2 * c->border;
		    if(abs(m->way - ny) < SNAP)
			    ny = m->way;
		    else if(abs((m->way + m->wah) - (ny + c->h + 2 * c->border)) < SNAP)
			    ny = m->way + m->wah - c->h - 2 * c->border;
		    resize(c, nm, nx-nm->sx, ny-nm->sy, c->w, c->h, False);
		    if(m != nm) {
			for(i = 0; i < ntags; i++)
			    c->tags[i] = nm->seltags[i];
			updateatom[WindowDesk](c);
			drawclient(c);
		    }
		    break;
	    }
    }
}

Client *
nexttiled(Client *c, Monitor *m) {
    for(; c && (c->isfloating || !isvisible(c, m) || c->isbastard || c->isicon); c = c->next);
    return c;
}

Client *
prevtiled(Client *c, Monitor *m) {
    for(; c && (c->isfloating || !isvisible(c, m) || c->isbastard || c->isicon); c = c->prev);
    return c;
}

void
reparentnotify(XEvent *e) {
    Client *c;
    XReparentEvent *ev = &e->xreparent;

    if((c = getclient(ev->window, clients, ClientWindow)))
	if(ev->parent != c->frame)
	    unmanage(c);
}

void
propertynotify(XEvent *e) {
    Client *c;
    Window trans;
    XPropertyEvent *ev = &e->xproperty;

    if(ev->state == PropertyDelete)
	    return; /* ignore */
    if((c = getclient(ev->window, clients, ClientWindow))) {
	    switch (ev->atom) {
		    case XA_WM_TRANSIENT_FOR:
			    XGetTransientForHint(dpy, c->win, &trans);
			    if(!c->isfloating && (c->isfloating = (getclient(trans, clients, ClientWindow) != NULL)))
				    arrange(clientmonitor(c));
			    break;
		    case XA_WM_NORMAL_HINTS:
			    updatesizehints(c);
			    break;
		    case XA_WM_NAME:
			    updatetitle(c);
			    break;
	    }
	    if(ev->atom == atom[StrutPartial]) {
		    updatestruts(ev->window);
		    arrange(clientmonitor(c));
	    } else if(ev->atom == atom[WindowName]) 
		    updatetitle(c);
    }
}

void
quit(const char *arg) {
    running = False;
    if(arg){
	cleanup();
	execvp(cargv[0], cargv);
	eprint("Can't exec: %s\n", strerror(errno));
    }
}

void
resize(Client *c, Monitor *m, int x, int y, int w, int h, Bool sizehints) {
    XWindowChanges wc;
    c->th = c->title&&(c->isfloating||dectiled||ISLTFLOATING(m)) ? dc.h : 0;
    if(sizehints) {
	/* set minimum possible */
	if (w < 1)
		w = 1;
	if (h < 1)
		h = 1;

	/* temporarily remove base dimensions */
	w -= c->basew;
	h -= c->baseh;

	/* adjust for aspect limits */
	if (c->minay > 0 && c->maxay > 0 && c->minax > 0 && c->maxax > 0) {
		if (w * c->maxay > h * c->maxax)
			w = h * c->maxax / c->maxay;
		else if (w * c->minay < h * c->minax)
			h = w * c->minay / c->minax;
	}

	/* adjust for increment value */
	if(c->incw)
		w -= w % c->incw;
	if(c->inch)
		h -= h % c->inch;

	/* restore base dimensions */
	w += c->basew;
	h += c->baseh;

	if(c->minw > 0 && w < c->minw)
		w = c->minw;
	if(c->minh > 0 && h - c->th < c->minh)
		h = c->minh + c->th;
	if(c->maxw > 0 && w > c->maxw)
		w = c->maxw;
	if(c->maxh > 0 && h - c->th > c->maxh)
		h = c->maxh + c->th;
    }
    if(w <= 0 || h <= 0)
	    return;
    /* offscreen appearance fixes */
    if(x > m->wax + m->sw)
	    x = m->sw - w - 2 * c->border;
    if(y > m->way + m->sh)
	    y = m->sh - h - 2 * c->border;
#if 0
    if(x + w + 2 * c->border < m->sx)
	    x = m->wax;
    if(y + h + 2 * c->border < m->sy)
	    y = m->way;
#endif
    if(c->title) {
	if(c->th)
	    XMoveResizeWindow(dpy, c->title, 0, 0, w, c->th);
	drawclient(c);
    }
    if(c->m != m || c->x != x || c->y != y || c->w != w || c->h != h) {
	    if(c->isfloating || ISLTFLOATING(m)) {
		    c->sfx = x;
		    c->sfy = y;
		    c->sfw = w;
		    c->sfh = h;
		    c->isplaced = True;
	    }
	    c->x = x;
	    c->y = y;
	    c->w = w;
	    c->h = h;
	    c->m = m;
	    XMoveResizeWindow(dpy, c->frame, m->sx + c->x, m->sy + c->y, c->w, c->h);
	    wc.x = 0;
	    wc.y = c->th;
	    wc.width = w;
	    wc.height = h - c->th;
	    wc.border_width = 0;
	    XConfigureWindow(dpy, c->win, CWY|CWWidth|CWHeight|CWBorderWidth, &wc);
	    configure(c);
	    XSync(dpy, False);
    }
}

void
resizemouse(Client *c) {
    int ocx, ocy, nw, nh;
    Monitor *cm;
    XEvent ev;

    if(c->isbastard)
	return;
    cm = curmonitor();

    ocx = c->x + cm->sx;
    ocy = c->y + cm->sy;
    if(XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
		    None, cursor[CurResize], CurrentTime) != GrabSuccess)
	    return;
    c->ismax = False;
    XWarpPointer(dpy, None, c->win, 0, 0, 0, 0, c->w + c->border - 1, c->h + c->border - 1);
    for(;;) {
	    XMaskEvent(dpy, MOUSEMASK | ExposureMask | SubstructureRedirectMask , &ev);
	    switch(ev.type) {
	    case ButtonRelease:
		    XWarpPointer(dpy, None, c->win, 0, 0, 0, 0,
				    c->w + c->border - 1, c->h + c->border - 1);
		    XUngrabPointer(dpy, CurrentTime);
		    while(XCheckMaskEvent(dpy, EnterWindowMask, &ev));
		    return;
	    case ConfigureRequest:
	    case Expose:
	    case MapRequest:
		    handler[ev.type](&ev);
		    break;
	    case MotionNotify:
		    XSync(dpy, False);
		    if((nw = ev.xmotion.x - ocx - 2 * c->border + 1) <= 0)
			    nw = MINWIDTH;
		    if((nh = ev.xmotion.y - ocy - 2 * c->border + 1) <= 0)
			    nh = MINHEIGHT;
		    resize(c, cm, c->x, c->y, nw, nh, True);
		    break;
	    }
    }
}

void
restack(Monitor *m) {
    Client *c;
    XEvent ev;
    Window *wl;
    int i,n;

    if(!sel)
	return;

    if(ISLTFLOATING(m) || sel->isfloating) {
	XRaiseWindow(dpy, sel->frame);
	goto end;
    }

    for(n = 0, c = stack; c; c = c->snext){
	if(isvisible(c, m) && !c->isicon){
		n++;
	}
    }
    if(n==1)
	return;
    wl = malloc(sizeof(Window)*n);
    for(i = 0, c = stack; c && i<n; c = c->snext)
	if(isvisible(c, m) && !c->isicon && c->isbastard){
		    wl[i++] = c->frame;
	}
    for(c = stack; c && i<n; c = c->snext)
	if(isvisible(c, m) && !c->isicon){
	    if(!c->isbastard && c->isfloating)
		    wl[i++] = c->frame;
	}
    for(c = stack; c && i<n; c = c->snext)
	if(isvisible(c, m) && !c->isicon){
	    if(!c->isfloating && !c->isbastard)
		    wl[i++] = c->frame;
	}
    XRestackWindows(dpy, wl, n);
    free(wl);
end:
    XSync(dpy, False);
    while(XCheckMaskEvent(dpy, EnterWindowMask, &ev));
}

void
run(void) {
    fd_set rd;
    int xfd;
    XEvent ev;

    /* main event loop */
    XSync(dpy, False);
    xfd = ConnectionNumber(dpy);
    while(running) {
	    FD_ZERO(&rd);
	    FD_SET(xfd, &rd);
	    if(select(xfd + 1, &rd, NULL, NULL, NULL) == -1) {
		    if(errno == EINTR)
			    continue;
		    eprint("select failed\n");
	    }
	    while(XPending(dpy)) {
		    XNextEvent(dpy, &ev);
		    if(handler[ev.type])
			    (handler[ev.type])(&ev); /* call handler */
	    }
    }
}

void
scan(void) {
    unsigned int i, num;
    Window *wins, d1, d2;
    XWindowAttributes wa;

    wins = NULL;
    if(XQueryTree(dpy, root, &d1, &d2, &wins, &num)) {
	    for(i = 0; i < num; i++) {
		    if(!XGetWindowAttributes(dpy, wins[i], &wa) || 
		    wa.override_redirect || XGetTransientForHint(dpy, wins[i], &d1))
			    continue;
		    if(wa.map_state == IsViewable || getstate(wins[i]) == IconicState || getstate(wins[i]) == NormalState)
			    manage(wins[i], &wa);
	    }
	    for(i = 0; i < num; i++) { /* now the transients */
		    if(!XGetWindowAttributes(dpy, wins[i], &wa))
			    continue;
		    if(XGetTransientForHint(dpy, wins[i], &d1)
		    && (wa.map_state == IsViewable || getstate(wins[i]) == IconicState || getstate(wins[i]) == NormalState ))
			    manage(wins[i], &wa);
	    }
    }
    if(wins)
	    XFree(wins);
}

void
setclientstate(Client *c, long state) {
    long data[] = {state, None};
    long winstate[2];

    XChangeProperty(dpy, c->win, atom[WMState], atom[WMState], 32,
		    PropModeReplace, (unsigned char *)data, 2);
    if(state == NormalState) {
	c->isicon = False;
	XDeleteProperty(dpy, c->win, atom[WindowState]);
    } else {
	winstate[0] = atom[WindowStateHidden];
	XChangeProperty(dpy, c->win, atom[WindowState], XA_ATOM, 32,
		PropModeReplace, (unsigned char*)winstate, 1);
    }
}

void
setlayout(const char *arg) {
    unsigned int i;

    if(!arg) {
	    if(&layouts[++ltidxs[curmontag]] == &layouts[LENGTH(layouts)])
		    ltidxs[curmontag] = 0;
    } else {
	    for(i = 0; i < LENGTH(layouts); i++)
		    if(!strcmp(arg, layouts[i].symbol))
			    break;
	    if(i == LENGTH(layouts))
		    return;
	    ltidxs[curmontag] = i;
    }
    if(sel)
	    arrange(curmonitor());
    updateatom[ELayout](NULL);
}

void
setmwfact(const char *arg) {
    double delta;

    if(!domwfact)
	    return;
    /* arg handling, manipulate mwfact */
    if(arg == NULL)
	    mwfacts[curmontag] = MWFACT;
    else if(sscanf(arg, "%lf", &delta) == 1) {
	    if(arg[0] == '+' || arg[0] == '-')
		    mwfacts[curmontag] += delta;
	    else
		    mwfacts[curmontag] = delta;
	    if(mwfacts[curmontag] < 0.1)
		    mwfacts[curmontag] = 0.1;
	    else if(mwfacts[curmontag] > 0.9)
		    mwfacts[curmontag] = 0.9;
    }
    arrange(curmonitor());
}

void
initlayouts(){
	int i,j;
	char conf[32], xres[256], buf[5];
	float mwfact;
	int nmaster;

	/* init layouts */
	bzero(buf, 5);
	nmasters = (int*)emallocz(sizeof(unsigned int) * ntags);
	ltidxs = (int*)emallocz(sizeof(unsigned int) * ntags);
	mwfacts = (double*)emallocz(sizeof(double) * ntags);
	bpos = (int*)emallocz(sizeof(unsigned int) * ntags);

	snprintf(buf, 5, "%.2f", MWFACT);
	mwfact = atof(getresource("mwfact", buf));
	bzero(buf, 5);
	snprintf(buf, 5, "%d", NMASTER);
	nmaster = atoi(getresource("nmaster", buf));
	    for(i = 0; i < ntags; i++) {
		ltidxs[i] = 0;
		snprintf(conf, 31, "tags.layout%d", i);
		strncpy(xres, getresource(conf, getresource("deflayout", "i")), 255);
		for (j = 0; j < LENGTH(layouts); j++) {
		    if (!strcmp(layouts[j].symbol, xres)) {
			ltidxs[i] = j;
			break;
		    }
		}
		mwfacts[i] = mwfact;
		nmasters[i] = nmaster;
		bpos[i] = BARPOS;
	    }

    updateatom[ELayout](NULL);
}

void
inittags(){
    int i;
    char tmp[25] = "\0";
    ntags = atoi(getresource("tags.number", "5"));
    tags = emallocz(ntags*sizeof(char*));
    for(i = 0; i < ntags; i++){
	tags[i] = emallocz(25*sizeof(char));
	snprintf(tmp, 24, "tags.name%d", i);
	snprintf(tags[i], 24, "%s", getresource(tmp, "null"));
    }
}

void 
sighandler(int signum) {
    if(signum == SIGHUP)
	quit((char*)signum);
    else
	quit(0);
}

void
setup(void) {
	int d;
	int i, j;
	unsigned int mask;
	Window w;
	Monitor *m;
	XModifierKeymap *modmap;
	XSetWindowAttributes wa;

	/* init EWMH atom */
	initatom();
	
	/* init cursors */
	cursor[CurNormal] = XCreateFontCursor(dpy, XC_left_ptr);
	cursor[CurResize] = XCreateFontCursor(dpy, XC_sizing);
	cursor[CurMove] = XCreateFontCursor(dpy, XC_fleur);

	/* init modifier map */
	modmap = XGetModifierMapping(dpy);
	for(i = 0; i < 8; i++)
		for(j = 0; j < modmap->max_keypermod; j++) {
			if(modmap->modifiermap[i * modmap->max_keypermod + j]
			== XKeysymToKeycode(dpy, XK_Num_Lock))
				numlockmask = (1 << i);
		}
	XFreeModifiermap(modmap);

	/* select for events */
	wa.event_mask = SubstructureRedirectMask | SubstructureNotifyMask 
		| EnterWindowMask | LeaveWindowMask | StructureNotifyMask | ButtonPressMask | ButtonReleaseMask;
	wa.cursor = cursor[CurNormal];
	XChangeWindowAttributes(dpy, root, CWEventMask | CWCursor, &wa);
	XSelectInput(dpy, root, wa.event_mask);

	/* init resource database */
	XrmInitialize();
	char conf[256];
	snprintf(conf, 255, "%s/%s", getenv("HOME"), "/.echinus");
	chdir(conf);
	xrdb = XrmGetFileDatabase("echinusrc");
	if(!xrdb) {
	    fprintf(stderr, "echinus: cannot open configuration file in %s\n", conf);
	    chdir(SYSCONFPATH);
	    xrdb = XrmGetFileDatabase("echinusrc");
	    if(!xrdb)
		eprint("echinus: cannot open configuration file\n");
	}

	/* init tags */
	inittags();
	/* init geometry */
	initmonitors(NULL);

	/* init modkey */
	initrules();
	initkeys();
	initlayouts();
	updateatom[NumberOfDesk](NULL);
	updateatom[DeskNames](NULL);
	updateatom[CurDesk](NULL);

	compileregs();

	/* grab keys */
	keypress(NULL);

	/* init appearance */
	dc.norm[ColBorder] = getcolor(getresource("normal.border", NORMBORDERCOLOR));
	dc.norm[ColBG] = getcolor(getresource("normal.bg", NORMBGCOLOR));
	dc.norm[ColFG] = getcolor(getresource("normal.fg", NORMFGCOLOR));
	dc.norm[ColButton] = getcolor(getresource("normal.button", NORMBUTTONCOLOR));

	dc.sel[ColBorder] = getcolor(getresource("selected.border", SELBORDERCOLOR));
	dc.sel[ColBG] = getcolor(getresource("selected.bg", SELBGCOLOR));
	dc.sel[ColFG] = getcolor(getresource("selected.fg", SELFGCOLOR));
	dc.sel[ColButton] = getcolor(getresource("selected.button", SELBUTTONCOLOR));

	dc.xftsel = emallocz(sizeof(XftColor));
	dc.xftnorm = emallocz(sizeof(XftColor));
	XftColorAllocName(dpy, DefaultVisual(dpy, screen), DefaultColormap(dpy, screen), getresource("selected.fg", SELFGCOLOR), dc.xftsel);
	XftColorAllocName(dpy, DefaultVisual(dpy, screen), DefaultColormap(dpy, screen), getresource("normal.fg", SELFGCOLOR), dc.xftnorm);
	if(!dc.xftnorm || !dc.xftnorm)
	     eprint("error, cannot allocate colors\n");
	initfont(getresource("font", FONT));
	look.borderpx = atoi(getresource("border", BORDERPX));
	look.uf_opacity = atof(getresource("opacity", NF_OPACITY));
	look.drawoutline = atoi(getresource("outline", "0"));
	look.titlelayout = getresource("titlelayout", "T N IMC");

	strncpy(terminal, getresource("terminal", TERMINAL), 255);

	dc.h = atoi(getresource("title", TITLEHEIGHT));
	if(!dc.h)
	    dc.h = dc.font.height + 2;
	dectiled = atoi(getresource("decoratetiled", DECORATETILED));
	hidebastards = atoi(getresource("hidebastards", "0"));
	sloppy = atoi(getresource("sloppy", "0"));

	for(m = monitors; m; m = m->next) {
	    m->struts[RightStrut] = m->struts[LeftStrut] = m->struts[TopStrut] = m->struts[BotStrut] = 0;
	    updategeom(m);
	}

	dc.gc = XCreateGC(dpy, root, 0, 0);

	/* buttons */
	initbuttons();
	chdir(getenv("HOME"));

	/* multihead support */
	selscreen = XQueryPointer(dpy, root, &w, &w, &d, &d, &d, &d, &mask);
}

void
spawn(const char *arg) {
	static char shell[] = "/bin/sh";

	if(!arg)
		return;
	/* The double-fork construct avoids zombie processes and keeps the code
	 * clean from stupid signal handlers. */
	if(fork() == 0) {
		if(fork() == 0) {
			if(dpy)
				close(ConnectionNumber(dpy));
			setsid();
			execl(shell, shell, "-c", arg, (char *)NULL);
			fprintf(stderr, "echinus: execl '%s -c %s'", shell, arg);
			perror(" failed");
		}
		exit(0);
	}
	wait(0);
}

void
tag(const char *arg) {
    int i;

    if(!sel)
	    return;
    for(i = 0; i < ntags; i++)
	    sel->tags[i] = (NULL == arg);
    sel->tags[idxoftag(arg)] = True;
    updateatom[WindowDesk](sel);
    arrange(NULL);
}

void
bstack(Monitor *m) {
    int i, n, nx, ny, nw, nh, mh, tw;
    Client *c, *mc;

    domwfact = dozoom = True;
    for(n = 0, c = nexttiled(clients, m); c; c = nexttiled(c->next, m))
	n++;

    mh = (n == 1) ? m->wah : mwfacts[m->curtag] * m->wah;
    tw = (n > 1) ? m->waw / (n - 1) : 0;

    nx = m->wax;
    ny = m->way;
    nh = 0;
    for(i = 0, c = mc = nexttiled(clients, m); c; c = nexttiled(c->next, m), i++) {
	c->ismax = False;
	if(i == 0) {
	    nh = mh - 2 * c->border;
	    nw = m->waw - 2 * c->border;
	    nx = m->wax;
	} else {
	    if(i == 1) {
		nx = m->wax;
		ny += mc->h+c->border;
		nh = (m->way + m->wah) - ny - 2 * c->border;
	    }
	    if(i + 1 == n)
		nw = (m->wax + m->waw) - nx - 2 * c->border;
	    else
		nw = tw - c->border;
	}
	resize(c, m, nx, ny, nw, nh, False);
	if(n > 1 && tw != curwaw)
	    nx = c->x + c->w + c->border;
    }
}

void
tile(Monitor *m) {
	int i, n, nx, ny, nw, nh, mw, mh, th;
	Client *c, *mc;
	wasfloating = False;

	domwfact = dozoom = True;
	for(n = 0, c = nexttiled(clients, m); c; c = nexttiled(c->next, m))
		n++;

	/* window geoms */
	mh = (n <= nmasters[m->curtag]) ? m->wah / (n > 0 ? n : 1) : m->wah / nmasters[m->curtag];
	mw = (n <= nmasters[m->curtag]) ? m->waw : mwfacts[m->curtag] * m->waw;
	th = (n > nmasters[m->curtag]) ? m->wah / (n - nmasters[m->curtag]) : 0;
	if(n > nmasters[m->curtag] && th < dc.h)
		th = m->wah;

	nx = m->wax;
	ny = m->way;
	nw = 0;
	for(i = 0, c = mc = nexttiled(clients, m); c; c = nexttiled(c->next, m), i++) {
		c->ismax = False;
		if(i < nmasters[m->curtag]) { /* master */
			ny = m->way + i * (mh - c->border);
			nw = mw - 2 * c->border;
			nh = mh;
			if(i + 1 == (n < nmasters[m->curtag] ? n : nmasters[m->curtag])) /* remainder */
				nh = m->way + m->wah - ny;
			nh -= 2 * c->border;
		} else {	/* tile window */
			if(i == nmasters[m->curtag]) {
				ny = m->way;
				nx += mc->w + mc->border;
				nw = m->waw - nx - 2*c->border + m->wax;
			}
			else 
			    ny -= c->border;
			if(i + 1 == n) /* remainder */
				nh = (m->way + m->wah) - ny - 2 * c->border;
			else
				nh = th - 2 * c->border;
		}
		resize(c, m, nx, ny, nw, nh, False);
		if(n > nmasters[m->curtag] && th != m->wah){
			ny = c->y + c->h + 2 * c->border;
		}
	}
}

void
togglestruts(const char *arg) {
    bpos[curmontag] = (bpos[curmontag] == StrutsOn) ? (hidebastards ? StrutsHide : StrutsOff) : StrutsOn;
    updategeom(curmonitor());
    arrange(curmonitor());
}

void
togglefloating(const char *arg) {
    if(!sel)
	    return;

    if(ISLTFLOATING(curmonitor()))
	    return;

    sel->isfloating = !sel->isfloating;
    if(sel->isfloating) {
	    /*restore last known float dimensions*/
	    resize(sel, curmonitor(), sel->sfx, sel->sfy, sel->sfw, sel->sfh, False);
    } else {
	    /*save last known float dimensions*/
	    sel->sfx = sel->x;
	    sel->sfy = sel->y;
	    sel->sfw = sel->w;
	    sel->sfh = sel->h;
    }
    drawclient(sel);
    arrange(curmonitor());
}

void
togglemax(const char *arg) {
    XEvent ev;

    if(!sel || sel->isfixed)
	    return;
    if((sel->ismax = !sel->ismax)) {
	    sel->rx = sel->x;
	    sel->ry = sel->y;
	    sel->rw = sel->w;
	    sel->rh = sel->h;
	    resize(sel, curmonitor(), cursx - sel->border, cursy - sel->border, cursw + 2*sel->border, cursh + 2*sel->border + sel->th, False);
    } else {
	resize(sel, curmonitor(), sel->rx, sel->ry, sel->rw, sel->rh, True);
    }
    while(XCheckMaskEvent(dpy, EnterWindowMask, &ev));
}

void
toggletag(const char *arg) {
    int i, j;

    if(!sel)
	    return;
    i = idxoftag(arg);
    sel->tags[i] = !sel->tags[i];
    for(j = 0; j < ntags && !sel->tags[j]; j++);
    if(j == ntags)
	    sel->tags[i] = True; /* at least one tag must be enabled */
    drawclient(sel);
    arrange(NULL);
}

void
togglemonitor(const char *arg) {
    Monitor *m, *cm;
    int x, y;

    getpointer(&x, &y);
    for(cm = curmonitor(), m = monitors; m == cm && m && m->next; m = m->next);
    XWarpPointer(dpy, None, root, 0, 0, 0, 0, m->sx + x % m->sw, m->sy + y % m->sh);
    focus(NULL);
}

void
toggleview(const char *arg) {
    int i, j;
    Monitor *m;
    i = idxoftag(arg);
    for(m = monitors; m; m = m->next) {
	memcpy(m->prevtags, m->seltags, ntags*(sizeof m->seltags));
	m->seltags[i] = ((m == curmonitor()) ? !m->seltags[i] : False);
	for(j = 0; j < ntags && !m->seltags[j]; j++);
	if(j == ntags) {
	    m->seltags[i] = True; /* at least one tag must be viewed */
	    j = i;
	}
	if(m->curtag == i)
	    m->curtag = j;
	arrange(m);
    }
    focus(NULL);
    updateatom[CurDesk](NULL);
}

void
focusview(const char *arg) {
    Client *c;
    int i;

    toggleview(arg);
    i = idxoftag(arg);
    if (!curseltags[i])
	    return;
    for(c = clients; c; c = c->next){
	if (c->tags[i] && !c->isbastard) {
		focus(c);
		c->isplaced = True;
	}
    }
    restack(curmonitor());
}

void
unban(Client *c) {
    if(!c->isbanned)
	    return;
    XSelectInput(dpy, c->win,
		     CLIENTMASK&~(StructureNotifyMask|EnterWindowMask));
    XSelectInput(dpy, c->frame, NoEventMask);
    XMapWindow(dpy, c->frame);
    XSelectInput(dpy, c->win, CLIENTMASK);
    XSelectInput(dpy, c->frame, FRAMEMASK);
    setclientstate(c, NormalState);
    if(c->isfloating)
	drawclient(c);
    c->isbanned = False;
}

void
unmanage(Client *c) {
    Monitor *m;
    XWindowChanges wc;
    Bool isfloating;
    Window trans;

    m = clientmonitor(c);
    isfloating = c->isfloating || c->isfixed || XGetTransientForHint(dpy, c->win, &trans);
    if(c->title) {
	XftDrawDestroy(c->xftdraw);
	XDestroyWindow(dpy, c->title);
	c->title = (Window)NULL;
    }
    XSelectInput(dpy, c->win,
		     CLIENTMASK&~(StructureNotifyMask|EnterWindowMask));
    XSelectInput(dpy, c->frame, NoEventMask);
    XReparentWindow(dpy, c->win, RootWindow(dpy, screen), c->x, c->y);
    XMoveWindow(dpy, c->win, c->x, c->y);
    XDestroyWindow(dpy, c->frame);
    wc.border_width = c->oldborder;
    /* The server grab construct avoids race conditions. */
    XGrabServer(dpy);
    XSetErrorHandler(xerrordummy);
    XConfigureWindow(dpy, c->win, CWBorderWidth, &wc); /* restore border */
    detach(c);
    detachstack(c);
    if(sel == c)
	focus(NULL);
    XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
    setclientstate(c, WithdrawnState);
    /* c->tags points to monitor */
    if(!c->isbastard)
	free(c->tags);
    free(c);
    XSync(dpy, False);
    XSetErrorHandler(xerror);
    XUngrabServer(dpy);
    if(!isfloating)
	arrange(m);
    updateatom[ClientList](NULL);
}

void
updategeom(Monitor *m) {
    m->wax = 0;
    m->way = 0;
    m->wah = m->sh;
    m->waw = m->sw;
    switch(bpos[m->curtag]){
    default:
	m->wax += m->struts[LeftStrut];
	m->waw -= (m->wax + m->struts[RightStrut]);
	m->way += m->struts[TopStrut];
	DPRINTF("DH %d strut %d\n", DisplayHeight(dpy, screen), m->struts[BotStrut]);
	m->wah = m->struts[BotStrut] ? DisplayHeight(dpy, screen) - m->struts[BotStrut] - m->way : m->sh - m->way;
	DPRINTF("WAH %d\n", m->wah);
	break;
    case StrutsHide:
    case StrutsOff:
	break;
    }
}

void
unmapnotify(XEvent *e) {
    Client *c;
    XUnmapEvent *ev = &e->xunmap;
    XWindowAttributes wa;

    if((c = getclient(ev->window, clients, ClientWindow))) {
	if(c->isicon)
	    return;
	XGetWindowAttributes(dpy, ev->window, &wa);
	if(wa.map_state == IsUnmapped && c->title){
		XGetWindowAttributes(dpy, c->title, &wa);
		if(wa.map_state == IsViewable) {
			ban(c);
			unmanage(c);
		}
	}
    }
}

void
updatesizehints(Client *c) {
	long msize;
	XSizeHints size;

	if(!XGetWMNormalHints(dpy, c->win, &size, &msize) || !size.flags)
		size.flags = PSize;
	c->flags = size.flags;
	if(c->flags & PBaseSize) {
		c->basew = size.base_width;
		c->baseh = size.base_height;
	} else if(c->flags & PMinSize) {
		c->basew = size.min_width;
		c->baseh = size.min_height;
	}
	else
		c->basew = c->baseh = 0;
	if(c->flags & PResizeInc) {
		c->incw = size.width_inc;
		c->inch = size.height_inc;
	}
	else
		c->incw = c->inch = 0;
	if(c->flags & PMaxSize) {
		c->maxw = size.max_width;
		c->maxh = size.max_height;
	}
	else
		c->maxw = c->maxh = 0;
	if(c->flags & PMinSize) {
		c->minw = size.min_width;
		c->minh = size.min_height;
	} else if(c->flags & PBaseSize) {
		c->minw = size.base_width;
		c->minh = size.base_height;
	}
	else
		c->minw = c->minh = 0;
	if(c->flags & PAspect) {
		c->minax = size.min_aspect.x;
		c->maxax = size.max_aspect.x;
		c->minay = size.min_aspect.y;
		c->maxay = size.max_aspect.y;
	}
	else
		c->minax = c->maxax = c->minay = c->maxay = 0;
	c->isfixed = (c->maxw && c->minw && c->maxh && c->minh
			&& c->maxw == c->minw && c->maxh == c->minh);
}

void
updatetitle(Client *c) {
    if(!gettextprop(c->win, atom[WindowName], c->name, sizeof c->name))
	    gettextprop(c->win, atom[WMName], c->name, sizeof c->name);
    drawclient(c);
}

/* There's no way to check accesses to destroyed windows, thus those cases are
 * ignored (ebastardly on UnmapNotify's).  Other types of errors call Xlibs
 * default error handler, which may call exit.	*/
int
xerror(Display *dpy, XErrorEvent *ee) {
    if(ee->error_code == BadWindow
    || (ee->request_code == X_SetInputFocus && ee->error_code == BadMatch)
    || (ee->request_code == X_PolyText8 && ee->error_code == BadDrawable)
    || (ee->request_code == X_PolyFillRectangle && ee->error_code == BadDrawable)
    || (ee->request_code == X_PolySegment && ee->error_code == BadDrawable)
    || (ee->request_code == X_ConfigureWindow && ee->error_code == BadMatch)
    || (ee->request_code == X_GrabKey && ee->error_code == BadAccess)
    || (ee->request_code == X_CopyArea && ee->error_code == BadDrawable))
	    return 0;
    fprintf(stderr, "echinus: fatal error: request code=%d, error code=%d\n",
	    ee->request_code, ee->error_code);
    return xerrorxlib(dpy, ee); /* may call exit */
}

int
xerrordummy(Display *dsply, XErrorEvent *ee) {
    return 0;
}

/* Startup Error handler to check if another window manager
 * is already running. */
int
xerrorstart(Display *dsply, XErrorEvent *ee) {
    otherwm = True;
    return -1;
}

void
view(const char *arg) {
    int i, prevcurtag;
    Monitor *m;
    int swapping = 0;

    if(curseltags[idxoftag(arg)])
	return;

    for(m = monitors; m ; m = m->next) {
	    if(m->seltags[idxoftag(arg)] && m != curmonitor()) {
		swapping = 1;
		m->seltags[idxoftag(arg)] = False;
		m->seltags[curmontag] = True;
	    }
    }
    memcpy(curprevtags, curseltags, ntags*(sizeof curseltags));
    for(i = 0; i < ntags; i++)
	    curseltags[i] = (NULL == arg);
    curseltags[idxoftag(arg)] = True;
    prevcurtag = curmontag;
    curmontag = idxoftag(arg);
    if (bpos[prevcurtag] != bpos[curmontag])
	updategeom(curmonitor());
    if(swapping)
	arrange(NULL);
    else
	arrange(curmonitor());
    focus(NULL);
    updateatom[CurDesk](NULL);
}

void
viewprevtag(const char *arg) {
    Bool tmptags[ntags];
    int i, prevcurtag;
 
    i = 0;
    while(i < ntags-1 && !curprevtags[i]) i++;
    prevcurtag = curmontag;
    curmontag = i;

    memcpy(tmptags, curseltags, ntags*(sizeof curseltags));
    memcpy(curseltags, curprevtags, ntags*(sizeof curseltags));
    memcpy(curprevtags, tmptags, ntags*(sizeof curseltags));
    if (bpos[prevcurtag] != bpos[curmontag])
	updategeom(curmonitor());
    arrange(NULL);
    updateatom[CurDesk](NULL);
}

void
viewlefttag(const char *arg) {
    int i;

    for(i = 0; i < ntags; i++) {
	if(i && curseltags[i]) {
		view(tags[i-1]);
		break;
	}
    }
}

void
viewrighttag(const char *arg) {
    int i;

    for(i = 0; i < ntags-1; i++) {
	if(curseltags[i]) {
	    view(tags[i+1]);
	    break;
	}
    }
}

void
zoom(const char *arg) {
    Client *c;

    if(!sel || !dozoom || sel->isfloating)
	    return;
    if((c = sel) == nexttiled(clients, curmonitor()))
	    if(!(c = nexttiled(c->next, curmonitor())))
		    return;
    detach(c);
    attach(c);
    arrange(curmonitor());
    focus(c);
}

int
main(int argc, char *argv[]) {
    if(argc == 2 && !strcmp("-v", argv[1]))
	    eprint("echinus-"VERSION", © 2006-2008 Anselm R. Garbe, Sander van Dijk, "
		   "Jukka Salmi, Premysl Hruby, Szabolcs Nagy, Alexander Polakov\n");
    else if(argc != 1)
	    eprint("usage: echinus [-v]\n");

    setlocale(LC_CTYPE, "");
    if(!(dpy = XOpenDisplay(0)))
	    eprint("echinus: cannot open display\n");
    signal(SIGHUP, sighandler);
    signal(SIGINT, sighandler);
    signal(SIGQUIT, sighandler);
    cargv = argv;
    screen = DefaultScreen(dpy);
    root = RootWindow(dpy, screen);

    checkotherwm();
    setup();
    scan();
    run();
    cleanup();

    XCloseDisplay(dpy);
    return 0;
}
