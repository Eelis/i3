#undef I3__FILE__
#define I3__FILE__ "window.c"
/*
 * vim:ts=4:sw=4:expandtab
 *
 * i3 - an improved dynamic tiling window manager
 * © 2009-2012 Michael Stapelberg and contributors (see also: LICENSE)
 *
 * window.c: Updates window attributes (X11 hints/properties).
 *
 */
#include "all.h"

/*
 * Updates the WM_CLASS (consisting of the class and instance) for the
 * given window.
 *
 */
void window_update_class(i3Window *win, xcb_get_property_reply_t *prop, bool before_mgmt) {
    if (prop == NULL || xcb_get_property_value_length(prop) == 0) {
        DLOG("WM_CLASS not set.\n");
        FREE(prop);
        return;
    }

    /* We cannot use asprintf here since this property contains two
     * null-terminated strings (for compatibility reasons). Instead, we
     * use strdup() on both strings */
    char *new_class = xcb_get_property_value(prop);

    FREE(win->class_instance);
    FREE(win->class_class);

    win->class_instance = sstrdup(new_class);
    if ((strlen(new_class) + 1) < xcb_get_property_value_length(prop))
        win->class_class = sstrdup(new_class + strlen(new_class) + 1);
    else win->class_class = NULL;
    LOG("WM_CLASS changed to %s (instance), %s (class)\n",
        win->class_instance, win->class_class);

    if (before_mgmt) {
        free(prop);
        return;
    }

    run_assignments(win);

    free(prop);
}

/*
 * Updates the name by using _NET_WM_NAME (encoded in UTF-8) for the given
 * window. Further updates using window_update_name_legacy will be ignored.
 *
 */
void window_update_name(i3Window *win, xcb_get_property_reply_t *prop, bool before_mgmt) {
    if (prop == NULL || xcb_get_property_value_length(prop) == 0) {
        DLOG("_NET_WM_NAME not specified, not changing\n");
        FREE(prop);
        return;
    }

    i3string_free(win->name);
    win->name = i3string_from_utf8_with_length(xcb_get_property_value(prop),
                                               xcb_get_property_value_length(prop));
    win->name_x_changed = true;
    LOG("_NET_WM_NAME changed to \"%s\"\n", i3string_as_utf8(win->name));

    win->uses_net_wm_name = true;

    if (before_mgmt) {
        free(prop);
        return;
    }

    run_assignments(win);

    free(prop);
}

/*
 * Updates the name by using WM_NAME (encoded in COMPOUND_TEXT). We do not
 * touch what the client sends us but pass it to xcb_image_text_8. To get
 * proper unicode rendering, the application has to use _NET_WM_NAME (see
 * window_update_name()).
 *
 */
void window_update_name_legacy(i3Window *win, xcb_get_property_reply_t *prop, bool before_mgmt) {
    if (prop == NULL || xcb_get_property_value_length(prop) == 0) {
        DLOG("WM_NAME not set (_NET_WM_NAME is what you want anyways).\n");
        FREE(prop);
        return;
    }

    /* ignore update when the window is known to already have a UTF-8 name */
    if (win->uses_net_wm_name) {
        free(prop);
        return;
    }

    i3string_free(win->name);
    win->name = i3string_from_utf8_with_length(xcb_get_property_value(prop),
                                               xcb_get_property_value_length(prop));

    LOG("WM_NAME changed to \"%s\"\n", i3string_as_utf8(win->name));
    LOG("Using legacy window title. Note that in order to get Unicode window "
        "titles in i3, the application has to set _NET_WM_NAME (UTF-8)\n");

    win->name_x_changed = true;

    if (before_mgmt) {
        free(prop);
        return;
    }

    run_assignments(win);

    free(prop);
}

/*
 * Updates the CLIENT_LEADER (logical parent window).
 *
 */
void window_update_leader(i3Window *win, xcb_get_property_reply_t *prop) {
    if (prop == NULL || xcb_get_property_value_length(prop) == 0) {
        DLOG("CLIENT_LEADER not set.\n");
        FREE(prop);
        return;
    }

    xcb_window_t *leader = xcb_get_property_value(prop);
    if (leader == NULL) {
        free(prop);
        return;
    }

    DLOG("Client leader changed to %08x\n", *leader);

    win->leader = *leader;

    free(prop);
}

/*
 * Updates the TRANSIENT_FOR (logical parent window).
 *
 */
void window_update_transient_for(i3Window *win, xcb_get_property_reply_t *prop) {
    if (prop == NULL || xcb_get_property_value_length(prop) == 0) {
        DLOG("TRANSIENT_FOR not set.\n");
        FREE(prop);
        return;
    }

    xcb_window_t transient_for;
    if (!xcb_icccm_get_wm_transient_for_from_reply(&transient_for, prop)) {
        free(prop);
        return;
    }

    DLOG("Transient for changed to %08x\n", transient_for);

    win->transient_for = transient_for;

    free(prop);
}

