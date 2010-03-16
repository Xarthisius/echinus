void
drawtext(const char *text, Drawable drawable, XftDraw *xftdrawable, unsigned long col[ColLast], unsigned int position) {
    int x, y, w, h;
    char buf[256];
    unsigned int len, olen;
    if(!text)
	    return;
    olen = len = strlen(text);
    w = 0;
    if(len >= sizeof buf)
	    len = sizeof buf - 1;
    memcpy(buf, text, len);
    buf[len] = 0;
    h = dc.h;
    y = dc.font.ascent + 1;
    x = dc.x + dc.font.height/2;
    /* shorten text if necessary */
    while(len && (w = textnw(buf, len)) > dc.w){
	    buf[--len] = 0;
    }
    if(len < olen) {
	    if(len > 1)
		    buf[len - 1] = '.';
	    if(len > 2)
		    buf[len - 2] = '.';
	    if(len > 3)
		    buf[len - 3] = '.';
    }
    if(w > dc.w)
	    return; /* too long */
    switch(position) {
	case TitleCenter:
		x = dc.x + dc.w/2 - w/2;
		break;
	case TitleLeft:
		x = dc.x + h/2;
		break;
	case TitleRight:
		x = dc.w - w - h;
		break;
    }
    while(x <= 0)
	    x = dc.x++;
    XftDrawStringUtf8(xftdrawable, (col==dc.norm) ? dc.xftnorm : dc.xftsel,
	    dc.font.xftfont, x, y, (unsigned char*)buf, len);
    if(look.drawoutline) {
	    XSetForeground(dpy, dc.gc, col[ColBorder]);
	    XDrawLine(dpy, drawable, dc.gc, 0, dc.h-1, dc.w, dc.h-1);
    }
    dc.x = x + w;
}

Pixmap
initpixmap(const char *file, Button *b) {
    b->pm = XCreatePixmap(dpy, root, dc.h, dc.h, 1);
    if(BitmapSuccess == XReadBitmapFile(dpy, root, file, &b->pw, &b->ph, &b->pm, &b->px, &b->py))
	return 0;
    else
	eprint("echinus: cannot load Button pixmaps, check your ~/.echinusrc\n");
    return 0;
}

void
initbuttons() {
    XSetForeground(dpy, dc.gc, dc.norm[ColButton]);
    XSetBackground(dpy, dc.gc, dc.norm[ColBG]);
    initpixmap(getresource("button.left.pixmap", BLEFTPIXMAP), &look.bleft);
    initpixmap(getresource("button.right.pixmap", BRIGHTPIXMAP), &look.bright);
    initpixmap(getresource("button.center.pixmap", BCENTERPIXMAP), &look.bcenter);
    look.bleft.action = iconifyit;
    look.bright.action = killclient;
    look.bcenter.action = togglemax;
}

void
drawbuttons(Client *c) {
    int x, y;
    y = dc.h/2 - look.bleft.ph/2;
    x = c->w - 3*dc.h;
    XSetForeground(dpy, dc.gc, (c == sel) ? dc.sel[ColButton] : dc.norm[ColButton]);
    XSetBackground(dpy, dc.gc, (c == sel) ? dc.sel[ColBG] : dc.norm[ColBG]);

    XCopyPlane(dpy, look.bleft.pm, c->title, dc.gc, 0, 0, look.bleft.pw, look.bleft.ph, x, y+look.bleft.py, 1);
    x+=dc.h;
    XCopyPlane(dpy, look.bcenter.pm, c->title, dc.gc, 0, 0, look.bcenter.pw, look.bcenter.ph, x, y+look.bcenter.py, 1);
    x+=dc.h;
    XCopyPlane(dpy, look.bright.pm, c->title, dc.gc, 0, 0, look.bright.pw, look.bright.ph, x, y+look.bright.py, 1);
}

int
drawbutton(Drawable d, Drawable btn, unsigned long col[ColLast], int x, int y) {
    XSetForeground(dpy, dc.gc, col[ColButton]);
    XSetBackground(dpy, dc.gc, col[ColBG]);
    XCopyPlane(dpy, btn, d, dc.gc, 0, 0, look.bleft.pw, look.bleft.ph, x, y+look.bleft.py, 1);
    return dc.h;
}

