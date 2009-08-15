/*
 *  File backend directory scanner
 *  Copyright (C) 2008 - 2009 Andreas Öman
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include "showtime.h"
#include "navigator.h"
#include "fileaccess.h"
#include "fa_probe.h"
#include "playqueue.h"
#include "misc/strtab.h"

typedef struct scanner {
  int s_refcount;

  char *s_url;

  prop_t *s_nodes;
  prop_t *s_viewprop;
  prop_t *s_root;

  int s_stop;

  fa_dir_t *s_fd;

  void *s_ref;

} scanner_t;






/**
 *
 */
const char *type2str[] = {
  [CONTENT_DIR]      = "directory",
  [CONTENT_FILE]     = "file",
  [CONTENT_AUDIO]    = "audio",
  [CONTENT_ARCHIVE]  = "archive",
  [CONTENT_VIDEO]    = "video",
  [CONTENT_PLAYLIST] = "playlist",
  [CONTENT_DVD]      = "dvd",
  [CONTENT_IMAGE]    = "image",
};


/**
 *
 */
static void
set_type(prop_t *proproot, unsigned int type)
{
  if(type < sizeof(type2str) / sizeof(type2str[0]) && type2str[type] != NULL)
    prop_set_string(prop_create(proproot, "type"), type2str[type]);
}


/**
 *
 */
static void
add_prop(fa_dir_entry_t *fde, prop_t *root, fa_dir_entry_t *before)
{
  prop_t *p = prop_create(NULL, "node");

  prop_set_string(prop_create(p, "url"), fde->fde_url);
  set_type(p, fde->fde_type);

  prop_set_string(prop_create(prop_create(p, "metadata"), "title"), 
		  fde->fde_filename);

  if(prop_set_parent_ex(p, root, before ? before->fde_prop : NULL, NULL)) {
    prop_destroy(p);
  } else {
    fde->fde_prop = p;
    prop_ref_inc(p);
  }
}

/**
 *
 */
static struct strtab postfixtab[] = {
  { "jpeg",            CONTENT_IMAGE },
  { "jpg",             CONTENT_IMAGE },
};

/**
 *
 */