/*
 * Updates the _NET_WM_STRUT_PARTIAL (reserved pixels at the screen edges)
 *
 */
void window_update_strut_partial(i3Window *win, xcb_get_property_reply_t *prop) {
    if (prop == NULL || xcb_get_property_value_length(prop) == 0) {
        DLOG("_NET_WM_STRUT_PARTIAL not set.\n");
        FREE(prop);
        return;
    }

    uint32_t *strut;
    if (!(strut = xcb_get_property_value(prop))) {
        free(prop);
        return;
    }

    DLOG("Reserved pixels changed to: left = %d, right = %d, top = %d, bottom = %d\n",
         strut[0], strut[1], strut[2], strut[3]);

    win->reserved = (struct reservedpx){ strut[0], strut[1], strut[2], strut[3] };

    free(prop);
}

/*
 * Updates the WM_WINDOW_ROLE
 *
 */
void window_update_role(i3Window *win, xcb_get_property_reply_t *prop, bool before_mgmt) {
    if (prop == NULL || xcb_get_property_value_length(prop) == 0) {
        DLOG("WM_WINDOW_ROLE not set.\n");
        FREE(prop);
        return;
    }

    char *new_role;
    if (asprintf(&new_role, "%.*s", xcb_get_property_value_length(prop),
                 (char*)xcb_get_property_value(prop)) == -1) {
        perror("asprintf()");
        DLOG("Could not get WM_WINDOW_ROLE\n");
        free(prop);
        return;
    }
    FREE(win->role);
    win->role = new_role;
    LOG("WM_WINDOW_ROLE changed to \"%s\"\n", win->role);

    if (before_mgmt) {
        free(prop);
        return;
    }

    run_assignments(win);

    free(prop);
}

/*
 * Updates the WM_HINTS (we only care about the input focus handling part).
 *
 */
void window_update_hints(i3Window *win, xcb_get_property_reply_t *prop) {
    if (prop == NULL || xcb_get_property_value_length(prop) == 0) {
        DLOG("WM_HINTS not set.\n");
        FREE(prop);
        return;
    }

    xcb_icccm_wm_hints_t hints;

    if (!xcb_icccm_get_wm_hints_from_reply(&hints, prop)) {
        DLOG("Could not get WM_HINTS\n");
        free(prop);
        return;
    }

    win->doesnt_accept_focus = !hints.input;
    LOG("WM_HINTS.input changed to \"%d\"\n", hints.input);

    free(prop);
}

#ifdef USE_ICONS
/*
 * Copy and resize icon if needed
 */
void copy_icon_with_resize(uint32_t *dst, int width, int height, uint32_t* src, int s_width, int s_height)
{
    int i, j;
    if (width==s_width && height==s_height) {
        /*  easy case, same dimensions, just copy data */
        memcpy(dst, src, width*height*sizeof(uint32_t));
    }
    else {
        uint32_t* row = src;
        int xstep = s_width/width;
        int ystep = s_height/height*s_width;

        for(i=0; i < height; ++i) {
            uint32_t* ptr = row;
            for(j=0; j < width; ++j) {
                *dst++ = *ptr;
                ptr+=xstep;
            }
            row += ystep;
        }
    }
}


void window_update_icon(i3Window *win, xcb_get_property_reply_t *prop)
{
    uint32_t *data = NULL;
    uint64_t len = 0;

    if(!prop || prop->type != XCB_ATOM_CARDINAL || prop->format != 32) {
    	DLOG("_NET_WM_ICON is not set\n");
        FREE(prop);
        return;
    }

    uint32_t prop_value_len = xcb_get_property_value_length(prop);
    uint32_t *prop_value = (uint32_t *) xcb_get_property_value(prop);

    /* Find the number of icons in the reply. */
    while(prop_value_len > (sizeof(uint32_t) * 2) && prop_value && prop_value[0] && prop_value[1])
    {
    	/* Check that the property is as long as it should be (in bytes),
         handling integer overflow. "+2" to handle the width and height
         fields. */
    	const uint64_t crt_len = prop_value[0] * (uint64_t) prop_value[1];
    	const uint64_t expected_len = (crt_len + 2) * 4;
    	if(expected_len > prop_value_len)
    		break;

    	if (len==0 || (crt_len>=16*16 && crt_len<len)) {
    		len = crt_len;
    		data  = prop_value;
    	}
    	if (len==16*16) break; // found 16 pixels icon

    	/* Find pointer to next icon in the reply. */
    	prop_value_len -= expected_len;
    	prop_value = (uint32_t *) (((uint8_t *) prop_value) + expected_len);
    }

    if (!data ) {
    	DLOG("Could not get _NET_WM_ICON\n");
    	free(prop);
    	return;
    }

    LOG("Got _NET_WM_ICON of size: (%d,%d)\n", data[0], data[1]);

    FREE(win->icon);
    win->icon = malloc(16*16*sizeof(uint32_t));
    copy_icon_with_resize(win->icon, 16, 16, data+2, data[0], data[1]);

    free(prop);
}
#endif /* USE_ICONS */
