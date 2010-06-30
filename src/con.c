/*
 * vim:ts=4:sw=4:expandtab
 *
 * i3 - an improved dynamic tiling window manager
 * © 2009-2010 Michael Stapelberg and contributors (see also: LICENSE)
 *
 * con.c contains all functions which deal with containers directly (creating
 * containers, searching containers, getting specific properties from
 * containers, …).
 *
 */
#include "all.h"

char *colors[] = {
    "#ff0000",
    "#00FF00",
    "#0000FF",
    "#ff00ff",
    "#00ffff",
    "#ffff00",
    "#aa0000",
    "#00aa00",
    "#0000aa",
    "#aa00aa"
};


Con *con_new(Con *parent) {
    Con *new = scalloc(sizeof(Con));
    TAILQ_INSERT_TAIL(&all_cons, new, all_cons);
    new->type = CT_CON;
    new->name = strdup("");
    static int cnt = 0;
    LOG("opening window %d\n", cnt);

    /* TODO: remove window coloring after test-phase */
    LOG("color %s\n", colors[cnt]);
    new->name = strdup(colors[cnt]);
    uint32_t cp = get_colorpixel(colors[cnt]);
    cnt++;
    if ((cnt % (sizeof(colors) / sizeof(char*))) == 0)
        cnt = 0;

    x_con_init(new);

    xcb_change_window_attributes(conn, new->frame, XCB_CW_BACK_PIXEL, &cp);

    TAILQ_INIT(&(new->floating_head));
    TAILQ_INIT(&(new->nodes_head));
    TAILQ_INIT(&(new->focus_head));
    TAILQ_INIT(&(new->swallow_head));

    if (parent != NULL)
        con_attach(new, parent);

    return new;
}

void con_attach(Con *con, Con *parent) {
    con->parent = parent;
    TAILQ_INSERT_TAIL(&(parent->nodes_head), con, nodes);
    /* We insert to the TAIL because con_focus() will correct this.
     * This way, we have the option to insert Cons without having
     * to focus them. */
    TAILQ_INSERT_TAIL(&(parent->focus_head), con, focused);
}

void con_detach(Con *con) {
    if (con->type == CT_FLOATING_CON) {
        TAILQ_REMOVE(&(con->parent->floating_head), con, floating_windows);
        TAILQ_REMOVE(&(con->parent->focus_head), con, focused);
    } else {
        TAILQ_REMOVE(&(con->parent->nodes_head), con, nodes);
        TAILQ_REMOVE(&(con->parent->focus_head), con, focused);
    }
}

/*
 * Sets input focus to the given container. Will be updated in X11 in the next
 * run of x_push_changes().
 *
 */
void con_focus(Con *con) {
    assert(con != NULL);

    /* 1: set focused-pointer to the new con */
    /* 2: exchange the position of the container in focus stack of the parent all the way up */
    TAILQ_REMOVE(&(con->parent->focus_head), con, focused);
    TAILQ_INSERT_HEAD(&(con->parent->focus_head), con, focused);
    if (con->parent->parent != NULL)
        con_focus(con->parent);

    focused = con;
    if (con->urgent) {
        con->urgent = false;
        workspace_update_urgent_flag(con_get_workspace(con));
    }
}

/*
 * Returns true when this node is a leaf node (has no children)
 *
 */
bool con_is_leaf(Con *con) {
    return TAILQ_EMPTY(&(con->nodes_head));
}

/*
 * Returns true if this node accepts a window (if the node swallows windows,
 * it might already have swallowed enough and cannot hold any more).
 *
 */
bool con_accepts_window(Con *con) {
    /* 1: workspaces never accept direct windows */
    if (con->type == CT_WORKSPACE)
        return false;

    if (con->orientation != NO_ORIENTATION) {
        DLOG("container %p does not accepts windows, orientation != NO_ORIENTATION\n", con);
        return false;
    }

    /* TODO: if this is a swallowing container, we need to check its max_clients */
    return (con->window == NULL);
}

/*
 * Gets the output container (first container with CT_OUTPUT in hierarchy) this
 * node is on.
 *
 */
Con *con_get_output(Con *con) {
    Con *result = con;
    while (result != NULL && result->type != CT_OUTPUT)
        result = result->parent;
    /* We must be able to get an output because focus can never be set higher
     * in the tree (root node cannot be focused). */
    assert(result != NULL);
    return result;
}

/*
 * Gets the workspace container this node is on.
 *
 */
Con *con_get_workspace(Con *con) {
    Con *result = con;
    while (result != NULL && result->type != CT_WORKSPACE)
        result = result->parent;
    assert(result != NULL);
    return result;
}

/*
 * helper data structure for the breadth-first-search in
 * con_get_fullscreen_con()
 *
 */
struct bfs_entry {
    Con *con;

    TAILQ_ENTRY(bfs_entry) entries;
};