static void
meta_analyzer(fa_dir_t *fd, prop_t *viewprop, prop_t *root, int *stop)
{
  int type;
  prop_t *metadata, *p;

  int album_score = 0;
  int images = 0;
  int unknown = 0;
  char album_name[128] = {0};
  char artist_name[128] = {0};
  char album_art[1024] = {0};
  int64_t album_art_score = 0;  // Bigger is better
  char buf[128];
  int trackidx;
  fa_dir_entry_t *fde;
  const char *str;

  /* Empty */
  if(fd->fd_count == 0) {
    if(viewprop != NULL)
      prop_set_string(viewprop, "empty");
    return;
  }

  /* Scan all entries */
  TAILQ_FOREACH(fde, &fd->fd_entries, fde_link) {

    if(fde->fde_type == CONTENT_UNKNOWN)
      continue;

    if(fde->fde_type == CONTENT_FILE) {

      if((str = strrchr(fde->fde_filename, '.')) != NULL) {
	str++;

	/* Check filename postfix */
	if((type = str2val(str, postfixtab)) >= 0) {

	  fde->fde_type = type;
	  if(fde->fde_prop != NULL)
	    set_type(fde->fde_prop, fde->fde_type);
	}
      }
    }

    metadata = fde->fde_prop ? prop_create(fde->fde_prop, "metadata") : NULL;

    if(metadata != NULL) {

      if(stop && *stop)
	return;

      type = fde->fde_type;
    
      if(fde->fde_type == CONTENT_DIR) {
	type = fa_probe_dir(metadata, fde->fde_url);
      } else if(fde->fde_type == CONTENT_FILE) {
	type = fa_probe(metadata, fde->fde_url, NULL, 0, NULL, 0);
      }
      
      set_type(fde->fde_prop, type);
      fde->fde_type = type;
    }


    switch(fde->fde_type) {

    case CONTENT_IMAGE:
      images++;

      if(metadata != NULL) {
	/* Only check filesize when doing deep search */

	if(!strncasecmp(fde->fde_filename, "albumart", 8) ||
	   !strncasecmp(fde->fde_filename, "folder.", 7)) {

	  if(fde->fde_size == 0) {

	    struct stat st;
	    if(!fa_stat(fde->fde_url, &st, NULL, 0))
	      fde->fde_size = st.st_size;

	    if(fde->fde_size > album_art_score) {
	      album_art_score = fde->fde_size;
	      snprintf(album_art, sizeof(album_art), "%s", fde->fde_url);
	    }
	  }
	}
      }
      break;

    case CONTENT_UNKNOWN:
      unknown++;
      prop_destroy(fde->fde_prop);
      prop_ref_dec(fde->fde_prop);
      fde->fde_prop = NULL;
      break;
      
    case CONTENT_AUDIO:
      if(metadata == NULL)
	break;

      if((p = prop_get_by_names(metadata, "album", NULL)) == NULL ||
	 prop_get_string(p, buf, sizeof(buf))) {
	album_score--;
	break;
      }

      if(album_name[0] == 0) {
	snprintf(album_name, sizeof(album_name), "%s", buf);
	album_score++;
      } else if(!strcasecmp(album_name, buf)) {
	album_score++;
      } else {
	album_score--;
	break;
      }
      
      if((p = prop_get_by_names(metadata, "artist", NULL)) == NULL ||
	 prop_get_string(p, buf, sizeof(buf)))
	break;

      if(strstr(artist_name, buf))
	break;

      snprintf(artist_name + strlen(artist_name),
	       sizeof(artist_name) - strlen(artist_name),
	       "%s%s", artist_name[0] ? ", " : "", buf);
      break;

    default:
      album_score = INT32_MIN;
      break;

    }
  }

  if(album_score > 0) {
      
    /* It is an album */
    if(viewprop != NULL)
      prop_set_string(viewprop, "album");
      
    if(root != NULL) {
      prop_set_string(prop_create(root, "album_name"), album_name);

      if(artist_name[0])
	prop_set_string(prop_create(root, "artist_name"), artist_name);
  
      if(album_art[0])
	prop_set_string(prop_create(root, "album_art"), album_art);
    }

    trackidx = 1;

    /* Remove everything that is not audio */
    TAILQ_FOREACH(fde, &fd->fd_entries, fde_link) {
      if(fde->fde_type != CONTENT_AUDIO) {
	if(fde->fde_prop != NULL) {
	  prop_destroy(fde->fde_prop);
	  prop_ref_dec(fde->fde_prop);
	  fde->fde_prop = NULL;
	}

      } else {
	metadata = prop_create(fde->fde_prop, "metadata");
	prop_set_int(prop_create(metadata, "trackindex"), trackidx);
	trackidx++;

	if(album_art[0])
	  prop_set_string(prop_create(metadata, "album_art"), 
			  album_art);
      }
    }
  } else if(fd->fd_count == unknown) {
    if(viewprop != NULL)
      prop_set_string(viewprop, "empty");
  } else if(images * 4 > fd->fd_count * 3) {
    if(viewprop != NULL)
      prop_set_string(viewprop, "images");
  } else {
    if(viewprop != NULL)
      prop_set_string(viewprop, "list");
  }
}




/**
 *
 */
static int
scannercore(scanner_t *s)
{
  fa_dir_entry_t *fde;
  fa_dir_t *fd;

  if((s->s_fd = fa_scandir(s->s_url, NULL, 0)) == NULL) 
    return -1;

  fd = s->s_fd;

  fa_dir_sort(s->s_fd);

  meta_analyzer(s->s_fd, s->s_viewprop, s->s_root, &s->s_stop);

  /* Add filename and type */
  TAILQ_FOREACH(fde, &fd->fd_entries, fde_link) {
    if(s->s_stop)
      return 0;
    add_prop(fde, s->s_nodes, NULL);
  }

  meta_analyzer(s->s_fd, s->s_viewprop, s->s_root, &s->s_stop);
  
  return 0;
}


/**
 *
 */
static void
scanner_unref(scanner_t *s)
{
  if(atomic_add(&s->s_refcount, -1) > 1)
    return;

  fa_unreference(s->s_ref);
  free(s);
}

/**
 *
 */
static int
scanner_checkstop(void *opaque)
{
  scanner_t *s = opaque;
  return !!s->s_stop;
}


/**
 *
 */
