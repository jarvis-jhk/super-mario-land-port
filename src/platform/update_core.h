/*
 * Release update logic without SDL or network access, so it can be
 * unit-tested: version comparison, the GitHub release list, the update
 * manifest and the changelog sections to show after an update.
 */
#ifndef SML_UPDATE_CORE_H
#define SML_UPDATE_CORE_H

#include <stddef.h>
#include <stdint.h>

/* Compares dotted numeric versions ("0.1.10" > "0.1.9", an optional leading
   'v' is ignored). Returns -1, 0 or 1; -2 if either is not a release version
   (development builds such as "M0-dev" never update). */
int version_compare(const char *a, const char *b);

/* Finds the highest release version among the "tag_name" fields of a GitHub
   releases JSON list. Returns 0 and the version without 'v', or -1. */
int update_latest_tag(const char *json, char *out, size_t out_len);

/* Update manifest (update.txt, one release asset):
       version 0.1.1
       file <asset name> <size> <sha256 hex>
   Returns 0 when the manifest names `version` and lists `asset`. */
int update_manifest_find(const char *manifest, const char *version, const char *asset,
                         uint64_t *size, char sha256[65]);

/* The changelog sections ("## <version> ...") newer than `seen` (NULL or ""
   for a first start: only the current version) up to `current`, as a
   malloc'd string, or NULL if there is nothing to show. */
char *changelog_since(const char *changelog, const char *seen, const char *current);

/* Turns changelog markdown into display paragraphs: hard-wrapped lines
   are joined, "## " heading marks are dropped, list items keep "- ".
   Returns a malloc'd string. */
char *changelog_reflow(const char *markdown);

#endif