/*
 * Returns the first fullscreen node below this node.
 *
 */
Con *con_get_fullscreen_con(Con *con) {
    Con *current, *child;

    LOG("looking for fullscreen node\n");
    /* TODO: is breadth-first-search really appropriate? (check as soon as
     * fullscreen levels and fullscreen for containers is implemented) */
    TAILQ_HEAD(bfs_head, bfs_entry) bfs_head = TAILQ_HEAD_INITIALIZER(bfs_head);
    struct bfs_entry *entry = smalloc(sizeof(struct bfs_entry));
    entry->con = con;
    TAILQ_INSERT_TAIL(&bfs_head, entry, entries);

    while (!TAILQ_EMPTY(&bfs_head)) {
        entry = TAILQ_FIRST(&bfs_head);
        current = entry->con;
        LOG("checking %p\n", current);
        if (current != con && current->fullscreen_mode != CF_NONE) {
            /* empty the queue */
            while (!TAILQ_EMPTY(&bfs_head)) {
                entry = TAILQ_FIRST(&bfs_head);
                TAILQ_REMOVE(&bfs_head, entry, entries);
                free(entry);
            }
            return current;
        }

        LOG("deleting from queue\n");
        TAILQ_REMOVE(&bfs_head, entry, entries);
        free(entry);

        TAILQ_FOREACH(child, &(current->nodes_head), nodes) {
            entry = smalloc(sizeof(struct bfs_entry));
            entry->con = child;
            TAILQ_INSERT_TAIL(&bfs_head, entry, entries);
        }
    }

    return NULL;
}

/*
 * Returns true if the node is floating.
 *
 */
bool con_is_floating(Con *con) {
    assert(con != NULL);
    LOG("checking if con %p is floating\n", con);
    return (con->floating >= FLOATING_AUTO_ON);
}

Con *con_by_window_id(xcb_window_t window) {
    Con *con;
    TAILQ_FOREACH(con, &all_cons, all_cons)
        if (con->window != NULL && con->window->id == window)
            return con;
    return NULL;
}

Con *con_by_frame_id(xcb_window_t frame) {
    Con *con;
    TAILQ_FOREACH(con, &all_cons, all_cons)
        if (con->frame == frame)
            return con;
    return NULL;
}

/*
 * Returns the first container which wants to swallow this window
 * TODO: priority
 *
 */
Con *con_for_window(i3Window *window, Match **store_match) {
    Con *con;
    Match *match;
    LOG("searching con for window %p\n", window);
    LOG("class == %s\n", window->class_class);

    TAILQ_FOREACH(con, &all_cons, all_cons)
        TAILQ_FOREACH(match, &(con->swallow_head), matches) {
            if (!match_matches_window(match, window))
                continue;
            if (store_match != NULL)
                *store_match = match;
            return con;
        }

    return NULL;
}

/*
 * Updates the percent attribute of the children of the given container. This
 * function needs to be called when a window is added or removed from a
 * container.
 *
 */
void con_fix_percent(Con *con, int action) {
    Con *child;
    int children = 0;
    TAILQ_FOREACH(child, &(con->nodes_head), nodes)
        children++;
    /* TODO: better document why this math works */
    double fix;
    if (action == WINDOW_ADD)
        fix = (1.0 - (1.0 / (children+1)));
    else
        fix = 1.0 / (1.0 - (1.0 / (children+1)));

    TAILQ_FOREACH(child, &(con->nodes_head), nodes) {
        if (child->percent <= 0.0)
            continue;
        child->percent *= fix;
    }
}

void con_toggle_fullscreen(Con *con) {
    Con *workspace, *fullscreen;
    LOG("toggling fullscreen for %p / %s\n", con, con->name);
    if (con->fullscreen_mode == CF_NONE) {
        /* 1: check if there already is a fullscreen con */
        workspace = con_get_workspace(con);
        if ((fullscreen = con_get_fullscreen_con(workspace)) != NULL) {
            LOG("Not entering fullscreen mode, container (%p/%s) "
                "already is in fullscreen mode\n",
                fullscreen, fullscreen->name);
            return;
        }

        /* 2: enable fullscreen */
        con->fullscreen_mode = CF_OUTPUT;
    } else {
        /* 1: disable fullscreen */
        con->fullscreen_mode = CF_NONE;
    }
    LOG("mode now: %d\n", con->fullscreen_mode);

    /* update _NET_WM_STATE if this container has a window */
    /* TODO: when a window is assigned to a container which is already
     * fullscreened, this state needs to be pushed to the client, too */
    if (con->window == NULL)
        return;

    uint32_t values[1];
    unsigned int num = 0;

    if (con->fullscreen_mode != CF_NONE)
        values[num++] = atoms[_NET_WM_STATE_FULLSCREEN];

    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, con->window->id,
                        atoms[_NET_WM_STATE], ATOM, 32, num, values);
}