void
drawclient(Client *c) {
    int i,j;
    unsigned int opacity;
    int center = 0;
    char title[] = "T|NIMC";

    if(look.uf_opacity) {
	opacity = (c == sel) ? OPAQUE : look.uf_opacity * OPAQUE;
	setopacity(c, opacity);
    }
    if(!isvisible(c, NULL))
	return;
    if(!c->title)
	return;
    /* XXX: that's not nice. we map and unmap title all the time */
    if(!c->isfloating && !ISLTFLOATING(c->m) && !dectiled) {
	XUnmapWindow(dpy, c->title);
	return;
    }
    XMapRaised(dpy, c->title);
    XSetForeground(dpy, dc.gc, c == sel ? dc.sel[ColBG] : dc.norm[ColBG]);
    XSetLineAttributes(dpy, dc.gc, look.borderpx, LineSolid, CapNotLast, JoinMiter);
    XFillRectangle(dpy, c->title, dc.gc, 0, 0, c->w, c->th);
    dc.x = dc.y = 0;
    dc.w = c->w;
    for(i = 0; i < strlen(title); i++) {
	switch(title[i]) {
	    case 'T':
	    case 't':
		for(j = 0; j < ntags; j++) {
		    if(c->tags[j]){
			drawtext(tags[j], c->title, c->xftdraw, c == sel ? dc.sel : dc.norm, TitleLeft);
			XSetForeground(dpy, dc.gc, c== sel ? dc.sel[ColBorder] : dc.norm[ColBorder]);
		    }
		}
		break;
	    case '|':
		dc.x += dc.h/4;
		XDrawLine(dpy, c->title, dc.gc, dc.x, 0, dc.x, dc.h);
		dc.x += dc.h/4;
		break;
	    case 'N':
	    case 'n':
		drawtext(c->name, c->title, c->xftdraw, c == sel ? dc.sel : dc.norm, look.tpos);
		break;
	    case 'I':
	    case 'i':
		dc.x += drawbutton(c->title, look.bleft.pm, c == sel ? dc.sel : dc.norm, dc.x, dc.h/2 - look.bleft.ph/2);
	}
    }
#if 0
    drawtext(NULL, c->title, c->xftdraw, c == sel ? dc.sel : dc.norm, look.tpos);
    if(look.tbpos) {
	for(i = 0; i < ntags; i++) {
	    if(c->tags[i]){
		drawtext(tags[i], c->title, c->xftdraw, c == sel ? dc.sel : dc.norm, TitleLeft);
		XSetForeground(dpy, dc.gc, c== sel ? dc.sel[ColBorder] : dc.norm[ColBorder]);
		if(c->border)
		    XDrawLine(dpy, c->title, dc.gc, dc.x+dc.h/2, 0, dc.x+dc.h/2, dc.h);
		dc.x+=dc.h/2+1;
	    }
	}
    }
    drawtext(c->name, c->title, c->xftdraw, c == sel ? dc.sel : dc.norm, look.tpos);
    if(c->w>=6*dc.h && dc.x <= c->w-6*dc.h && look.tpos != TitleRight)
	drawbuttons(c);
#endif
}

static void
initfont(const char *fontstr) {
    dc.font.xftfont = XftFontOpenXlfd(dpy,screen,fontstr);
    if(!dc.font.xftfont)
	 dc.font.xftfont = XftFontOpenName(dpy,screen,fontstr);
    if(!dc.font.xftfont)
	 eprint("error, cannot load font: '%s'\n", fontstr);
    dc.font.extents = emallocz(sizeof(XGlyphInfo));
    XftTextExtentsUtf8(dpy, dc.font.xftfont,
	(const unsigned char*)fontstr, strlen(fontstr), dc.font.extents);
    dc.font.height = dc.font.xftfont->ascent + dc.font.xftfont->descent + 1;
    dc.font.ascent = dc.font.xftfont->ascent;
    dc.font.descent = dc.font.xftfont->descent;
}

unsigned int
textnw(const char *text, unsigned int len) {
    XftTextExtentsUtf8(dpy, dc.font.xftfont,
	(const unsigned char*)text, strlen(text), dc.font.extents);
    return dc.font.extents->xOff;
}

unsigned int
textw(const char *text) {
    return textnw(text, strlen(text)) + dc.font.height;
}