static void
scanner_notification(void *opaque, fa_notify_op_t op, const char *filename,
		     const char *url, int type)
{
  scanner_t *s = opaque;
  fa_dir_entry_t *fde;
  prop_t *metadata;
  int r;

  if(filename[0] == '.')
    return; /* Skip all dot-filenames */

  switch(op) {
  case FA_NOTIFY_DEL:

    TAILQ_FOREACH(fde, &s->s_fd->fd_entries, fde_link)
      if(!strcmp(filename, fde->fde_filename))
	break;

    if(fde == NULL)
      break;
    
    if(fde->fde_prop != NULL)
      prop_destroy(fde->fde_prop);

    fa_dir_entry_free(s->s_fd, fde);
    break;

  case FA_NOTIFY_ADD:
    fde = fa_dir_insert(s->s_fd, url, filename, type);
    add_prop(fde, s->s_nodes, TAILQ_NEXT(fde, fde_link));
    
    metadata = prop_create(fde->fde_prop, "metadata");

    if(fde->fde_type == CONTENT_DIR) {
      r = fa_probe_dir(metadata, fde->fde_url);
    } else {
      r = fa_probe(metadata, fde->fde_url, NULL, 0, NULL, 0);
    }

    set_type(fde->fde_prop, r);
    fde->fde_type = r;
    break;
  }
  meta_analyzer(s->s_fd, s->s_viewprop, s->s_root, &s->s_stop);
}

/**
 *
 */
static void *
scanner(void *aux)
{
  scanner_t *s = aux;

  s->s_ref = fa_reference(s->s_url);
  
  if(scannercore(s) != -1) {
    fa_notify(s->s_url, s, scanner_notification, scanner_checkstop);
    fa_dir_free(s->s_fd);
  }
  
  free(s->s_url);
  prop_ref_dec(s->s_root);
  prop_ref_dec(s->s_nodes);

  if(s->s_viewprop != NULL)
    prop_ref_dec(s->s_viewprop);

  scanner_unref(s);
  return NULL;
}


/**
 *
 */
static void
scanner_stop(void *opaque, prop_event_t event, ...)
{
  prop_t *p;
  scanner_t *s = opaque;

  va_list ap;
  va_start(ap, event);

  if(event != PROP_DESTROYED) 
    return;

  p = va_arg(ap, prop_t *);
  prop_unsubscribe(va_arg(ap, prop_sub_t *));

  s->s_stop = 1;
  scanner_unref(s);
}


/**
 *
 */
void
fa_scanner(const char *url, prop_t *root, int flags)
{
  scanner_t *s = calloc(1, sizeof(scanner_t));

  s->s_url = strdup(url);

  s->s_root = root;
  prop_ref_inc(s->s_root);

  s->s_nodes = prop_create(root, "nodes");
  prop_ref_inc(s->s_nodes);

  if(flags & FA_SCANNER_DETERMINE_VIEW) {

    s->s_viewprop = prop_create(root, "view");
    prop_ref_inc(s->s_viewprop);
  }

  s->s_refcount = 2; // One held by scanner thread, one by the subscription

  hts_thread_create_detached(scanner, s);

  prop_subscribe(PROP_SUB_TRACK_DESTROY,
		 PROP_TAG_CALLBACK, scanner_stop, s,
		 PROP_TAG_ROOT, s->s_root,
		 NULL);
}


typedef struct album_art_scanner {
  char *aas_url;
  prop_t *aas_prop;

} album_art_scanner_t;


/**
 *
 */
static void *
album_art_scanner(void *aux)
{
  album_art_scanner_t *aas = aux;
  int64_t album_art_score = 0;  // Bigger is better
  char album_art[1024] = {0};
  fa_dir_entry_t *fde;
  fa_dir_t *fd;

  if((fd = fa_scandir(aas->aas_url, NULL, 0)) != NULL) {

    TAILQ_FOREACH(fde, &fd->fd_entries, fde_link) {
      if(!strncasecmp(fde->fde_filename, "albumart", 8) ||
	 !strncasecmp(fde->fde_filename, "folder.", 7)) {

	if(fde->fde_size == 0) {

	  struct stat st;
	  if(!fa_stat(fde->fde_url, &st, NULL, 0))
	    fde->fde_size = st.st_size;

	  if(fde->fde_size > album_art_score) {
	    album_art_score = fde->fde_size;
	    snprintf(album_art, sizeof(album_art), "%s", fde->fde_url);
	  }
	}
      }
    }

    if(album_art[0])
      prop_set_string(aas->aas_prop, album_art);
    
    fa_dir_free(fd);
  }


  prop_ref_dec(aas->aas_prop);
  free(aas->aas_url);
  free(aas);
  return NULL;
}


/**
 *
 */
void
fa_scanner_find_albumart(const char *url, prop_t *album_art)
{
  album_art_scanner_t *aas = malloc(sizeof(album_art_scanner_t));

  aas->aas_url = strdup(url);
  aas->aas_prop = album_art;
  
  hts_thread_create_detached(album_art_scanner, aas);

}